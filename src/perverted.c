/* Perverted scheduling (sección "Testing and Debugging" del paper):
 *  - mutex switch: en cada lock exitoso el hilo actual va al final de su cola;
 *  - random switch: al salir del kernel, con probabilidad 1/2 el hilo actual
 *    se reencola y el siguiente se elige al azar entre los listos.
 * El generador es propio para que una misma semilla repita el entrelazado. */
#include "mpt_internal.h"

mpt_sched_t mpt_sched_mode = MPT_SCHED_FIFO;
int mpt_random_pick;
static unsigned rng_state = 1;

void mpt_set_sched(mpt_sched_t mode, unsigned seed)
{
    mpt_sched_mode = mode;
    rng_state = seed ? seed : 1;
    mpt_random_pick = 0;
}

unsigned mpt_rng(void)
{
    unsigned x = rng_state;         /* xorshift32 */
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int mpt_perverted_on_lock(void)
{
    return mpt_sched_mode == MPT_SCHED_MUTEX_SWITCH;
}

int mpt_perverted_on_leave(void)
{
    mpt_t *cur = mpt_current;
    if (mpt_sched_mode != MPT_SCHED_RANDOM_SWITCH) return 0;
    if (cur->state != MPT_RUNNING || cur->queued) return 0;
    if (!(mpt_rng() & 1)) return 0;
    /* ponytail: el paper lo manda a la cola de menor prioridad; aquí va al
     * final de la suya para no romper el invariante prio == índice de cola. */
    cur->state = MPT_READY;
    mpt_rq_push_tail(cur);
    mpt_random_pick = 1;
    return 1;
}
