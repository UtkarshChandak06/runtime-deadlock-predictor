#define _XOPEN_SOURCE 700
#include "banking.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A failed synchronization primitive leaves ownership/lifetime uncertain.
 * Stop the process instead of continuing with possibly unprotected balances. */
void pthread_check(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

int account_init(Account *account, int id, int64_t cents)
{
    if (account == NULL || id <= 0 || cents < 0) return EINVAL;
    account->id = id;
    account->cents = cents;
    return pthread_mutex_init(&account->mutex, NULL);
}

/* Accounts must be initialized, have distinct immutable IDs, and remain alive.
 * All balance reads/writes during concurrent execution require their mutexes. */
int transfer(Account *from, Account *to, int64_t cents, int ordered, int worker)
{
    if (from == NULL || to == NULL || from == to || cents <= 0 ||
        from->id <= 0 || to->id <= 0 || from->id == to->id ||
        (ordered != 0 && ordered != 1)) return EINVAL;
    Account *first = from;
    Account *second = to;
    if (ordered && first->id > second->id) {
        first = to;
        second = from;
    }
    int error = pthread_mutex_lock(&first->mutex);
    if (error != 0) return error;
    printf("ACQUIRED worker=%d account=%d\n", worker, first->id);
    error = pthread_mutex_lock(&second->mutex);
    if (error != 0) {
        pthread_check(pthread_mutex_unlock(&first->mutex), "unlock first");
        return error;
    }
    printf("ACQUIRED worker=%d account=%d\n", worker, second->id);

    /* Validate and update together while both account locks are held. */
    if (from->cents < 0 || to->cents < 0) error = EINVAL;
    else if (from->cents < cents) error = ENOSPC;
    else if (to->cents > INT64_MAX - cents) error = EOVERFLOW;
    else {
        from->cents -= cents;
        to->cents += cents;
        printf("TRANSFER worker=%d from=%d to=%d cents=%" PRId64 "\n",
               worker, from->id, to->id, cents);
    }
    pthread_check(pthread_mutex_unlock(&second->mutex), "unlock second");
    printf("RELEASED worker=%d account=%d\n", worker, second->id);
    pthread_check(pthread_mutex_unlock(&first->mutex), "unlock first");
    printf("RELEASED worker=%d account=%d\n", worker, first->id);
    return error;
}

typedef struct {
    int id;
    Account *from;
    Account *to;
    int64_t amount;
    enum Scenario scenario;
    pthread_barrier_t *barrier;
    int result;
} Work;

static void *worker(void *argument)
{
    Work *work = argument;
    if (work->scenario != DEADLOCK) {
        work->result = transfer(work->from, work->to, work->amount,
                                work->scenario == SAFE, work->id);
        return NULL;
    }
    pthread_check(pthread_mutex_lock(&work->from->mutex), "lock first");
    printf("HELD worker=%d account=%d\n", work->id, work->from->id);
    /* The barrier has no application-level account lock of its own. Each
     * worker deliberately keeps its first lock across this rendezvous. */
    const int error = pthread_barrier_wait(work->barrier);
    if (error != PTHREAD_BARRIER_SERIAL_THREAD) pthread_check(error, "barrier wait");
    printf("REQUEST worker=%d held=%d wanted=%d\n",
           work->id, work->from->id, work->to->id);
    pthread_check(pthread_mutex_lock(&work->to->mutex), "lock second");
    /* Unreachable in the intended fixture; never print a fake completion. */
    puts("UNEXPECTED: second lock acquired");
    pthread_check(pthread_mutex_unlock(&work->to->mutex), "unlock second");
    pthread_check(pthread_mutex_unlock(&work->from->mutex), "unlock first");
    work->result = EPROTO;
    return NULL;
}

int banking_run(enum Scenario scenario)
{
    if (scenario < SAFE || scenario > DEADLOCK) return EXIT_FAILURE;
    /* Evidence must reach the parent even when stdout is a pipe. */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0) return EXIT_FAILURE;
    Account accounts[2];
    pthread_check(account_init(&accounts[0], 1, 100000), "init account 1");
    int error = account_init(&accounts[1], 2, 100000);
    if (error != 0) {
        pthread_check(pthread_mutex_destroy(&accounts[0].mutex), "destroy account 1");
        pthread_check(error, "init account 2");
    }
    const int64_t before = accounts[0].cents + accounts[1].cents;
    printf("INITIAL account1=%" PRId64 " account2=%" PRId64 " total=%" PRId64 "\n",
           accounts[0].cents, accounts[1].cents, before);
    puts(scenario == SAFE ? "MODE safe: ascending account IDs" :
         scenario == UNSAFE ? "MODE unsafe: opposite orders, workers joined sequentially" :
         "MODE controlled-deadlock: parent harness must terminate this process");
    pthread_barrier_t barrier;
    if (scenario == DEADLOCK)
        pthread_check(pthread_barrier_init(&barrier, NULL, 2), "barrier init");
    Work work[2] = {
        {1, &accounts[0], &accounts[1], 10000, scenario, &barrier, 0},
        {2, &accounts[1], &accounts[0], 15000, scenario, &barrier, 0}
    };
    pthread_t threads[2];
    for (int i = 0; i < 2; ++i) {
        error = pthread_create(&threads[i], NULL, worker, &work[i]);
        if (error != 0 && i == 1 && scenario == SAFE)
            pthread_check(pthread_join(threads[0], NULL), "join after create failure");
        /* For a partial DEADLOCK start, fail-fast process exit releases the
         * waiting worker; joining it would hang at the two-party barrier. */
        pthread_check(error, "pthread_create");
        if (scenario == UNSAFE)
            pthread_check(pthread_join(threads[i], NULL), "sequential join");
    }
    if (scenario != UNSAFE) {
        for (int i = 0; i < 2; ++i)
            pthread_check(pthread_join(threads[i], NULL), "pthread_join");
    }
    /* No workers remain, so final balances can be read without locking. */
    int valid = work[0].result == 0 && work[1].result == 0 &&
                accounts[0].cents >= 0 && accounts[1].cents >= 0 &&
                accounts[0].cents <= INT64_MAX - accounts[1].cents;
    int64_t after = valid ? accounts[0].cents + accounts[1].cents : -1;
    valid = valid && after == before;
    for (int i = 0; i < 2; ++i) {
        if (work[i].result != 0)
            fprintf(stderr, "worker %d transfer: %s\n", i + 1, strerror(work[i].result));
        pthread_check(pthread_mutex_destroy(&accounts[i].mutex), "mutex destroy");
    }
    if (scenario == DEADLOCK)
        pthread_check(pthread_barrier_destroy(&barrier), "barrier destroy");
    printf("FINAL account1=%" PRId64 " account2=%" PRId64 " total=%" PRId64 "\n",
           accounts[0].cents, accounts[1].cents, after);
    printf("VALIDATION %s\n", valid ? "PASS" : "FAIL");
    if (scenario == UNSAFE && valid)
        puts("Both transactions completed without an actual deadlock; lock ordering is unsafe.");
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
}
