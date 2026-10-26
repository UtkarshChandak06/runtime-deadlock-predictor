#define _XOPEN_SOURCE 700
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Real identical workload in all modes. There is no monitor-dependent branch. */
static pthread_mutex_t mutexes[8];
static pthread_barrier_t start;
static uint64_t counters[8];
static unsigned threads, iterations;
static const char *workload;
static void check(int rc) { if (rc) { fprintf(stderr,"pthread error %d\n",rc); exit(2); } }
static uint64_t clock_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) { perror("clock_gettime"); exit(2); }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static void *worker(void *arg) {
    unsigned id = *(unsigned *)arg;
    int result = pthread_barrier_wait(&start);
    if (result != PTHREAD_BARRIER_SERIAL_THREAD) check(result);
    unsigned slot = strcmp(workload,"independent") == 0 ? id : 0;
    int nested = strcmp(workload,"nested") == 0;
    for (unsigned i=0; i<iterations; ++i) {
        check(pthread_mutex_lock(&mutexes[slot]));
        if (nested) check(pthread_mutex_lock(&mutexes[1]));
        ++counters[slot];
        if (nested) check(pthread_mutex_unlock(&mutexes[1]));
        check(pthread_mutex_unlock(&mutexes[slot]));
    }
    return NULL;
}
static unsigned number(const char *value, unsigned maximum) {
    char *end; errno=0;
    unsigned long n=strtoul(value,&end,10);
    if (errno || !*value || *end || n<1 || n>maximum) exit(2);
    return (unsigned)n;
}
int main(int argc, char **argv) {
    if (argc!=4) return 2;
    workload=argv[1]; threads=number(argv[2],8); iterations=number(argv[3],10000000);
    if (strcmp(workload,"independent") && strcmp(workload,"shared") && strcmp(workload,"nested")) return 2;
    unsigned locks=strcmp(workload,"nested")==0 ? 2 : strcmp(workload,"shared")==0 ? 1 : threads;
    for (unsigned i=0;i<locks;++i) check(pthread_mutex_init(&mutexes[i],NULL));
    check(pthread_barrier_init(&start,NULL,threads+1));
    pthread_t handles[8]; unsigned ids[8];
    for (unsigned i=0;i<threads;++i) { ids[i]=i; check(pthread_create(&handles[i],NULL,worker,&ids[i])); }
    uint64_t begin=clock_ns();
    int result=pthread_barrier_wait(&start);
    if(result!=PTHREAD_BARRIER_SERIAL_THREAD) check(result);
    for(unsigned i=0;i<threads;++i) check(pthread_join(handles[i],NULL));
    uint64_t elapsed=clock_ns()-begin, count=0;
    for(unsigned i=0;i<threads;++i) count+=counters[i];
    uint64_t expected=(uint64_t)threads*iterations;
    if(count!=expected) { fprintf(stderr,"counter mismatch\n"); return 3; }
    for(unsigned i=0;i<locks;++i) check(pthread_mutex_destroy(&mutexes[i]));
    check(pthread_barrier_destroy(&start));
    printf("{\"workload\":\"%s\",\"threads\":%u,\"iterations\":%u,\"elapsed_ns\":%" PRIu64
           ",\"operations\":%" PRIu64 ",\"lock_acquisitions\":%" PRIu64 ",\"correct\":true}\n",
           workload,threads,iterations,elapsed,count,expected*(strcmp(workload,"nested")==0?2:1));
    return 0;
}
