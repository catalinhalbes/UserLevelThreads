#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "ult.h"
#include "linked_list.h"

#define CLOCKID CLOCK_REALTIME
#define SIG SIGALRM
#define TIMER_INTERVAL_NS 1000000 //ns = 1ms

#define SWAP_TO_SCHEDULER(context) \
    should_change_thread = 1; \
    if (swapcontext(context, &scheduler_context) != 0) \
        BAIL("Swapcontext")

typedef enum {
    RUNNING,
    WAITING,
    FINISHED
} ult_status;

typedef struct ult_data_t{
    ult_t                           id;
    ult_status                      status;
    void*                           result;
    void*                           arg;

    struct ult_data_t*              waiting_join;
    linked_list_t                   held_mutexes;

    voidptr_arg_voidptr_ret_func    start_routine;
    ucontext_t                      context;
    char                            stack[DEFAULT_ULT_STACK_SIZE];
}ult_data_t;

typedef struct ult_mutex_data_t {
    ult_mutex_t     id;
    ult_data_t*     owner;
    linked_list_t   waiting;
} ult_mutex_data_t;

static ucontext_t scheduler_context;
static char scheduler_stack[DEFAULT_ULT_STACK_SIZE]; // only accessed by the scheduler context, volatile probably not needed

static linked_list_t running_ult_list, all_ults;
static volatile uint64_t ult_counter = 0;

static linked_list_t mutex_list;
static volatile uint64_t mutex_counter = 0;

// alarm triggered?
static volatile uint8_t should_change_thread = 0;

static inline ult_data_t* get_ult_data(node_t* node) {
    return (ult_data_t*) node->data;
}

static inline ult_mutex_data_t* get_ult_mutex(node_t* node) {
    return (ult_mutex_data_t*) node->data;
}

static inline ucontext_t* get_context(node_t* node) {
    return &(((ult_data_t*) node->data)->context);
}

void sig_handler(int signum) {
    // See SIGVTALRM (virtual time, none passes while the process is paused)
    // NOTE: printf shoud not be called inside a signal handler as it is not signal safe

    switch (signum) {
        case SIGUSR1:
            printf("SIGUSR1\n");
            break;

        case SIGALRM:
            // printf("SIGALRM\n");
            SWAP_TO_SCHEDULER(get_context(running_ult_list.head));
            break;
    }
}

static void set_signals() {
    if (signal(SIGALRM, sig_handler) == SIG_ERR)
        BAIL("Unable to catch SIGALRM");

    if (signal(SIGUSR1, sig_handler) == SIG_ERR)
        BAIL("Unable to catch SIGUSR1");
}

static void unset_signals() {
    if (signal(SIGALRM, SIG_IGN) == SIG_ERR)
        BAIL("Unable to catch SIGALRM");

    if (signal(SIGUSR1, SIG_IGN) == SIG_ERR)
        BAIL("Unable to catch SIGUSR1");
}

static inline void init_ult(ult_data_t* ult, ult_t id, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    ult->id = id;
    ult->status = RUNNING;
    ult->result = NULL;

    ult->waiting_join = NULL;
    init_linked_list(&(ult->held_mutexes)); 

    ult->arg = arg;
    ult->start_routine = start_routine;
    ult->waiting_join = NULL;
}

static inline void init_ult_context_data(ult_data_t* ult, ucontext_t* link) {
    if (getcontext(&(ult->context)) != 0)
        BAIL("Get Context");

    ult->context.uc_stack.ss_sp = ult->stack;
    ult->context.uc_stack.ss_size = sizeof(ult->stack);
    ult->context.uc_link = link;
}

void wrapper() {
    ult_data_t* current = get_ult_data(running_ult_list.head);

    printf("[%lu] wrapper enter\n", current->id); fflush(NULL);
    current->result = current->start_routine(current->arg);
    printf("[%lu] routine finished\n", current->id); fflush(NULL);

    unset_signals();

        current->status = FINISHED;

        // remove the current thread from the running list
        delete_first(&running_ult_list);    // in theory the current thread is the first
        // the memory is freed after join

        if (current->waiting_join != NULL) {
            printf("[%lu] wrapper, change status of (%lu) to RUNNING\n", current->id, current->waiting_join->id); fflush(NULL);
            current->waiting_join->status = RUNNING; // wake the thread waiting to join the current thread, without changing the run order
            insert_last(&running_ult_list, current->waiting_join);
        }

        printf("[%lu] wrapper exit\n", current->id); fflush(NULL);

    set_signals();

    // automatic return to scheduler
}

static void create_new_ult(voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    ult_counter += 1;
    ult_t id = ult_counter;

    ult_data_t* new_ult_data = (ult_data_t*) malloc(sizeof(ult_data_t));
    init_ult(new_ult_data, id, start_routine, arg);
    init_ult_context_data(new_ult_data, &scheduler_context);    // when done return to the scheduler

    // cast wraper to a function without parameters that returns void (void (*)(void)) to avoid compiler warning if wrapper has parameters
    makecontext(&(new_ult_data->context), wrapper, 0);  // passing pointers as parameters to makecontext might not be portable, so we save the parameters in ult_data
    
    insert_last(&running_ult_list, new_ult_data);
    insert_last(&all_ults, new_ult_data);
}

