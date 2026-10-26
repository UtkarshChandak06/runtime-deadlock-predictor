#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Each worker receives its own ID; the IDs remain alive until main joins. */
static void *worker(void *argument)
{
    const int id = *(const int *)argument;
    printf("Worker thread %d started\n", id);
    return NULL;
}

int main(void)
{
    pthread_t threads[2];
    int ids[2] = {1, 2};
    int created = 0;
    int result = EXIT_SUCCESS;

    puts("Deadlock Sentinel - Phase 1");
    /* pthread_create starts a separate execution path in the same process. */
    for (int i = 0; i < 2; ++i) {
        const int error = pthread_create(&threads[i], NULL, worker, &ids[i]);
        /* Pthread functions return error codes directly, rather than errno. */
        if (error != 0) {
            fprintf(stderr, "pthread_create (worker %d): %s\n", i + 1,
                    strerror(error));
            result = EXIT_FAILURE;
            break;
        }
        ++created;
    }

    /* Join every started worker, including after a partial creation failure. */
    for (int i = 0; i < created; ++i) {
        const int error = pthread_join(threads[i], NULL);
        if (error != 0) {
            fprintf(stderr, "pthread_join (worker %d): %s\n", i + 1,
                    strerror(error));
            result = EXIT_FAILURE;
        }
    }
    if (result == EXIT_SUCCESS) {
        puts("Both threads completed successfully");
    }
    return result;
}
