/* Never run directly: check_diagnostics.py kills/reaps this subprocess. */
#include <assert.h>
#include <pthread.h>
static pthread_mutex_t a = PTHREAD_MUTEX_INITIALIZER, b = PTHREAD_MUTEX_INITIALIZER;
static pthread_barrier_t ready;
static void *worker(void *arg)
{
    pthread_mutex_t *first = arg ? &a : &b, *second = arg ? &b : &a;
    assert(!pthread_mutex_lock(first));
    int rc = pthread_barrier_wait(&ready);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    (void)pthread_mutex_lock(second);
    return NULL;
}
int main(void)
{
    pthread_t t[2];
    assert(!pthread_barrier_init(&ready, NULL, 2));
    assert(!pthread_create(&t[0], NULL, worker, &a));
    assert(!pthread_create(&t[1], NULL, worker, NULL));
    assert(!pthread_join(t[0], NULL));
    assert(!pthread_join(t[1], NULL));
    return 0;
}
