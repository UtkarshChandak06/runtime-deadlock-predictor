/* Same binary in both modes; Linux/POSIX, deliberately separate from tests.
 * CLOCK_MONOTONIC excludes startup, probes, creation/join and final validation.
 * For >1 workers it includes release/start and completion barriers consistently.
 * One worker executes on main (no hidden helper thread or threaded libc mode).
 */
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_THREADS 8
#define MAX_ITERATIONS UINT64_C(100000000)

typedef size_t (*count_fn)(void);
typedef int (*holds_fn)(void *);
struct monitor_probe {
    count_fn tracker_count, tracker_dropped, graph_nodes, graph_edges;
    holds_fn tracker_holds;
    count_fn graph_dropped, log_dropped, log_errors;
    int loaded;
    const char *library;
};
static struct monitor_probe monitor;

/* Alignment makes sizeof(slot) a multiple of 64 as well. Each independent
 * mutex AND its protected counter occupy a separate cache-line-aligned slot.
 * 64 bytes is a stated layout choice, not a claim about every machine's line.
 */
struct slot {
    _Alignas(64) pthread_mutex_t mutex;
    uint64_t value;
};
static struct slot slots[MAX_THREADS];
static pthread_barrier_t ready_barrier, start_barrier, done_barrier;
enum workload { SINGLE, INDEPENDENT, SHARED, ORDERED };
struct worker {
    _Alignas(64) unsigned index;
    enum workload workload;
    uint64_t iterations, completed, operations;
    size_t tracker_held, tracker_dropped;
};

