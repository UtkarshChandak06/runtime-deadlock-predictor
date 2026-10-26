#ifndef BANKING_H
#define BANKING_H
#include <pthread.h>
#include <stdint.h>

typedef struct {
    int id;
    int64_t cents;
    pthread_mutex_t mutex;
} Account;

enum Scenario { SAFE, UNSAFE, DEADLOCK };
int account_init(Account *account, int id, int64_t cents);
int transfer(Account *from, Account *to, int64_t cents, int ordered, int worker);
int banking_run(enum Scenario scenario);
void pthread_check(int error, const char *operation);
#endif