void scheduler_worker() {
    printf("[scheduler] scheduler started\n");

    while (1) {
            // check if we should change the execution to another thread
            if (should_change_thread) {
                should_change_thread = 0;
                rotate_front_to_back(&running_ult_list);
            }

            if (running_ult_list.size == 0) {
                BAIL("There are no running threads! This might indicate that a deadlock that involves all existing threads occured!");
            }

        set_signals(); // set signal handlers before switch

            // swapcontext out of scheduler
            if (swapcontext(&scheduler_context, get_context(running_ult_list.head)) != 0) 
                BAIL("Swapcontext scheduler");

        unset_signals(); // unset signals after switch to avoid scheduler being interrupted
    }
}

static void init_main() {
    ult_t id = 1;
    ult_counter = 1;

    ult_data_t* main_ult_data = (ult_data_t*) malloc(sizeof(ult_data_t));
    init_ult(main_ult_data, id, NULL, NULL);
    init_ult_context_data(main_ult_data, &scheduler_context); // when main is done return to scheduler to make sure no other threads were left behind and perform final cleanup

    insert_last(&running_ult_list, main_ult_data);
    insert_last(&all_ults, main_ult_data);
}

static void init_scheduler() {
    if (getcontext(&scheduler_context) != 0)
        BAIL("Get Context SCHEDULER");

    scheduler_context.uc_stack.ss_sp = scheduler_stack;
    scheduler_context.uc_stack.ss_size = sizeof(scheduler_stack);
    scheduler_context.uc_link = NULL; // when the scheduler is done, everything should be done

    makecontext(&scheduler_context, scheduler_worker, 0);
}

static void init_timer() {
    timer_t             timerid;
    struct sigevent     sev;
    struct itimerspec   its;

    // Set up the timer
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIG;
    sev.sigev_value.sival_ptr = &timerid;
    if (timer_create(CLOCKID, &sev, &timerid) == -1) {
        BAIL("Timer Create");
    }

    its.it_value.tv_sec = 0;
    its.it_value.tv_nsec = TIMER_INTERVAL_NS;
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = TIMER_INTERVAL_NS;

    // Start the timer
    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        BAIL("Timer Settime");
    }
}

static inline void init_lib() {
    if (ult_counter == 0) {
        printf("Initializing library\n");

        init_linked_list(&running_ult_list);
        init_linked_list(&all_ults);
        init_linked_list(&mutex_list);

        // this is the first call to the library
        init_scheduler();
        init_timer();
        init_main(); // we initialize main after initializing all other lib parts to make sure that we don't execute the code twice
    }
}

// 'public' members

int ult_create(ult_t* thread, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    init_lib();

    // TODO: the area that needs protection can be split into 2 smaller ones (one that gets the thread id and one that inserts the thread into the lists) if create_new_ult is inlined directly
    unset_signals(); // protect this area from being interrupted

        ult_data_t* running = get_ult_data(running_ult_list.head);
        printf("[%lu] create: %lu\n", running->id, ult_counter + 1); fflush(NULL);

        create_new_ult(start_routine, arg);
        *thread = get_ult_data(running_ult_list.tail)->id;

    set_signals(); // end of protected zone

    // TODO: maybe it would be more 'fair' to call swap
    return 0;
}

int ult_join(ult_t thread, void** retval) {
    init_lib();

    unset_signals();

        ult_data_t* current_waiting_join = get_ult_data(running_ult_list.head);

        printf("[%lu] waiting to join: %lu\n", current_waiting_join->id, thread); fflush(NULL);

        // search the thread to join
        node_t* to_join_node = all_ults.head;
        ult_data_t* to_join = NULL;

        while (to_join_node != NULL) {
            ult_data_t* aux = get_ult_data(to_join_node);

            if (aux->id == thread) {
                to_join = aux;
                break;
            }

            to_join_node = to_join_node->next;
        }

        if (to_join == NULL) {
            set_signals();
            return 1;
        }

        if (to_join->waiting_join != NULL) {
            // the thread is already being waited by some other thread
            printf("[%lu] %lu was already waited by %lu\n", current_waiting_join->id, to_join->id, to_join->waiting_join->id); fflush(NULL);

            set_signals();
            return 2;
        }

        if (to_join->status != FINISHED) {
            printf("[%lu] %lu did not finish yet\n", current_waiting_join->id, to_join->id); fflush(NULL);

            if (to_join->waiting_join != NULL) {
                // the thread is already being waited by some other thread
                printf("[%lu] %lu is being waited by %lu\n", current_waiting_join->id, to_join->id, to_join->waiting_join->id); fflush(NULL);

                set_signals();
                return 2;
            }

            to_join->waiting_join = current_waiting_join;
            current_waiting_join->status = WAITING;
            delete_first(&running_ult_list); // remove the current thread from the running list

            SWAP_TO_SCHEDULER(&(current_waiting_join->context)); // the scheduler would set the signals back, but just to make sure
        }

        // current thread is now running after the finish of the to-be-joined thread (or the thread was already in the finished state)     
        *retval = to_join->result;

        // remove the joined thread from the list
        free(to_join);
        delete_node(&all_ults, to_join_node);

    set_signals();
    return 0;
}