static void fail(const char *what, int rc)
{
    fprintf(stderr, "benchmark failure: %s (code %d)\n", what, rc);
    exit(EXIT_FAILURE);
}
static void checked(int rc, const char *what)
{
    if (rc != 0) fail(what, rc);
}
static void barrier(pthread_barrier_t *b)
{
    int rc = pthread_barrier_wait(b);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD)
        fail("pthread_barrier_wait", rc);
}
static uint64_t number(const char *text, uint64_t limit)
{
    char *end;
    if (!*text) fail("empty numeric argument", 0);
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9') fail("invalid numeric argument", 0);
    errno = 0;
    unsigned long long n = strtoull(text, &end, 10);
    if (errno || *end || !n || n > limit)
        fail("numeric argument outside supported bounds", errno);
    return (uint64_t)n;
}
static void probe_monitor(int expected)
{
    monitor.tracker_holds = (holds_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_holds");
    monitor.tracker_count = (count_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_count");
    monitor.tracker_dropped = (count_fn)dlsym(RTLD_DEFAULT, "rdp_tracker_dropped");
    monitor.graph_nodes = (count_fn)dlsym(RTLD_DEFAULT, "rdp_graph_node_count");
    monitor.graph_edges = (count_fn)dlsym(RTLD_DEFAULT, "rdp_graph_edge_count");
    monitor.graph_dropped = (count_fn)dlsym(RTLD_DEFAULT, "rdp_graph_dropped");
    monitor.log_dropped = (count_fn)dlsym(RTLD_DEFAULT, "rdp_log_dropped");
    monitor.log_errors = (count_fn)dlsym(RTLD_DEFAULT, "rdp_log_errors");
    int found = (monitor.tracker_holds != NULL) + (monitor.tracker_count != NULL) + (monitor.tracker_dropped != NULL)
        + (monitor.graph_nodes != NULL) + (monitor.graph_edges != NULL)
        + (monitor.graph_dropped != NULL) + (monitor.log_dropped != NULL)
        + (monitor.log_errors != NULL);
    if (found != (expected ? 8 : 0)) fail("unexpected monitor presence/symbols", found);
    monitor.loaded = expected;
    monitor.library = "";
    if (expected) {
        Dl_info info;
        if (!dladdr((void *)monitor.tracker_count, &info) || !info.dli_fname)
            fail("cannot identify monitor library", 0);
        monitor.library = info.dli_fname;
        Dl_info wrapper;
        if (!dladdr(dlsym(RTLD_DEFAULT, "pthread_mutex_lock"), &wrapper) ||
            wrapper.dli_fbase != info.dli_fbase)
            fail("lock wrapper and monitor probes resolve to different libraries", 0);
        if (monitor.tracker_count() || monitor.tracker_dropped() ||
            monitor.graph_nodes() || monitor.graph_edges() || monitor.graph_dropped() ||
            monitor.log_dropped() || monitor.log_errors())
            fail("nonempty monitor state before workload", 0);
    }
}
static void work(struct worker *w)
{
    struct slot *first = &slots[w->workload == INDEPENDENT ? w->index : 0];
    for (uint64_t i = 0; i < w->iterations; ++i) {
        checked(pthread_mutex_lock(&first->mutex), "lock first");
        ++w->operations;
        if (w->workload == ORDERED) {
            checked(pthread_mutex_lock(&slots[1].mutex), "lock second");
            ++w->operations;
            checked(pthread_mutex_lock(&slots[2].mutex), "lock third");
            ++w->operations;
        }
        ++first->value;
        if (w->workload == ORDERED) {
            checked(pthread_mutex_unlock(&slots[2].mutex), "unlock third");
            ++w->operations;
            checked(pthread_mutex_unlock(&slots[1].mutex), "unlock second");
            ++w->operations;
        }
        checked(pthread_mutex_unlock(&first->mutex), "unlock first");
        ++w->operations;
        ++w->completed;
    }
}
static void inspect_worker(struct worker *w)
{
    if (monitor.loaded) {
        w->tracker_held = monitor.tracker_count();
        w->tracker_dropped = monitor.tracker_dropped();
        if (w->tracker_held || w->tracker_dropped)
            fail("worker tracker is incomplete/nonempty after workload", 0);
    }
}
static void *thread_main(void *arg)
{
    struct worker *w = arg;
    barrier(&ready_barrier);
    barrier(&start_barrier);
    work(w);
    barrier(&done_barrier);
    /* TLS probes are outside the timed interval and queried before exit. */
    inspect_worker(w);
    return NULL;
}
static void clock_now(struct timespec *time)
{
    if (clock_gettime(CLOCK_MONOTONIC, time)) fail("clock_gettime", errno);
}
static void json_string(const char *s)
{
    putchar('"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 32) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}
int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s single|independent|shared|ordered 1|2|4|8 iterations baseline|monitored\n", argv[0]);
        return EXIT_FAILURE;
    }
    enum workload kind;
    if (!strcmp(argv[1], "single")) kind = SINGLE;
    else if (!strcmp(argv[1], "independent")) kind = INDEPENDENT;
    else if (!strcmp(argv[1], "shared")) kind = SHARED;
    else if (!strcmp(argv[1], "ordered")) kind = ORDERED;
    else { fail("unknown workload", 0); return EXIT_FAILURE; }
    unsigned threads = (unsigned)number(argv[2], MAX_THREADS);
    if ((threads != 1 && threads != 2 && threads != 4 && threads != 8) ||
        (kind == SINGLE && threads != 1)) fail("unsupported thread count", 0);
    uint64_t iterations = number(argv[3], MAX_ITERATIONS);
    int expected;
    if (!strcmp(argv[4], "baseline")) expected = 0;
    else if (!strcmp(argv[4], "monitored")) expected = 1;
    else { fail("unknown mode", 0); return EXIT_FAILURE; }
    probe_monitor(expected);
    unsigned mutexes = kind == ORDERED ? 3 : kind == INDEPENDENT ? threads : 1;
    for (unsigned i = 0; i < mutexes; ++i)
        checked(pthread_mutex_init(&slots[i].mutex, NULL), "pthread_mutex_init");
    /* One untimed pair in BOTH modes verifies actual interception and warms
     * the initial tracker/resolver path. It creates no dependency edges. */
    checked(pthread_mutex_lock(&slots[0].mutex), "probe lock");
    if (monitor.loaded && (monitor.tracker_count() != 1 ||
        !monitor.tracker_holds(&slots[0].mutex)))
        fail("loaded monitor did not observe successful probe lock", 0);
    checked(pthread_mutex_unlock(&slots[0].mutex), "probe unlock");
    if (monitor.loaded && monitor.tracker_count())
        fail("probe unlock left tracker nonempty", 0);
    struct worker workers[MAX_THREADS] = {0};
    pthread_t ids[MAX_THREADS];
    for (unsigned i = 0; i < threads; ++i) {
        workers[i].index = i;
        workers[i].workload = kind;
        workers[i].iterations = iterations;
    }
    struct timespec begin, end;
    if (threads == 1) {
        clock_now(&begin);
        work(&workers[0]);
        clock_now(&end);
        inspect_worker(&workers[0]);
    } else {
        checked(pthread_barrier_init(&ready_barrier, NULL, threads + 1), "ready init");
        checked(pthread_barrier_init(&start_barrier, NULL, threads + 1), "start init");
        checked(pthread_barrier_init(&done_barrier, NULL, threads + 1), "done init");
        for (unsigned i = 0; i < threads; ++i)
            checked(pthread_create(&ids[i], NULL, thread_main, &workers[i]), "pthread_create");
        barrier(&ready_barrier);
        clock_now(&begin);
        barrier(&start_barrier);
        barrier(&done_barrier);
        clock_now(&end);
        for (unsigned i = 0; i < threads; ++i)
            checked(pthread_join(ids[i], NULL), "pthread_join");
        checked(pthread_barrier_destroy(&ready_barrier), "ready destroy");
        checked(pthread_barrier_destroy(&start_barrier), "start destroy");
        checked(pthread_barrier_destroy(&done_barrier), "done destroy");
    }
    uint64_t completed = 0, operations = 0, values = 0;
    size_t held = 0, dropped = 0;
    for (unsigned i = 0; i < threads; ++i) {
        if (workers[i].completed != iterations ||
            workers[i].operations != iterations * (kind == ORDERED ? 6 : 2))
            fail("incorrect per-thread operation count", 0);
        completed += workers[i].completed;
        operations += workers[i].operations;
        held += workers[i].tracker_held;
        dropped += workers[i].tracker_dropped;
    }
    for (unsigned i = 0; i < mutexes; ++i) {
        values += slots[i].value;
        checked(pthread_mutex_destroy(&slots[i].mutex), "pthread_mutex_destroy");
    }
    if (values != iterations * threads) fail("incorrect protected counter", 0);
    size_t nodes = 0, edges = 0, graph_dropped = 0, log_dropped = 0, log_errors = 0;
    if (monitor.loaded) {
        nodes = monitor.graph_nodes();
        edges = monitor.graph_edges();
        graph_dropped = monitor.graph_dropped();
        log_dropped = monitor.log_dropped();
        log_errors = monitor.log_errors();
        held += monitor.tracker_count();
        dropped += monitor.tracker_dropped();
        size_t expect_graph = kind == ORDERED ? 3 : 0;
        if (nodes != expect_graph || edges != expect_graph || graph_dropped ||
            held || dropped || log_dropped || log_errors)
            fail("unexpected monitor final graph/tracker/log state", 0);
    }
    double seconds = (double)(end.tv_sec - begin.tv_sec) +
                     (double)(end.tv_nsec - begin.tv_nsec) / 1000000000.0;
    if (seconds <= 0.0) fail("nonpositive elapsed time", 0);
    printf("{\"benchmark\":\"%s\",\"threads\":%u,\"iterations\":%" PRIu64
           ",\"elapsed_seconds\":%.9f,\"operations\":%" PRIu64
           ",\"completed_iterations\":%" PRIu64 ",\"counter_value\":%" PRIu64
           ",\"monitor_loaded\":%d,\"monitor_library\":", argv[1], threads,
           iterations, seconds, operations, completed, values, monitor.loaded);
    json_string(monitor.library);
    printf(",\"graph_nodes\":%zu,\"graph_edges\":%zu,\"graph_dropped\":%zu,"
           "\"tracker_held\":%zu,\"tracker_dropped\":%zu,\"log_dropped\":%zu,"
           "\"log_errors\":%zu}\n", nodes, edges, graph_dropped, held, dropped,
           log_dropped, log_errors);
    if (fflush(stdout) || ferror(stdout)) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
