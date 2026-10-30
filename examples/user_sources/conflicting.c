#include <pthread.h>
#include <stdio.h>

int main(void)
{
    pthread_mutex_t document, index_lock;
    if (pthread_mutex_init(&document, NULL) || pthread_mutex_init(&index_lock, NULL)) return 1;
    pthread_mutex_lock(&document);
    pthread_mutex_lock(&index_lock);
    puts("Document then index");
    pthread_mutex_unlock(&index_lock);
    pthread_mutex_unlock(&document);

    /* Sequential: this records reverse order but does not actually deadlock. */
    pthread_mutex_lock(&index_lock);
    pthread_mutex_lock(&document);
    puts("Index then document");
    pthread_mutex_unlock(&document);
    pthread_mutex_unlock(&index_lock);
    pthread_mutex_destroy(&document);
    pthread_mutex_destroy(&index_lock);
    return 0;
}
