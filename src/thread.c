/* Kernel de la librería: TCB, colas, dispatcher y operaciones de hilos. */
#include "mpt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

mpt_t *mpt_current;
volatile sig_atomic_t mpt_kernel_flag, mpt_dispatcher_flag, mpt_sig_pending;
int mpt_monitor_enabled = 1;
unsigned long mpt_stat_switches;

static mpt_t main_tcb;
static int next_id;

void mpt_fatal(const char *msg)
{
    fflush(stdout);
    write(2, "mpt: ", 5);
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
    _exit(2);
}

void mpt_init(int main_prio)
{
    memset(&main_tcb, 0, sizeof main_tcb);
    strcpy(main_tcb.name, "main");
    main_tcb.prio = main_tcb.base_prio = main_prio;
    main_tcb.state = MPT_RUNNING;
    mpt_current = &main_tcb;
    next_id = 1;
    mpt_kernel_flag = mpt_dispatcher_flag = mpt_sig_pending = 0;
    mpt_stat_switches = 0;
    mpt_rq_reset();
}

void mpt_set_monitor(int enabled) { mpt_monitor_enabled = enabled; }

/* ---- monitor monolítico ------------------------------------------------ */

void mpt_enter_kernel(void)
{
    if (mpt_monitor_enabled) mpt_kernel_flag = 1;
}

/* Al salir del kernel: si el dispatcher flag está puesto se invoca el
 * dispatcher (posible cambio de contexto); si no, solo se baja el flag. */
void mpt_leave_kernel(void)
{
    if (mpt_perverted_on_leave()) mpt_dispatcher_flag = 1;
    if (mpt_dispatcher_flag) mpt_dispatch();
    else mpt_kernel_flag = 0;
}

/* Corre en el hilo que retoma el control tras un cambio de contexto (o al
 * arrancar). Si una señal llegó mientras estábamos en el kernel, se atiende
 * ahora y se vuelve a despachar; recién entonces se baja el kernel flag. */
static void kernel_epilogue(void)
{
    if (mpt_sig_pending) { mpt_dispatch(); return; }
    mpt_kernel_flag = 0;
}

void mpt_dispatch(void)
{
    mpt_dispatcher_flag = 0;
    if (mpt_sig_pending) { mpt_sig_pending = 0; mpt_timeslice(); }

    mpt_t *old = mpt_current;
    if (old->state == MPT_RUNNING && !old->queued) {
        old->state = MPT_READY;             /* fue desplazado, no cedió: va al frente */
        mpt_rq_push_head(old);
    }

    mpt_t *new = mpt_random_pick ? mpt_rq_pop_random(mpt_rng()) : mpt_rq_pop_highest();
    mpt_random_pick = 0;
    if (!new) mpt_fatal("no hay hilos listos (deadlock o cola de listos corrupta)");

    new->state = MPT_RUNNING;
    mpt_current = new;
    if (new != old) {
        mpt_stat_switches++;
        /* Si 'new' fue interrumpido por una señal, su contexto se guardó dentro
         * del manejador con SIGALRM bloqueada: swapcontext restaura esa máscara,
         * así que no puede apilarse otro manejador antes de que retorne el
         * primero (evita el crecimiento ilimitado de la pila del paper). */
        swapcontext(&old->ctx, &new->ctx);
    }
    kernel_epilogue();
}

void mpt_make_ready(mpt_t *t)
{
    t->state = MPT_READY;
    mpt_rq_push_tail(t);
    if (t->prio > mpt_current->prio) mpt_dispatcher_flag = 1;
}

void mpt_block_current(void)
{
    mpt_current->state = MPT_BLOCKED;
    mpt_dispatcher_flag = 1;
}

/* Fin de quantum: el hilo actual pasa al final de su cola de prioridad. */
void mpt_timeslice(void)
{
    mpt_current->interrupted++;
    if (mpt_current->state == MPT_RUNNING && !mpt_current->queued) {
        mpt_current->state = MPT_READY;
        mpt_rq_push_tail(mpt_current);
    }
    mpt_dispatcher_flag = 1;
}

/* ---- operaciones de hilos ---------------------------------------------- */

static void trampoline(void)
{
    kernel_epilogue();                      /* el dispatcher nos dejó el kernel flag puesto */
    mpt_current->fn(mpt_current->arg);
    mpt_exit();
}

mpt_t *mpt_create(void (*fn)(void *), void *arg, int prio, const char *name)
{
    mpt_enter_kernel();
    mpt_t *t = calloc(1, sizeof *t);
    if (!t) mpt_fatal("sin memoria");
    t->id = next_id++;
    strncpy(t->name, name, sizeof t->name - 1);
    t->fn = fn;
    t->arg = arg;
    t->prio = t->base_prio = prio < 0 ? 0 : prio > MPT_MAX_PRIO ? MPT_MAX_PRIO : prio;
    t->stack = malloc(MPT_STACK_SIZE);
    if (!t->stack) mpt_fatal("sin memoria para la pila");
    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = MPT_STACK_SIZE;
    t->ctx.uc_link = NULL;
    makecontext(&t->ctx, trampoline, 0);
    mpt_make_ready(t);
    mpt_leave_kernel();
    return t;
}

void mpt_yield(void)
{
    mpt_enter_kernel();
    if (!mpt_current->queued) {
        mpt_current->state = MPT_READY;
        mpt_rq_push_tail(mpt_current);
    }
    mpt_dispatcher_flag = 1;
    mpt_leave_kernel();
}

void mpt_exit(void)
{
    mpt_enter_kernel();
    mpt_current->state = MPT_TERMINATED;
    if (mpt_current->joiner) mpt_make_ready(mpt_current->joiner);
    mpt_dispatcher_flag = 1;
    mpt_leave_kernel();
    mpt_fatal("mpt_exit retornó");
}

void mpt_join(mpt_t *t)
{
    mpt_enter_kernel();
    if (t->state != MPT_TERMINATED) {
        t->joiner = mpt_current;
        mpt_block_current();
    }
    mpt_leave_kernel();
    free(t->stack);                          /* la pila se libera cuando ya no es la activa */
    free(t);
}

void mpt_suspend(void)
{
    mpt_enter_kernel();
    mpt_block_current();
    mpt_leave_kernel();
}

void mpt_resume(mpt_t *t)
{
    mpt_enter_kernel();
    if (t->state == MPT_BLOCKED) mpt_make_ready(t);
    mpt_leave_kernel();
}

mpt_t      *mpt_self(void)          { return mpt_current; }
const char *mpt_name(mpt_t *t)      { return t->name; }
int         mpt_prio(mpt_t *t)      { return t->prio; }
