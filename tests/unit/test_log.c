/* Fault injection affects logger write only, not libc test output. */
#include "rdp/internal.h"
#include "rdp/report.h"
#include "rdp/graph.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static char captured[300000];
static size_t used;
static int fault, calls, blocked, release_writer;
static pthread_mutex_t test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
ssize_t __wrap_write(int fd, const void *buf, size_t len)
{
    assert(fd == STDERR_FILENO);
    /* A graph query here would hang if diagnostics retained graph ownership. */
    (void)rdp_graph_node_count();
    pthread_mutex_lock(&test_mutex);
    ++calls;
    if (fault == 1 && calls == 1) {
        pthread_mutex_unlock(&test_mutex); errno = EINTR; return -1;
    }
    if (fault == 2 || fault == 3) {
        int mode = fault;
        pthread_mutex_unlock(&test_mutex); errno = EIO; return mode == 2 ? -1 : 0;
    }
    if (fault == 4 && !release_writer) {
        blocked = 1;
        pthread_cond_broadcast(&condition);
        while (!release_writer) pthread_cond_wait(&condition, &test_mutex);
    }
    if (fault == 1 && len > 7) len = 7;
    assert(used + len < sizeof captured);
    memcpy(captured + used, buf, len);
    used += len;
    captured[used] = 0;
    pthread_mutex_unlock(&test_mutex);
    return (ssize_t)len;
}
static void *writer(void *unused) { (void)unused; rdp_log("first\n"); return NULL; }
static char tokens[1024];
static void *path[1024];
static void *full_report(void *unused)
{
    (void)unused;
    rdp_report_cycle(42, path[1023], path[0], path, 1024);
    return NULL;
}
int main(void)
{
    errno = EDOM;
    fault = 1;
    rdp_log("partial-write-and-EINTR\n");
    assert(errno == EDOM && !strcmp(captured, "partial-write-and-EINTR\n"));
    fault = 2; rdp_log("EIO\n");
    fault = 3; rdp_log("zero write\n");
    assert(errno == EDOM && rdp_log_errors() == 2);
    fault = 0;


    for (size_t i = 0; i < 1024; ++i) path[i] = &tokens[i];
    used = 0;
    rdp_report_cycle(42, path[1023], path[0], path, 1024);
    assert(strstr(captured,"Thread: tid=42\nHeld Lock:"));
    size_t arrows = 0;
    for (char *s = captured; (s = strstr(s, " -> ")); s += 4) ++arrows;
    assert(arrows == 1024);
    puts("ok - complete maximum path; EINTR, short writes, EIO and zero-write recovery");

    char expected[RDP_LOG_RECORD_CAP];
    size_t record_len = used;
    memcpy(expected, captured, record_len);
    used = 0; fault = 1;
    pthread_t reporters[8];
    for (int i = 0; i < 8; ++i) assert(!pthread_create(&reporters[i], NULL, full_report, NULL));
    for (int i = 0; i < 8; ++i) assert(!pthread_join(reporters[i], NULL));
    assert(used == record_len * 8);
    for (int i = 0; i < 8; ++i) assert(!memcmp(captured + i * record_len, expected, record_len));
    puts("ok - eight concurrent maximum-length records remain contiguous under short writes");

    pthread_t thread;
    used = 0; fault = 4;
    assert(!pthread_create(&thread, NULL, writer, NULL));
    pthread_mutex_lock(&test_mutex);
    while (!blocked) pthread_cond_wait(&condition, &test_mutex);
    pthread_mutex_unlock(&test_mutex);
    for (int i = 0; i < 20; ++i) rdp_log("queued-%d\n", i);
    assert(rdp_log_dropped() == 4);
    pthread_mutex_lock(&test_mutex);
    release_writer = 1;
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&test_mutex);
    assert(!pthread_join(thread, NULL));
    assert(strstr(captured,"4 records dropped (queue capacity 16)"));
    for (int i = 0; i < 16; ++i) {
        char expected[32]; snprintf(expected, sizeof expected, "queued-%d\n", i);
        assert(strstr(captured, expected));
    }
    puts("ok - bounded queue overflow reported; producers proceed while writer blocked");
    puts("TEST log PASS");
    return 0;
}

