/*
 * Demo 2: inversión de prioridades, Figura 5 del paper.
 *
 * P1 (baja) toma el mutex; en t1 quedan listos P2 (media) y P3 (alta). P3
 * intenta tomar el mutex. Se imprime una línea de tiempo por protocolo:
 * sin protocolo, con herencia de prioridad y con techo de prioridad (SRP).
 * No hay temporizador: el tiempo es virtual (un tick por unidad de trabajo),
 * así que la salida es determinista.
 */
#include "mpt.h"
#include <stdio.h>
#include <string.h>

#define P1 0
#define P2 1
#define P3 2
#define MAXT 32

static mpt_mutex_t m;
static mpt_t *th[3];
static char tl[3][MAXT + 1];
static int now, t_ready, t3_try, t3_got, nev;
static char events[16][80];

static void ev(const char *s) { snprintf(events[nev++], 80, "t=%-2d %s", now, s); }

static void work(int who, int ticks)
{
    for (int i = 0; i < ticks && now < MAXT; i++) {
        tl[who][now] = (m.owner == th[who]) ? 'M' : '#';
        now++;
    }
}

static void p1(void *a)
{
    (void)a;
    mpt_mutex_lock(&m); ev("P1 toma el mutex");
    work(P1, 1);
    ev("t1: P2 y P3 pasan a listos"); t_ready = now;
    mpt_resume(th[P3]);
    mpt_resume(th[P2]);
    work(P1, 3);
    ev("P1 libera el mutex"); mpt_mutex_unlock(&m);
    work(P1, 1);
}

static void p2(void *a) { (void)a; mpt_suspend(); work(P2, 3); }

static void p3(void *a)
{
    (void)a;
    mpt_suspend();
    t3_try = now; ev("P3 intenta tomar el mutex");
    mpt_mutex_lock(&m);
    t3_got = now; ev("P3 obtiene el mutex");
    work(P3, 2);
    mpt_mutex_unlock(&m);
    work(P3, 1);
}

static void run(const char *title, mpt_proto_t proto)
{
    memset(tl, '.', sizeof tl);
    now = nev = 0; t3_try = t3_got = -1;
    mpt_init(MPT_MAX_PRIO);        /* main no es desplazado: los hilos arrancan en el join */
    mpt_mutex_init(&m, proto, 3);
    th[P3] = mpt_create(p3, NULL, 3, "P3");
    th[P2] = mpt_create(p2, NULL, 2, "P2");
    th[P1] = mpt_create(p1, NULL, 1, "P1");
    for (int i = 0; i < 3; i++) mpt_join(th[i]);

    printf("=== %s ===\n", title);
    for (int i = 0; i < nev; i++) printf("  %s\n", events[i]);
    printf("\n  tick     ");
    for (int t = 0; t < now; t++) putchar('0' + t % 10);
    putchar('\n');
    for (int who = P3; who >= P1; who--) {
        tl[who][now] = 0;
        if (who == P3 && t3_try >= 0)
            for (int t = t3_try; t < t3_got; t++) if (tl[who][t] == '.') tl[who][t] = 'w';
        printf("  P%d (prio %d) %s\n", who + 1, who + 1, tl[who]);
    }
    int blocked = (t3_got >= 0 && t3_try >= 0) ? t3_got - t3_try : 0;
    printf("\n  P3 listo en t=%d, obtiene el mutex en t=%d: espera %d ticks, "
           "bloqueado en el mutex %d ticks\n  cambios de contexto: %lu\n\n",
           t_ready, t3_got, t3_got - t_ready, blocked, mpt_stat_switches);
}

int main(void)
{
    puts("Demo 2: inversión de prioridades (Figura 5 del paper)\n"
         "  # ejecuta   M ejecuta con el mutex   w bloqueado esperando el mutex\n");
    run("(a) sin protocolo", MPT_PROTO_NONE);
    run("(b) herencia de prioridad", MPT_PROTO_INHERIT);
    run("(c) techo de prioridad (SRP, techo = 3)", MPT_PROTO_CEILING);
    return 0;
}
