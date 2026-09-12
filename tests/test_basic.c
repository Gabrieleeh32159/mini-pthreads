/* Auto-verificación mínima: creación, prioridades, mutex y condvar. */
#include "mpt.h"
#include <assert.h>
#include <stdio.h>

static char order[16]; static int n;
static mpt_mutex_t m; static mpt_cond_t c; static int flag;

static void hi(void *a)  { (void)a; order[n++] = 'H'; }
static void lo(void *a)  { (void)a; order[n++] = 'L'; }
static void waiter(void *a)
{
    (void)a; mpt_mutex_lock(&m);
    while (!flag) mpt_cond_wait(&c, &m);
    order[n++] = 'W'; mpt_mutex_unlock(&m);
}
static void signaler(void *a)
{
    (void)a; mpt_mutex_lock(&m); flag = 1; order[n++] = 'S';
    mpt_cond_signal(&c); mpt_mutex_unlock(&m);
}

int main(void)
{
    mpt_init(MPT_MAX_PRIO);                  /* main con prioridad máxima: nadie lo desplaza */
    mpt_t *a = mpt_create(lo, 0, 1, "lo"), *b = mpt_create(hi, 0, 3, "hi");
    mpt_yield();                             /* main sigue siendo el de mayor prioridad */
    assert(n == 0);
    mpt_join(b); mpt_join(a);
    assert(order[0] == 'H' && order[1] == 'L');   /* corre primero el de mayor prioridad */

    mpt_mutex_init(&m, MPT_PROTO_NONE, 0); mpt_cond_init(&c);
    mpt_t *w = mpt_create(waiter, 0, 2, "w"), *s = mpt_create(signaler, 0, 1, "s");
    mpt_join(w); mpt_join(s);
    assert(order[2] == 'S' && order[3] == 'W');   /* el waiter despierta tras el signal */
    puts("test_basic: OK");
    return 0;
}
