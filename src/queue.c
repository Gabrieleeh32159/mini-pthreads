/* Colas de listos (una FIFO por prioridad) y listas de espera ordenadas. */
#include "mpt_internal.h"
#include <stddef.h>

struct rq { mpt_t *head, *tail; };
static struct rq rq[MPT_MAX_PRIO + 1];
static int ready_count;
static int race_window_spins;

void mpt_set_race_window(int spins) { race_window_spins = spins; }

/* Ventana artificial dentro de la sección crítica: hace observable, en una
 * demo corta, la carrera entre la operación sobre la cola y la señal del
 * temporizador. Con el monitor activo la señal se difiere y no pasa nada. */
static void race_window(void)
{
    for (volatile int i = 0; i < race_window_spins; i++)
        ;
}

void mpt_rq_reset(void)
{
    for (int p = 0; p <= MPT_MAX_PRIO; p++) rq[p].head = rq[p].tail = NULL;
    ready_count = 0;
}

void mpt_rq_push_tail(mpt_t *t)
{
    struct rq *q = &rq[t->prio];
    t->next = NULL;
    t->queued = 1;
    if (q->tail) { q->tail->next = t; race_window(); q->tail = t; }
    else         { q->head = t;       race_window(); q->tail = t; }
    ready_count++;
}

void mpt_rq_push_head(mpt_t *t)
{
    struct rq *q = &rq[t->prio];
    t->next = q->head;
    t->queued = 1;
    race_window();
    q->head = t;
    if (!q->tail) q->tail = t;
    ready_count++;
}

static mpt_t *unlink_node(struct rq *q, mpt_t *prev, mpt_t *t)
{
    if (prev) prev->next = t->next; else q->head = t->next;
    race_window();
    if (q->tail == t) q->tail = prev;
    t->next = NULL;
    t->queued = 0;
    ready_count--;
    return t;
}

void mpt_rq_remove(mpt_t *t)
{
    struct rq *q = &rq[t->prio];
    for (mpt_t *prev = NULL, *it = q->head; it; prev = it, it = it->next)
        if (it == t) { unlink_node(q, prev, t); return; }
}

mpt_t *mpt_rq_pop_highest(void)
{
    for (int p = MPT_MAX_PRIO; p >= 0; p--)
        if (rq[p].head) return unlink_node(&rq[p], NULL, rq[p].head);
    return NULL;
}

int mpt_rq_highest_prio(void)
{
    for (int p = MPT_MAX_PRIO; p >= 0; p--)
        if (rq[p].head) return p;
    return -1;
}

/* Random switch: elige el r-ésimo hilo listo (módulo el total). */
mpt_t *mpt_rq_pop_random(unsigned r)
{
    if (ready_count <= 0) return NULL;
    int k = (int)(r % (unsigned)ready_count);
    for (int p = MPT_MAX_PRIO; p >= 0; p--)
        for (mpt_t *prev = NULL, *it = rq[p].head; it; prev = it, it = it->next)
            if (k-- == 0) return unlink_node(&rq[p], prev, it);
    return NULL;
}

/* Verificación de integridad: recorre cada cola con un tope de pasos y
 * compara con el contador. Se hace con SIGALRM bloqueada para que la propia
 * verificación no sea interrumpida. */
int mpt_check_queues(void)
{
    sigset_t set, old;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, &old);

    int bad = 0, n = 0;
    for (int p = 0; p <= MPT_MAX_PRIO && !bad; p++) {
        mpt_t *last = NULL;
        int steps = 0;
        for (mpt_t *it = rq[p].head; it; last = it, it = it->next) {
            if (++steps > 4096 || it->prio != p || !it->queued) { bad = 1; break; }
            n++;
        }
        if (last != rq[p].tail) bad = 2;
    }
    if (!bad && n != ready_count) bad = 3;

    sigprocmask(SIG_SETMASK, &old, NULL);
    return bad;
}

/* Listas de espera (mutex, condvar): ordenadas por prioridad descendente,
 * FIFO entre iguales, para que despierte "el de mayor prioridad". */
void mpt_wl_insert(mpt_t **list, mpt_t *t)
{
    mpt_t **pp = list;
    while (*pp && (*pp)->prio >= t->prio) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
}

mpt_t *mpt_wl_pop(mpt_t **list)
{
    mpt_t *t = *list;
    if (t) { *list = t->next; t->next = NULL; }
    return t;
}
