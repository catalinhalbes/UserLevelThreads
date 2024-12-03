#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

pthread_t threads[8];
pthread_mutex_t mutexes[8];

typedef struct thread_arg {
    uint64_t id;
    pthread_mutex_t* mutex1;
    pthread_mutex_t* mutex2;
} thread_arg;

uint64_t do_long_work(uint64_t multiplier) {
    volatile uint64_t res = 1;
    for (uint64_t i = 0; i < multiplier * 100000000; i++)
        res += (i * i * i) / res;
    return res;
}

void* worker(void* arg) {
    thread_arg* targ = (thread_arg*) arg;

    printf("[%lu] waiting mutex1\n", targ->id); fflush(NULL);
    pthread_mutex_lock(targ->mutex1);
    printf("[%lu]                  locked mutex1\n", targ->id); fflush(NULL);

    do_long_work(1);

    printf("[%lu] waiting mutex2\n", targ->id); fflush(NULL);
    pthread_mutex_lock(targ->mutex2);
    printf("[%lu]                  locked mutex2\n", targ->id); fflush(NULL);

    pthread_mutex_unlock(targ->mutex1);
    pthread_mutex_unlock(targ->mutex2);

    return NULL;
}

int main() {
    for (int i = 0; i < 8; i++)
        pthread_mutex_init(&mutexes[i], NULL);

    thread_arg args[] = {
        {1, &mutexes[0], &mutexes[1]},
        {2, &mutexes[1], &mutexes[2]},
        {3, &mutexes[2], &mutexes[3]},
        {4, &mutexes[3], &mutexes[1]},
        {5, &mutexes[4], &mutexes[3]},
        {6, &mutexes[5], &mutexes[2]},
        {7, &mutexes[6], &mutexes[7]},
        {8, &mutexes[7], &mutexes[6]}
    };

    for (int i = 0; i < 8; i++)
        pthread_create(&threads[i], NULL, worker, (void*) &args[i]);

    for (int i = 0; i < 8; i++)
        pthread_join(threads[i], NULL);

    for (int i = 0; i < 8; i++)
        pthread_mutex_destroy(&mutexes[i]);

    return 0;
}