ult_t ult_get_id() {
    init_lib();
    return get_ult_data(running_ult_list.head)->id;
}

int ult_mutex_init(ult_mutex_t* mutex) {
    init_lib();

    unset_signals();

        mutex_counter += 1;
        ult_mutex_t id = mutex_counter;
        
    set_signals();

    ult_mutex_data_t* mutex_data = (ult_mutex_data_t*) malloc(sizeof(ult_mutex_data_t));

    *mutex = id;
    mutex_data->id = id;
    mutex_data->owner = NULL;
    init_linked_list(&(mutex_data->waiting));

    unset_signals();

        insert_last(&mutex_list, mutex_data);

    set_signals();

    return 0;
}

int ult_mutex_destroy(ult_mutex_t mutex) {
    init_lib();

    unset_signals();

        node_t* mutex_node = mutex_list.head;
        ult_mutex_data_t* mutex_data = NULL;

        while (mutex_node != NULL) {
            ult_mutex_data_t* aux = get_ult_mutex(mutex_node);

            if (aux->id == mutex) {
                mutex_data = aux;
                break;
            }

            mutex_node = mutex_node->next;
        }

        if (mutex_data == NULL) {
            set_signals();
            return 1;
        }

        free(mutex_data);
        delete_node(&mutex_list, mutex_node);

    set_signals();

    return 0;
}

int ult_mutex_lock(ult_mutex_t mutex) {
    init_lib();

    unset_signals();

        ult_data_t* current = get_ult_data(running_ult_list.head);

        node_t* mutex_node = mutex_list.head;
        ult_mutex_data_t* mutex_data = NULL;

        while (mutex_node != NULL) {
            ult_mutex_data_t* aux = get_ult_mutex(mutex_node);

            if (aux->id == mutex) {
                mutex_data = aux;
                break;
            }

            mutex_node = mutex_node->next;
        }

        if (mutex_data == NULL) {
            set_signals();
            return 1;
        }

        if (mutex_data->owner == NULL) {
            // the mutex is free
            mutex_data->owner = current;
            set_signals();
            return 0;
        }

        if (mutex_data->owner->id == current->id) { // comapring the pointers should also work, but for corectness comapre ids
            // the mutex is held by the running thread
            set_signals();
            return 0;
        }

        // the current thread should wait
        insert_last(&(mutex_data->waiting), current);
        current->status = WAITING;
        delete_first(&running_ult_list);

        SWAP_TO_SCHEDULER(&(current->context));

        // from now on the mutex is held by the curent thread
        insert_last(&(current->held_mutexes), mutex_data);

    set_signals();
    
    return 0;
}

int ult_mutex_unlock(ult_mutex_t mutex) {
    init_lib();

    unset_signals();

        ult_data_t* current = get_ult_data(running_ult_list.head);

        node_t* mutex_node = mutex_list.head;
        ult_mutex_data_t* mutex_data = NULL;

        while (mutex_node != NULL) {
            ult_mutex_data_t* aux = get_ult_mutex(mutex_node);

            if (aux->id == mutex) {
                mutex_data = aux;
                break;
            }

            mutex_node = mutex_node->next;
        }

        if (mutex_data == NULL || mutex_data->owner == NULL || mutex_data->owner->id != current->id) {
            // exit with error if the mutex doesn't exist, has no owner or the current thread is not the owner
            set_signals();
            return 1;
        }

        // remove the mutex from current thread's held list
        mutex_node = current->held_mutexes.head;
        while (mutex_node != NULL) {
            ult_mutex_data_t* aux = get_ult_mutex(mutex_node);

            if (aux->id == mutex) {
                delete_node(&(current->held_mutexes), mutex_node);
                break;  // in theory the mutex appears only once in the list so no need to preserve the next node
            }

            mutex_node = mutex_node->next;
        }

        // current thread frees the mutex
        mutex_data->owner = NULL;

        // if there are threads waiting for this mutex
        if (mutex_data->waiting.size > 0) { 
            // pass the ownership to the next thread in the waiting list
            mutex_data->owner = get_ult_data(mutex_data->waiting.head);
            delete_first(&(mutex_data->waiting));

            // if there is a thread waiting, WAKE IT UP!
            mutex_data->owner->status = RUNNING;
            insert_last(&running_ult_list, mutex_data->owner);
        }

        // maybe it would be a good idea to switch to the scheduler

    set_signals();
    
    return 0;
}
