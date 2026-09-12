/*
 * Demo 1: el monitor monolítico (kernel flag) frente a la señal del temporizador.
 *
 * El mismo programa corre dos veces en procesos hijos: con el monitor activo
 * y sin él. Varios hilos ceden el procesador en bucle mientras SIGALRM llega
 * cada 200 µs. Sin monitor, la señal que cae en medio de una operación sobre
 * la cola de listos invoca al dispatcher sobre una estructura a medio
 * modificar; con monitor, la señal se registra y se atiende al salir.
 */
#include "mpt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#define WORKERS 4
#define ITERS   20000
#define QUANTUM_US 200

static unsigned long checks;

static void worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        mpt_yield();
        if (i % 64 == 0) {
            checks++;
            int bad = mpt_check_queues();
            if (bad) {
                char msg[96];
                int n = snprintf(msg, sizeof msg,
                    "  [%s] cola de listos corrupta (código %d) en la iteración %d\n",
                    mpt_name(mpt_self()), bad, i);
                write(1, msg, n);
                _exit(3);
            }
        }
    }
}

static void child(int monitor)
{
    mpt_t *t[WORKERS];
    char names[WORKERS][8];
    mpt_init(MPT_MAX_PRIO);        /* main no es desplazado: los hilos arrancan en el join */
    mpt_set_monitor(monitor);
    mpt_set_race_window(300);
    for (int i = 0; i < WORKERS; i++) {
        snprintf(names[i], sizeof names[i], "w%d", i);
        t[i] = mpt_create(worker, NULL, 1, names[i]);
    }
    mpt_preempt_start(QUANTUM_US);
    for (int i = 0; i < WORKERS; i++) mpt_join(t[i]);
    mpt_preempt_stop();
    printf("  señales del temporizador: %lu | diferidas (llegaron dentro del kernel): %lu\n"
           "  cambios de contexto: %lu | verificaciones de la cola: %lu, todas íntegras\n",
           mpt_stat_signals, mpt_stat_deferred, mpt_stat_switches, checks);
    fflush(stdout);
    _exit(0);
}

static void run(int monitor)
{
    printf("--- monitor monolítico %s ---\n", monitor ? "ACTIVADO" : "DESACTIVADO");
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) child(monitor);

    int status, waited = 0;
    for (int ms = 0; ms < 5000; ms += 10) {
        if (waitpid(pid, &status, WNOHANG) == pid) { waited = 1; break; }
        usleep(10000);
    }
    if (!waited) {
        kill(pid, SIGKILL); waitpid(pid, &status, 0);
        puts("  resultado: el proceso se COLGÓ (hilo perdido o ciclo en la cola); se mató a los 5 s");
    } else if (WIFSIGNALED(status)) {
        printf("  resultado: el proceso ABORTÓ con la señal %d (%s):\n"
               "  el dispatcher conmutó a un contexto inválido (hilo duplicado o perdido en la cola)\n",
               WTERMSIG(status), strsignal(WTERMSIG(status)));
    } else if (WEXITSTATUS(status) == 0) {
        puts("  resultado: terminó correctamente");
    } else if (WEXITSTATUS(status) == 2) {
        puts("  resultado: el dispatcher se quedó sin hilos listos (hilos perdidos de la cola)");
    } else if (WEXITSTATUS(status) == 3) {
        puts("  resultado: CORRUPCIÓN detectada por la verificación de la cola");
    } else {
        printf("  resultado: salió con código %d\n", WEXITSTATUS(status));
    }
}

int main(void)
{
    printf("Demo 1: kernel flag y diferimiento de señales\n"
           "%d hilos x %d yields, SIGALRM cada %d us, ventana de carrera ampliada\n\n",
           WORKERS, ITERS, QUANTUM_US);
    run(1);
    putchar('\n');
    run(0);
    puts("\nCon el monitor, la señal que cae dentro de una operación de la librería se\n"
         "difiere hasta salir del kernel; sin él, el dispatcher corre sobre una cola a\n"
         "medio modificar.");
    return 0;
}
