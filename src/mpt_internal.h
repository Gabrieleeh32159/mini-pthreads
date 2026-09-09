#ifndef MPT_INTERNAL_H
#define MPT_INTERNAL_H
#include "mpt.h"
#include <ucontext.h>
#include <signal.h>

enum mpt_state { MPT_READY, MPT_RUNNING, MPT_BLOCKED, MPT_TERMINATED };
#define MPT_MAX_HELD 8

/* Bloque de control de hilo (TCB) */
struct mpt_thread {
    int            id;
    char           name[16];
    ucontext_t     ctx;
    void          *stack;
    enum mpt_state state;
    int            prio, base_prio;
    void         (*fn)(void *);
    void          *arg;
    mpt_t         *next;      /* enlace en la cola de listos o en una lista de espera */
    int            queued;
    mpt_t         *joiner;
    mpt_mutex_t   *held[MPT_MAX_HELD]; int nheld;   /* herencia: búsqueda lineal al desbloquear */
    int            ceil_stack[MPT_MAX_HELD]; int nceil; /* techo: pila (SRP) */
    unsigned long  interrupted;
};

extern mpt_t *mpt_current;
extern volatile sig_atomic_t mpt_kernel_flag, mpt_dispatcher_flag, mpt_sig_pending;
extern int mpt_monitor_enabled;
extern mpt_sched_t mpt_sched_mode;
extern int mpt_random_pick;

/* kernel de la librería */
void mpt_enter_kernel(void);
void mpt_leave_kernel(void);
void mpt_dispatch(void);
void mpt_make_ready(mpt_t *t);
void mpt_block_current(void);
void mpt_timeslice(void);
void mpt_fatal(const char *msg);

/* colas de listos y listas de espera */
void   mpt_rq_reset(void);
void   mpt_rq_push_tail(mpt_t *t);
void   mpt_rq_push_head(mpt_t *t);
void   mpt_rq_remove(mpt_t *t);
mpt_t *mpt_rq_pop_highest(void);
mpt_t *mpt_rq_pop_random(unsigned r);
int    mpt_rq_highest_prio(void);
void   mpt_wl_insert(mpt_t **list, mpt_t *t);
mpt_t *mpt_wl_pop(mpt_t **list);

/* mutex (uso interno desde cond.c) */
void mpt_mutex_release_locked(mpt_mutex_t *m);

/* perverted scheduling */
unsigned mpt_rng(void);
int mpt_perverted_on_lock(void);
int mpt_perverted_on_leave(void);

#endif
