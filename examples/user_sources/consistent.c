#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t document, index_lock;
static int updates;

static void *update(void *argument)
{
    (void)argument;
    pthread_mutex_lock(&document);
    pthread_mutex_lock(&index_lock);
    ++updates;
    pthread_mutex_unlock(&index_lock);
    pthread_mutex_unlock(&document);
    return NULL;
}

int main(void)
{
    pthread_t first, second;
    if (pthread_mutex_init(&document, NULL) || pthread_mutex_init(&index_lock, NULL)) return 1;
    if (pthread_create(&first, NULL, update, NULL) || pthread_create(&second, NULL, update, NULL)) return 2;
    if (pthread_join(first, NULL) || pthread_join(second, NULL)) return 3;
    printf("Completed document updates: %d\n", updates);
    pthread_mutex_destroy(&document);
    pthread_mutex_destroy(&index_lock);
    return updates == 2 ? 0 : 4;
}
