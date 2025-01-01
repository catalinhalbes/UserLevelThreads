#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "ult.h"

uint64_t count = 0, run = 1;

typedef struct {
    char character;
    uint64_t count;
    uint64_t nano_seconds;
    ult_mutex_t* mut;
} thread_arg;

ult_t threads[8];
ult_mutex_t mutexes[8];

typedef struct t_arg {
    ult_mutex_t* mutex1;
    ult_mutex_t* mutex2;
} t_arg;

////////////////// Test 1 ///////////////////////

void do_work(uint64_t work) {
    double res = 0;
    for (uint64_t j = 0; j < work; j++) {
        res += sqrt(j * j * j) / (res + 1);
    }
}

void* print_char(void* arg) {
    thread_arg targ = *((thread_arg*) arg); // this makes a copy... oh well
    uint64_t i = 0;

    int err = 0;

    printf("[%lu] waiting mutex %lu\n", ult_get_id(), targ.mut->id); fflush(NULL);
    err = ult_mutex_lock(targ.mut);
    printf("[%lu] mutex %lu locked, err: %d\n", ult_get_id(), targ.mut->id, err); fflush(NULL);

    while (i < targ.count) {
        // do_work(targ.nano_seconds);
        ult_sleep(0, targ.nano_seconds);

        printf("[%lu]", ult_get_id());
        for (int i = 0; i < ult_get_id(); i++) printf("\t");
        printf("%c %lu\n", targ.character, i);
        fflush(NULL);

        i += 1;
    }

    err = ult_mutex_unlock(targ.mut);
    printf("[%lu] mutex %lu unlocked, err: %d\n", ult_get_id(), targ.mut->id, err); fflush(NULL);

    return (void*)i;
}

void* immediate_exit3(void* arg) {
    printf("[%lu] immediate_exit3: enter\n", ult_get_id());
    ult_sleep(2, 0);
    ult_exit(arg);
    printf("[%lu] immediate_exit3: exit\n", ult_get_id());

    return NULL;
} 

void* immediate_exit2(void* arg) {
    printf("[%lu] immediate_exit2: enter\n", ult_get_id());
    ult_sleep(2, 0);
    immediate_exit3(arg);
    printf("[%lu] immediate_exit2: exit\n", ult_get_id());

    return NULL;
} 

void* immediate_exit(void* arg) {
    printf("[%lu] immediate_exit: enter\n", ult_get_id());
    ult_sleep(2, 0);
    immediate_exit2(arg);
    printf("[%lu] immediate_exit: exit\n", ult_get_id());

    return NULL;
} 

void test1() {
    ult_mutex_t* mutexes = (ult_mutex_t*) malloc(sizeof(ult_mutex_t) * 2);
    ult_t* threads = (ult_t*) malloc(sizeof(ult_t) * 4);

    for (int i = 0; i < 2; i++) {
        ult_mutex_init(&mutexes[i]);
        printf("[%lu] created mutex: %lu\n", ult_get_id(), mutexes[i].id);
    }

    thread_arg args[4] = {{'A', 10, 800000000, &mutexes[1]}, {'B', 20, 400000000, &mutexes[0]}, {'C', 40, 200000000, &mutexes[0]}, {'D', 80, 100000000, &mutexes[1]}};

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

    free(mutexes);
    free(threads);
}

void test2() {
    ult_t t;
    void* val;

    ult_create(&t, immediate_exit, (void*) 0xdeadbeef);
    ult_join(&t, &val);
    printf("Thread returned: %p\n", val);

    ult_create(&t, immediate_exit, (void*) 0xc0ffee);
    ult_exit(NULL);
    ult_join(&t, &val);
    printf("Thread returned: %p\n", val);
}

//////////////// Deadlock test //////////////////

void* deadlock_worker(void* arg) {
    ult_mutex_t* mutex1 = *((ult_mutex_t**) arg);
    ult_mutex_t* mutex2 = *((ult_mutex_t**) arg + 1);

    printf("[%lu] locking mutex1: %lu\n", ult_get_id(), mutex1->id);
    ult_mutex_lock(mutex1);
    printf("[%lu]                       locked mutex1: %lu\n", ult_get_id(), mutex1->id);

    ult_sleep(3, 0);

    printf("[%lu] locking mutex2: %lu\n", ult_get_id(), mutex2->id);
    ult_mutex_lock(mutex2);
    printf("[%lu]                       locked mutex2: %lu\n", ult_get_id(), mutex2->id);

    ult_sleep(3, 0);

    ult_mutex_unlock(mutex1);
    printf("[%lu]                                            unlocked mutex1: %lu\n", ult_get_id(), mutex1->id);

    ult_mutex_unlock(mutex2);
    printf("[%lu]                                            unlocked mutex2: %lu\n", ult_get_id(), mutex2->id);

    return NULL;
}

