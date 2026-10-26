#include "banking.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { fputs("Usage: banking_safe (no arguments)\n", stderr); return 1; }
    return banking_run(SAFE);
}
