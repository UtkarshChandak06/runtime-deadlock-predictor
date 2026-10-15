/* Exercise cancellation at a real blocked write and subsequent logger recovery. */
#include "rdp/internal.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static pthread_mutex_t ready_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready_cond = PTHREAD_COND_INITIALIZER;
static int entered;
ssize_t __real_write(int fd, const void *buf, size_t len);
ssize_t __wrap_write(int fd, const void *buf, size_t len)
{
    return __real_write(fd, buf, len);
}
static void *writer(void *unused)
{
    (void)unused;
    /* Signal before entering the logger.  ThreadSanitizer may intercept write()
     * ahead of the linker's --wrap hook, so using the wrapper as the rendezvous
     * can leave the test waiting forever even though the logger is blocked. */
    pthread_mutex_lock(&ready_mutex);
    entered = 1;
    pthread_cond_signal(&ready_cond);
    pthread_mutex_unlock(&ready_mutex);
    rdp_log("cancelled-record\n");
    return NULL;
}
int main(void)
{
    int descriptors[2];
    assert(!pipe(descriptors));
    int flags = fcntl(descriptors[1], F_GETFL);
    assert(flags >= 0 && !fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK));
    char bytes[4096]; memset(bytes, 'x', sizeof bytes);
    while (__real_write(descriptors[1], bytes, sizeof bytes) > 0) {}
    assert(errno == EAGAIN);
    assert(!fcntl(descriptors[1], F_SETFL, flags));
    int saved_stderr = dup(STDERR_FILENO);
    assert(saved_stderr >= 0 && dup2(descriptors[1], STDERR_FILENO) == STDERR_FILENO);
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, writer, NULL));
    pthread_mutex_lock(&ready_mutex);
    while (!entered) pthread_cond_wait(&ready_cond, &ready_mutex);
    pthread_mutex_unlock(&ready_mutex);
    rdp_log("queued-before-cancel\n");
    assert(!pthread_cancel(thread));
    void *result;
    assert(!pthread_join(thread, &result) && result == PTHREAD_CANCELED);
    assert(rdp_log_errors() == 1);
    FILE *capture = tmpfile();
    assert(capture && dup2(fileno(capture), STDERR_FILENO) == STDERR_FILENO);
    rdp_log("writer-recovered\n");
    assert(!fflush(capture));
    rewind(capture);
    char actual[128] = {0};
    assert(fread(actual, 1, sizeof actual - 1, capture) > 0);
    assert(!strcmp(actual, "queued-before-cancel\nwriter-recovered\n"));
    assert(dup2(saved_stderr, STDERR_FILENO) == STDERR_FILENO);
    close(saved_stderr); close(descriptors[0]); close(descriptors[1]); fclose(capture);
    puts("TEST log_cancel PASS: cancelled writer releases ownership; next producer drains queued records");
    return 0;
}
