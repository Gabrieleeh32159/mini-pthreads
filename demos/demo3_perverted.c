/*
 * Demo 3: perverted scheduling (sección "Testing and Debugging" del paper).
 *
 * Dos hilos retiran de una cuenta con una carrera sutil: verifican el saldo
 * bajo el mutex, lo sueltan, y vuelven a tomarlo para retirar (check-then-act).
 * Bajo FIFO el error nunca aparece, porque nadie se intercala entre ambas
 * secciones críticas. Con "mutex switch" o "random switch" la librería fuerza
 * conmutaciones y la carrera se manifiesta; con la misma semilla, el
 * entrelazado se repite exactamente.
 */
#include "mpt.h"
#include <stdio.h>

static mpt_mutex_t m;
static int balance;
static int quiet;

static void withdraw(void *arg)
{
    const char *who = arg;
    mpt_mutex_lock(&m);
    int ok = balance >= 100;
    if (!quiet) printf("    %s: verifica saldo = %d -> %s\n", who, balance, ok ? "puede retirar" : "sin fondos");
    mpt_mutex_unlock(&m);
    if (ok) {
        mpt_mutex_lock(&m);
        balance -= 100;
        if (!quiet) printf("    %s: retira 100, saldo = %d\n", who, balance);
        mpt_mutex_unlock(&m);
    }
}

static int run(const char *title, mpt_sched_t mode, unsigned seed)
{
    balance = 100;
    mpt_init(MPT_MAX_PRIO);        /* main no es desplazado: los hilos arrancan en el join */
    mpt_set_sched(mode, seed);
    mpt_mutex_init(&m, MPT_PROTO_NONE, 0);
    if (!quiet) printf("  %s\n", title);
    mpt_t *a = mpt_create(withdraw, "A", 1, "A");
    mpt_t *b = mpt_create(withdraw, "B", 1, "B");
    mpt_join(a); mpt_join(b);
    if (!quiet) printf("    saldo final = %4d  %s\n\n", balance,
                       balance < 0 ? "<-- ERROR: doble retiro (carrera expuesta)" : "OK");
    return balance;
}

int main(void)
{
    puts("Demo 3: perverted scheduling como herramienta de depuración\n");
    run("Política FIFO (normal)", MPT_SCHED_FIFO, 0);
    run("Política MUTEX SWITCH (conmuta en cada lock)", MPT_SCHED_MUTEX_SWITCH, 0);

    puts("  Política RANDOM SWITCH, distintas semillas:");
    quiet = 1;
    unsigned failing = 0;
    for (unsigned s = 1; s <= 8; s++) {
        int r = run("", MPT_SCHED_RANDOM_SWITCH, s);
        printf("    semilla %u: saldo final = %4d  %s\n", s, r, r < 0 ? "ERROR" : "ok");
        if (r < 0 && !failing) failing = s;
    }
    if (failing) {
        int r1 = run("", MPT_SCHED_RANDOM_SWITCH, failing);
        int r2 = run("", MPT_SCHED_RANDOM_SWITCH, failing);
        printf("\n  Repetir la semilla %u dos veces: %d y %d -> el entrelazado es reproducible\n",
               failing, r1, r2);
    }
    quiet = 0;
    puts("\nLa carrera existe siempre; FIFO solo la esconde. Forzar conmutaciones la\n"
         "hace visible y la semilla permite repetir el mismo entrelazado.");
    return 0;
}
