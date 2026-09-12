/* Variables de condición: el mutex se libera atómicamente (dentro del
 * kernel) con la suspensión y se readquiere antes de retornar. */
#include "mpt_internal.h"
#include <string.h>

void mpt_cond_init(mpt_cond_t *c) { memset(c, 0, sizeof *c); }

void mpt_cond_wait(mpt_cond_t *c, mpt_mutex_t *m)
{
    mpt_enter_kernel();
    mpt_wl_insert(&c->waiters, mpt_current);
    mpt_block_current();
    mpt_mutex_release_locked(m);
    mpt_leave_kernel();
    mpt_mutex_lock(m);
}

void mpt_cond_signal(mpt_cond_t *c)
{
    mpt_enter_kernel();
    mpt_t *w = mpt_wl_pop(&c->waiters);
    if (w) mpt_make_ready(w);
    mpt_leave_kernel();
}

void mpt_cond_broadcast(mpt_cond_t *c)
{
    mpt_enter_kernel();
    for (mpt_t *w; (w = mpt_wl_pop(&c->waiters)); ) mpt_make_ready(w);
    mpt_leave_kernel();
}
