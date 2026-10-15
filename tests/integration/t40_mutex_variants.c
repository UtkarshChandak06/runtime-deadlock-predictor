/* Distinguish tested partial support from unsupported synchronization semantics. */
#include "tracker_probe.h"
#include <errno.h>
#include <pthread.h>
static pthread_mutex_t recursive, priority, robust;
static void *owner_dies(void *unused)
{
    (void)unused;
    TEST_CHECK(!pthread_mutex_lock(&robust), "robust initial ownership");
    return NULL; /* deliberate owner death, not a deadlock */
}
int main(void)
{
    tracker_probe p;
    tracker_probe_load(&p);
    pthread_mutexattr_t attr;
    TEST_CHECK(!pthread_mutexattr_init(&attr), "attributes");
    TEST_CHECK(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE), "recursive attributes");
    TEST_CHECK(!pthread_mutex_init(&recursive, &attr), "recursive init");
    for (int i = 1; i <= 3; ++i) {
        errno = EDOM;
        TEST_CHECK(!pthread_mutex_lock(&recursive), "recursive lock");
        TEST_CHECK(errno == EDOM, "recursive errno");
        if (p.available) TEST_CHECK(p.count() == 1 && p.depth(&recursive) == (size_t)i, "native recursion depth");
    }
    for (int i = 2; i >= 0; --i) {
        TEST_CHECK(!pthread_mutex_unlock(&recursive), "recursive unlock");
        if (p.available) TEST_CHECK(p.depth(&recursive) == (size_t)i, "recursive release depth");
    }
    TEST_CHECK(!pthread_mutex_destroy(&recursive), "recursive destroy");
    puts("PARTIAL recursive: native depth/errno passed; self-cycle warning remains conservative");

    TEST_CHECK(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL), "normal type");
    int rc = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    if (rc == ENOTSUP) puts("UNSUPPORTED priority-inheritance: host rejected attribute");
    else {
        TEST_CHECK(rc == 0, "PI attribute");
        TEST_CHECK(!pthread_mutex_init(&priority, &attr), "PI init");
        TEST_CHECK(!pthread_mutex_lock(&priority), "PI basic lock");
        if (p.available) TEST_CHECK(p.holds(&priority), "PI basic tracker");
        TEST_CHECK(!pthread_mutex_unlock(&priority), "PI basic unlock");
        TEST_CHECK(!pthread_mutex_destroy(&priority), "PI destroy");
        puts("PARTIAL priority-inheritance: ordinary lock/unlock tested; priority scheduling not tested");
    }
    TEST_CHECK(!pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_NONE), "reset protocol");
    rc = pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    if (rc == ENOTSUP) puts("UNSUPPORTED robust owner-death: host rejected attribute");
    else {
        TEST_CHECK(!rc, "robust attribute");
        TEST_CHECK(!pthread_mutex_init(&robust, &attr), "robust init");
        pthread_t thread;
        TEST_CHECK(!pthread_create(&thread, NULL, owner_dies, NULL), "owner thread");
        TEST_CHECK(!pthread_join(thread, NULL), "owner exits");
        TEST_CHECK(pthread_mutex_lock(&robust) == EOWNERDEAD, "ownership returned with EOWNERDEAD");
        /* This assertion documents a known blind spot, not supported tracking. */
        if (p.available) TEST_CHECK(!p.holds(&robust), "unsupported EOWNERDEAD acquisition remains untracked");
        TEST_CHECK(!pthread_mutex_consistent(&robust), "recover native robust mutex");
        TEST_CHECK(!pthread_mutex_unlock(&robust), "robust unlock");
        TEST_CHECK(!pthread_mutex_destroy(&robust), "robust destroy");
        puts("UNSUPPORTED robust owner-death: EOWNERDEAD forwarded, acquired ownership absent from tracker");
    }
    TEST_CHECK(!pthread_mutexattr_destroy(&attr), "attributes destroy");
    if (p.available) TEST_CHECK(!p.count(), "empty tracker after variant tests");
    puts("TEST t40 PASS");
    return 0;
}