void* heartbeat_worker(void*) {
    uint64_t i = 0;
    struct timespec start, current, prev;

    if (clock_gettime(CLOCK_REALTIME, &start) == -1) {
        perror("clock_gettime");
    }

    prev = start;

    while(1) {
        i += 1;
        ult_sleep(3, 500000000);

        if (clock_gettime(CLOCK_REALTIME, &current) == -1) {
            perror("clock_gettime");
        }

        double elapsed_start = (current.tv_sec - start.tv_sec) + (current.tv_nsec - start.tv_nsec) / 1e9;
        double elapsed_prev = (current.tv_sec - prev.tv_sec) + (current.tv_nsec - prev.tv_nsec) / 1e9;

        printf("[%lu] Keeping the scheduler busy %lu (elapsed: %lfs, since start: %lfs)\n", ult_get_id(), i, elapsed_prev, elapsed_start);

        prev = current;
    }
}

void deadlock_test(int thread_num) {
    ult_mutex_t* mutexes = (ult_mutex_t*) malloc(sizeof(ult_mutex_t) * thread_num);
    ult_mutex_t** mutex_list = (ult_mutex_t**) malloc(sizeof(ult_mutex_t*) * (thread_num) + 1);
    ult_t* threads = (ult_t*) malloc(sizeof(ult_t) * (thread_num + 1));

    for (int i = 0; i < thread_num; i++) {
        ult_mutex_init(mutexes + i);
    }

    for (int i = 0; i < thread_num + 1; i++) {
        mutex_list[i] = &mutexes[i % thread_num];
    }

    for (int i = 0; i < thread_num; i++) {
        ult_create(threads + i, deadlock_worker, mutex_list + i);
    }

    ult_create(threads + thread_num, heartbeat_worker, NULL);
    
    for (int i = 0; i < thread_num; i++) {
        ult_join(&threads[i], NULL);
        printf("Thread %lu joined\n", threads[i].id);
    }

    for (int i = 0; i < thread_num; i++) {
        ult_mutex_destroy(&mutexes[i]);
    }

    free(mutexes);
    free(threads);
}

///////////////// Deadlock test 2 //////////////////

uint64_t do_long_work(uint64_t multiplier) {
    volatile uint64_t res = 1;
    for (uint64_t i = 0; i < multiplier * 100000000; i++)
        res += (i * i * i) / res;
    return res;
}

void* worker(void* arg) {
    t_arg* targ = (t_arg*) arg;

    printf("  {%lu} waiting mutex1 {%lu}\n", ult_get_id(), targ->mutex1->id); fflush(NULL);
    ult_mutex_lock(targ->mutex1);
    printf("  {%lu}                  locked mutex1 {%lu}\n", ult_get_id(), targ->mutex1->id); fflush(NULL);

    do_long_work(1);

    printf("  {%lu} waiting mutex2 {%lu}\n", ult_get_id(), targ->mutex2->id); fflush(NULL);
    ult_mutex_lock(targ->mutex2);
    printf("  {%lu}                  locked mutex2 {%lu}\n", ult_get_id(), targ->mutex2->id); fflush(NULL);

    ult_mutex_unlock(targ->mutex1);
    ult_mutex_unlock(targ->mutex2);

    return NULL;
}

void* deadlock_main_with_mutex(void* mut) {
    ult_mutex_lock((ult_mutex_t*) mut);
    ult_mutex_unlock((ult_mutex_t*) mut);

    return NULL;
}

void* self_join(void* thread) {
    ult_join((ult_t*) thread, NULL);
    return NULL;
}

void deadlock_test2() {
    for (int i = 0; i < 8; i++)
        ult_mutex_init(&mutexes[i]);

    t_arg args[] = {
        {&mutexes[0], &mutexes[1]}, // 2
        {&mutexes[1], &mutexes[2]}, // 3
        {&mutexes[2], &mutexes[3]}, // 4
        {&mutexes[3], &mutexes[1]}, // 5
        {&mutexes[4], &mutexes[3]}, // 6
        {&mutexes[5], &mutexes[2]}, // 7
        {&mutexes[6], &mutexes[7]}, // 8
        {&mutexes[7], &mutexes[6]}  // 9
    };

    for (int i = 0; i < 8; i++)
        ult_create(&threads[i], worker, (void*) &args[i]);

    ult_t heartbeat, deadlock_main, selfjoin; 
    ult_mutex_t main_mutex;
    
    ult_mutex_init(&main_mutex);
    ult_mutex_lock(&main_mutex);

    ult_create(&deadlock_main, deadlock_main_with_mutex, (void*) &main_mutex);
    ult_create(&selfjoin, self_join, (void*) &selfjoin);
    ult_create(&heartbeat, heartbeat_worker, NULL);

    ult_join(&deadlock_main, NULL);
}

int main() {
    // test1();
    // test2();
    // deadlock_test(5);
    deadlock_test2();
    return 0;
}
