#include "banking.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return EXIT_FAILURE; \
} } while (0)

int main(void)
{
    Account a, b;
    CHECK(account_init(NULL, 1, 0) == EINVAL);
    CHECK(account_init(&a, 0, 0) == EINVAL);
    CHECK(account_init(&a, 1, -1) == EINVAL);
    CHECK(account_init(&a, 1, 100) == 0);
    CHECK(account_init(&b, 2, 200) == 0);
    CHECK(transfer(NULL, &b, 1, 1, 1) == EINVAL);
    CHECK(transfer(&a, &a, 1, 1, 1) == EINVAL);
    CHECK(transfer(&a, &b, 0, 1, 1) == EINVAL);
    CHECK(transfer(&a, &b, -1, 1, 1) == EINVAL);
    CHECK(transfer(&a, &b, 1, 2, 1) == EINVAL);
    b.id = 1;
    CHECK(transfer(&a, &b, 1, 1, 1) == EINVAL);
    b.id = 2;
    CHECK(transfer(&a, &b, 101, 1, 1) == ENOSPC);
    CHECK(a.cents == 100 && b.cents == 200);
    b.cents = INT64_MAX;
    CHECK(transfer(&a, &b, 1, 1, 1) == EOVERFLOW);
    CHECK(a.cents == 100 && b.cents == INT64_MAX);
    b.cents = INT64_MAX - 100;
    CHECK(transfer(&a, &b, 100, 1, 1) == 0);
    CHECK(a.cents == 0 && b.cents == INT64_MAX);
    /* A later transfer succeeding also checks rejection paths released locks. */
    CHECK(transfer(&b, &a, 100, 1, 2) == 0);
    CHECK(a.cents == 100 && b.cents == INT64_MAX - 100);
    pthread_check(pthread_mutex_destroy(&a.mutex), "destroy a");
    pthread_check(pthread_mutex_destroy(&b.mutex), "destroy b");
    puts("PASS: invalid inputs, insufficient funds, overflow, exact limits, lock release");
    return EXIT_SUCCESS;
}
