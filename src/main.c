#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "ult.h"

uint64_t count = 0, run = 1;

typedef struct {
    char character;
    uint64_t count;
    uint64_t seconds;
    ult_mutex_t* mut;
} thread_arg;

void* print_char(void* arg) {
    thread_arg targ = *((thread_arg*) arg); // this makes a copy... oh well
    uint64_t i = 0;

    int err = 0;



    err = ult_mutex_lock(targ.mut);
    printf("[%lu] mutex %lu locked, err: %d\n", ult_get_id(), targ.mut->id, err);

    while (i < targ.count) {
        uint64_t res = 0;

        for (uint64_t j = 0; j < targ.seconds * 30000000; j++) {
            res += sqrt(j * j * j);
        }

        printf("[%lu]", ult_get_id());
        for (int i = 0; i < ult_get_id(); i++)
            printf("\t");
        printf("%c %lu\n", targ.character, i);
        fflush(NULL);

        i += 1;
    }

    err = ult_mutex_unlock(targ.mut);
    printf("[%lu] mutex %lu unlocked, err: %d\n", ult_get_id(), targ.mut->id, err);

    return (void*)i;
}

int main() {
    ult_mutex_t mutexes[2];
    ult_t threads[4];

    for (int i = 0; i < 2; i++) {
        ult_mutex_init(&mutexes[i]);
        printf("[%lu] created mutex: %lu\n", ult_get_id(), mutexes[i].id);
    }

    thread_arg args[4] = {{'A', 10, 8, &mutexes[1]}, {'B', 20, 4, &mutexes[0]}, {'C', 40, 2, &mutexes[0]}, {'D', 80, 1, &mutexes[1]}};

    for (int i = 0; i < 4; i++)
        ult_create(threads + i, print_char, args + i);
    
    for (int i = 0; i < 4; i++) {
        uint64_t retval;
        int err = ult_join(&threads[i], (void**)&retval);

        printf("Thread %lu returned %lu (error: %d)\n", threads[i].id, retval, err);
    }

    for (int i = 0; i < 2; i++) {
        int err = ult_mutex_destroy(&mutexes[i]);
        printf("[%lu] mutex destroyed, err: %d\n", ult_get_id(), err);
    }

    return 0;
}
