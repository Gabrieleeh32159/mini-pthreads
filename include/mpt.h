/*
 * mpt.h - mini Pthreads: librería de hilos a nivel de usuario.
 *
 * Reproduce en pequeño la arquitectura de Mueller (1993), "A Library
 * Implementation of POSIX Threads under UNIX": kernel de la librería como
 * monitor monolítico (kernel flag + dispatcher flag), manejador universal de
 * señales con diferimiento, cambio de contexto con ucontext, mutex con
 * protocolos de herencia/techo de prioridad y "perverted scheduling".
 */
#ifndef MPT_H
#define MPT_H

#define MPT_MAX_PRIO   7
#define MPT_STACK_SIZE (256 * 1024)

typedef struct mpt_thread mpt_t;

typedef enum { MPT_PROTO_NONE, MPT_PROTO_INHERIT, MPT_PROTO_CEILING } mpt_proto_t;

typedef struct mpt_mutex {
    int         locked;
    mpt_t      *owner;
    mpt_proto_t proto;
    int         ceiling;   /* techo de prioridad (solo MPT_PROTO_CEILING) */
    mpt_t      *waiters;   /* lista ordenada por prioridad */
} mpt_mutex_t;

typedef struct mpt_cond {
    mpt_t *waiters;
} mpt_cond_t;

/* ---- gestión de hilos ------------------------------------------------- */
void        mpt_init(int main_prio);
mpt_t      *mpt_create(void (*fn)(void *), void *arg, int prio, const char *name);
void        mpt_yield(void);
void        mpt_exit(void);
void        mpt_join(mpt_t *t);
mpt_t      *mpt_self(void);
const char *mpt_name(mpt_t *t);
int         mpt_prio(mpt_t *t);
void        mpt_suspend(void);          /* el hilo actual se bloquea hasta mpt_resume */
void        mpt_resume(mpt_t *t);

/* ---- sincronización --------------------------------------------------- */
void mpt_mutex_init(mpt_mutex_t *m, mpt_proto_t proto, int ceiling);
void mpt_mutex_lock(mpt_mutex_t *m);
void mpt_mutex_unlock(mpt_mutex_t *m);
void mpt_cond_init(mpt_cond_t *c);
void mpt_cond_wait(mpt_cond_t *c, mpt_mutex_t *m);
void mpt_cond_signal(mpt_cond_t *c);
void mpt_cond_broadcast(mpt_cond_t *c);

/* ---- apropiación por temporizador (SIGALRM) --------------------------- */
void mpt_preempt_start(unsigned quantum_us);
void mpt_preempt_stop(void);

/* ---- depuración: perverted scheduling y monitor ----------------------- */
typedef enum { MPT_SCHED_FIFO, MPT_SCHED_MUTEX_SWITCH, MPT_SCHED_RANDOM_SWITCH } mpt_sched_t;
void mpt_set_sched(mpt_sched_t mode, unsigned seed);
void mpt_set_monitor(int enabled);      /* 0 = desactiva el monitor monolítico (demo 1) */
void mpt_set_race_window(int spins);    /* ensancha la sección crítica interna (demo 1) */
int  mpt_check_queues(void);            /* 0 si las colas de listos están íntegras */

extern unsigned long mpt_stat_signals, mpt_stat_deferred, mpt_stat_switches;

#endif
