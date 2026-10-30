#define _XOPEN_SOURCE 700
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build twice: concurrent globally ordered transfers, and sequential ring
 * ordering that deliberately demonstrates a SIX-lock historical cycle. */
#ifndef NETWORK_ORDERED
#define NETWORK_ORDERED 1
#endif
#define ACCOUNTS 6
#define ROUNDS 3

typedef struct {
    pthread_mutex_t mutex;
    int id;
    long balance;
} Account;

typedef struct {
    Account *from, *to;
    int id;
    pthread_barrier_t *start;
} Transfer;

static void check(int result, const char *operation)
{
    if (result) {
        fprintf(stderr, "%s: %s\n", operation, strerror(result));
        exit(EXIT_FAILURE);
    }
}

static void *worker(void *argument)
{
    Transfer *work = argument;
    Account *first = work->from, *second = work->to;
    if (NETWORK_ORDERED && first->id > second->id) {
        first = work->to;
        second = work->from;
    }
    if (NETWORK_ORDERED) {
        int result = pthread_barrier_wait(work->start);
        if (result != PTHREAD_BARRIER_SERIAL_THREAD) check(result, "start barrier");
    }
    for (int round = 0; round < ROUNDS; ++round) {
        check(pthread_mutex_lock(&first->mutex), "lock first");
        check(pthread_mutex_lock(&second->mutex), "lock second");
        /* Both balances are protected. The workload uses bounded fixed values. */
        if (work->from->balance < 100) {
            fputs("Insufficient balance\n", stderr);
            exit(EXIT_FAILURE);
        }
        work->from->balance -= 100;
        work->to->balance += 100;
        printf("TRANSFER worker=%d round=%d from=%d to=%d cents=100 order=%d->%d\n",
               work->id, round + 1, work->from->id, work->to->id,
               first->id, second->id);
        check(pthread_mutex_unlock(&second->mutex), "unlock second");
        check(pthread_mutex_unlock(&first->mutex), "unlock first");
    }
    return NULL;
}

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { fputs("No arguments accepted\n", stderr); return EXIT_FAILURE; }
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return EXIT_FAILURE;
    Account accounts[ACCOUNTS];
    Transfer transfers[ACCOUNTS];
    pthread_t threads[ACCOUNTS];
    pthread_barrier_t start;
    if (NETWORK_ORDERED) check(pthread_barrier_init(&start, NULL, ACCOUNTS), "barrier init");
    printf("NETWORK accounts=6 workers=6 rounds=3 mode=%s\n",
           NETWORK_ORDERED ? "concurrent ordered" : "sequential historical cycle");
    for (int i = 0; i < ACCOUNTS; ++i) {
        accounts[i].id = i + 1;
        accounts[i].balance = 100000;
        check(pthread_mutex_init(&accounts[i].mutex, NULL), "mutex init");
    }
    for (int i = 0; i < ACCOUNTS; ++i) {
        transfers[i] = (Transfer){&accounts[i], &accounts[(i + 1) % ACCOUNTS], i + 1, &start};
        check(pthread_create(&threads[i], NULL, worker, &transfers[i]), "create worker");
        /* Do not remove this join in the risk fixture: it keeps the example
         * bounded while retaining opposite ordering across six lock lifetimes. */
        if (!NETWORK_ORDERED) check(pthread_join(threads[i], NULL), "sequential join");
    }
    if (NETWORK_ORDERED)
        for (int i = 0; i < ACCOUNTS; ++i) check(pthread_join(threads[i], NULL), "join worker");
    long total = 0;
    int valid = 1;
    for (int i = 0; i < ACCOUNTS; ++i) {
        printf("FINAL account=%d cents=%ld\n", accounts[i].id, accounts[i].balance);
        total += accounts[i].balance;
        valid &= accounts[i].balance == 100000;
        check(pthread_mutex_destroy(&accounts[i].mutex), "mutex destroy");
    }
    if (NETWORK_ORDERED) check(pthread_barrier_destroy(&start), "barrier destroy");
    valid &= total == 600000;
    printf("TOTAL cents=%ld transfers=%d VALIDATION %s\n", total, ACCOUNTS * ROUNDS, valid ? "PASS" : "FAIL");
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
