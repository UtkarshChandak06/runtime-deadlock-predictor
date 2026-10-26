#include "banking.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { fputs("Usage: banking_unsafe (no arguments)\n", stderr); return 1; }
    return banking_run(UNSAFE);
}
