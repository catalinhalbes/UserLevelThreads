#ifndef ULT_H
#define ULT_H

#include <stdint.h>
#include "linked_list.h"

#define DEFAULT_ULT_STACK_SIZE 0x4000

#define BAIL(msg) \
    do { \
        if (errno != 0) fprintf(stderr, "System Error: %s\n", strerror(errno)); \
        fprintf(stderr, "\n=====\nError: %s\nFile: %s\nFunction: %s\nLine: %d\n=====\n\n", msg, __FILE__, __FUNCTION__, __LINE__); \
        fflush(NULL); \
        exit(EXIT_FAILURE); \
    } while(0)

typedef void* (*voidptr_arg_voidptr_ret_func)(void*);

#define NULL_ULT 0
#define NULL_MUTEX 0

typedef uint64_t ult_t;
typedef uint64_t ult_mutex_t;

int ult_create(ult_t* thread, voidptr_arg_voidptr_ret_func start_routine, void* arg);
int ult_join(ult_t thread, void** retval);

ult_t ult_get_id();

int ult_mutex_init(ult_mutex_t* mutex);
int ult_mutex_destroy(ult_mutex_t mutex);
int ult_mutex_lock(ult_mutex_t mutex);
int ult_mutex_unlock(ult_mutex_t mutex);

#endif // ULT_H
