#include "banking.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    /* Accident-prevention guard, not a security boundary. Use only the harness. */
    if (argc != 2 || strcmp(argv[1], "--controlled-fixture") != 0) {
        fputs("Run through: python3 tests/test_banking.py --deadlock-only\n", stderr);
        return 1;
    }
    return banking_run(DEADLOCK);
}
