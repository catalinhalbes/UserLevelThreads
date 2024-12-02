#ifndef ULT_H
#define ULT_H

#include <stdint.h>
#include <ucontext.h>

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

// the id 0 will be considered invalid

typedef enum {
    RUNNING,
    WAITING,
    FINISHED
} ult_status;

typedef struct ult_t{
    uint64_t                        id;
    ult_status                      status;
    void*                           result;
    void*                           arg;

    struct ult_t*                   waiting_join;
    mutex_linked_list_t             held_mutexes;

    voidptr_arg_voidptr_ret_func    start_routine;
    ucontext_t                      context;
    char                            stack[DEFAULT_ULT_STACK_SIZE];
}ult_t;

typedef struct ult_mutex_t {
    uint64_t            id;
    ult_t*              owner;
    ult_linked_list_t   waiting;
} ult_mutex_t;

int ult_create(ult_t* thread, voidptr_arg_voidptr_ret_func start_routine, void* arg);
int ult_join(ult_t* thread, void** retval);

uint64_t ult_get_id();

int ult_mutex_init(ult_mutex_t* mutex);
int ult_mutex_destroy(ult_mutex_t* mutex);
int ult_mutex_lock(ult_mutex_t* mutex);
int ult_mutex_unlock(ult_mutex_t* mutex);

#endif // ULT_H
