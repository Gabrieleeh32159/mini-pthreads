/* Mutex con entrega directa al de mayor prioridad y protocolos contra la
 * inversión de prioridades: herencia (búsqueda lineal al desbloquear) y
 * techo vía SRP (pila de prioridades). */
#include "mpt_internal.h"
#include <string.h>

void mpt_mutex_init(mpt_mutex_t *m, mpt_proto_t proto, int ceiling)
{
    memset(m, 0, sizeof *m);
    m->proto = proto;
    m->ceiling = ceiling;
}

static void acquire(mpt_t *t, mpt_mutex_t *m)
{
    m->locked = 1;
    m->owner = t;
    if (t->nheld < MPT_MAX_HELD) t->held[t->nheld++] = m;
    if (m->proto == MPT_PROTO_CEILING) {
        t->ceil_stack[t->nceil++] = t->prio;       /* push del nivel previo */
        if (m->ceiling > t->prio) t->prio = m->ceiling;
    }
}

static void held_remove(mpt_t *t, mpt_mutex_t *m)
{
    for (int i = 0; i < t->nheld; i++)
        if (t->held[i] == m) { t->held[i] = t->held[--t->nheld]; return; }
}

/* Herencia: el dueño sube a la prioridad del contendiente. Si estaba en la
 * cola de listos se reubica en la cola que le corresponde ahora.
 * ponytail: no se propaga transitivamente (dueño bloqueado en otro mutex). */
static void boost(mpt_t *owner, int prio)
{
    int was_queued = owner->queued;
    if (was_queued) mpt_rq_remove(owner);
    owner->prio = prio;
    if (was_queued) mpt_rq_push_head(owner);
}

void mpt_mutex_lock(mpt_mutex_t *m)
{
    mpt_t *cur = mpt_current;
    mpt_enter_kernel();
    if (m->locked) {
        if (m->proto == MPT_PROTO_INHERIT && m->owner->prio < cur->prio)
            boost(m->owner, cur->prio);
        mpt_wl_insert(&m->waiters, cur);
        mpt_block_current();
        mpt_leave_kernel();      /* al volver, unlock ya nos entregó el mutex */
        mpt_enter_kernel();
    } else {
        acquire(cur, m);
    }
    if (mpt_perverted_on_lock()) {          /* mutex switch */
        if (!cur->queued) { cur->state = MPT_READY; mpt_rq_push_tail(cur); }
        mpt_dispatcher_flag = 1;
    }
    mpt_leave_kernel();
}

/* Debe llamarse con el kernel flag puesto. */
void mpt_mutex_release_locked(mpt_mutex_t *m)
{
    mpt_t *cur = mpt_current;
    held_remove(cur, m);
    if (m->proto == MPT_PROTO_CEILING && cur->nceil > 0) {
        cur->prio = cur->ceil_stack[--cur->nceil];  /* pop: nivel previo al lock */
    } else if (m->proto == MPT_PROTO_INHERIT) {
        int p = cur->base_prio;                       /* max(propia, contendientes de los otros mutex) */
        for (int i = 0; i < cur->nheld; i++)
            for (mpt_t *w = cur->held[i]->waiters; w; w = w->next)
                if (w->prio > p) p = w->prio;
        cur->prio = p;
    }
    mpt_t *w = mpt_wl_pop(&m->waiters);
    if (w) { acquire(w, m); mpt_make_ready(w); }
    else   { m->locked = 0; m->owner = NULL; }
    if (mpt_rq_highest_prio() > cur->prio) mpt_dispatcher_flag = 1;
}

void mpt_mutex_unlock(mpt_mutex_t *m)
{
    mpt_enter_kernel();
    mpt_mutex_release_locked(m);
    mpt_leave_kernel();
}
