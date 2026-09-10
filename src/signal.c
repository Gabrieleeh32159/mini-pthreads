/* Manejador universal de señales y apropiación por temporizador. */
#include "mpt_internal.h"
#include <sys/time.h>
#include <stddef.h>

unsigned long mpt_stat_signals, mpt_stat_deferred;

/* Regla del paper: si la señal llega dentro del kernel se registra y se
 * atiende al salir (dispatcher flag); si llega fuera, se entra al kernel,
 * se dirige al hilo actual (fin de quantum) y se despacha. */
static void universal_handler(int sig)
{
    (void)sig;
    mpt_stat_signals++;
    if (mpt_kernel_flag) {
        mpt_stat_deferred++;
        mpt_sig_pending = 1;
        mpt_dispatcher_flag = 1;
        return;
    }
    mpt_enter_kernel();
    mpt_timeslice();
    mpt_leave_kernel();       /* puede conmutar; se vuelve aquí cuando el hilo es reelegido */
}

void mpt_preempt_start(unsigned quantum_us)
{
    struct sigaction sa;
    sa.sa_handler = universal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);

    struct itimerval it;
    it.it_interval.tv_sec = it.it_value.tv_sec = 0;
    it.it_interval.tv_usec = it.it_value.tv_usec = quantum_us;
    mpt_stat_signals = mpt_stat_deferred = 0;
    setitimer(ITIMER_REAL, &it, NULL);
}

void mpt_preempt_stop(void)
{
    struct itimerval zero = {{0, 0}, {0, 0}};
    setitimer(ITIMER_REAL, &zero, NULL);
    signal(SIGALRM, SIG_IGN);
}
