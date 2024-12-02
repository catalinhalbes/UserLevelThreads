#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include <valgrind/valgrind.h>

#include "ult.h"
#include "linked_list.h"

#define CLOCKID CLOCK_REALTIME
#define SIG SIGUSR1
#define TIMER_INTERVAL_NS 1000000 //ns = 1ms

#define SWAP_TO_SCHEDULER(context) \
    should_change_thread = 1; \
    if (swapcontext(context, &scheduler_context) != 0) \
        BAIL("Swapcontext")

static ucontext_t scheduler_context;
static char scheduler_stack[DEFAULT_ULT_STACK_SIZE]; // only accessed by the scheduler context, volatile probably not needed

static ult_t main_ult;

static ult_linked_list_t running_ult_list;
static volatile uint64_t ult_counter = 0;
static volatile uint64_t mutex_counter = 0;

// alarm triggered?
static volatile uint8_t should_change_thread = 0;

void sig_handler(int signum) {
    // See SIGVTALRM (virtual time, none passes while the process is paused)
    // NOTE: // printf shoud not be called inside a signal handler as it is not signal safe

    switch (signum) {
        case SIGUSR1:
            SWAP_TO_SCHEDULER(&(running_ult_list.head->ult->context));
            break;

        case SIGUSR2:
            // printf("SIGUSR2\n");
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

static inline void init_ult(ult_t* ult, uint64_t id, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    ult->id = id;
    ult->status = RUNNING;
    ult->result = NULL;

    ult->waiting_join = NULL;
    init_mutex_linked_list(&(ult->held_mutexes)); 

    ult->arg = arg;
    ult->start_routine = start_routine;
    ult->waiting_join = NULL;
}

static inline void init_ult_context(ult_t* ult, ucontext_t* link) {
    if (getcontext(&(ult->context)) != 0)
        BAIL("Get Context");

    VALGRIND_STACK_REGISTER(ult->stack, ult->stack + DEFAULT_ULT_STACK_SIZE);

    ult->context.uc_stack.ss_sp = ult->stack;
    ult->context.uc_stack.ss_size = sizeof(ult->stack);
    ult->context.uc_link = link;
}

void wrapper() {
    ult_t* current = running_ult_list.head->ult;

    // printf("[%lu] wrapper enter\n", current->id); fflush(NULL);
    current->result = current->start_routine(current->arg);
    // printf("[%lu] routine finished\n", current->id); fflush(NULL);

    unset_signals();

        current->status = FINISHED;

        // remove the current thread from the running list
        delete_ult_first(&running_ult_list);    // in theory the current thread is the first
        // the memory is freed after join

        if (current->waiting_join != NULL) {
            // printf("[%lu] wrapper, change status of (%lu) to RUNNING\n", current->id, current->waiting_join->id); fflush(NULL);
            current->waiting_join->status = RUNNING; // wake the thread waiting to join the current thread, without changing the run order
            insert_ult_last(&running_ult_list, current->waiting_join);
        }

        // printf("[%lu] wrapper exit\n", current->id); fflush(NULL);

    set_signals();

    // automatic return to scheduler
}

void scheduler_worker() {
    // printf("[scheduler] scheduler started\n");

    while (1) {
            // check if we should change the execution to another thread
            if (should_change_thread) {
                should_change_thread = 0;
                rotate_ult_front_to_back(&running_ult_list);
            }

            if (running_ult_list.size == 0) {
                BAIL("There are no running threads! This might indicate that a deadlock that involves all existing threads occured!");
            }

        set_signals(); // set signal handlers before switch

            // swapcontext out of scheduler
            if (swapcontext(&scheduler_context, &(running_ult_list.head->ult->context)) != 0) 
                BAIL("Swapcontext scheduler");

        unset_signals(); // unset signals after switch to avoid scheduler being interrupted
    }
}

static void init_main() {
    uint64_t id = 1;
    ult_counter = 1;

    init_ult(&main_ult, id, NULL, NULL);
    init_ult_context(&main_ult, NULL); // when main is done the entire program is done, no cleanup will be done after

    insert_ult_last(&running_ult_list, &main_ult);
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
        // printf("Initializing library\n");

        init_ult_linked_list(&running_ult_list);

        // this is the first call to the library
        unset_signals();
        init_scheduler();
        init_timer();
        init_main(); // we initialize main after initializing all other lib parts to make sure that we don't execute the code twice
    }
}

// 'public' members

int ult_create(ult_t* thread, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    init_lib();

    ult_t* running = running_ult_list.head->ult;
    // printf("[%lu] create: %lu\n", running->id, ult_counter + 1); fflush(NULL);

    unset_signals(); // protect this area from being interrupted
        ult_counter += 1;
        uint64_t id = ult_counter;
    set_signals(); // end of protected zone

        init_ult(thread, id, start_routine, arg);
        init_ult_context(thread, &scheduler_context);    // when done return to the scheduler

        // cast wraper to a function without parameters that returns void (void (*)(void)) to avoid compiler warning if wrapper has parameters
        makecontext(&(thread->context), wrapper, 0);  // passing pointers as parameters to makecontext might not be portable, so we save the parameters in ult_data
        
    unset_signals();
        insert_ult_last(&running_ult_list, thread);
    set_signals();

    // TODO: maybe it would be more 'fair' to call swap
    return 0;
}

int ult_join(ult_t* thread, void** retval) {
    init_lib();

    unset_signals();

        ult_t* current_waiting_join = running_ult_list.head->ult;

        // printf("[%lu] waiting to join: %lu\n", current_waiting_join->id, thread->id); fflush(NULL);

        if (thread->waiting_join != NULL) {
            // the thread is already being waited by some other thread
            // printf("[%lu] %lu was already waited by %lu\n", current_waiting_join->id, thread->id, thread->waiting_join->id); fflush(NULL);

            set_signals();
            return 2;
        }

        if (thread->status != FINISHED) {
            // printf("[%lu] %lu did not finish yet\n", current_waiting_join->id, thread->id); fflush(NULL);

            if (thread->waiting_join != NULL) {
                // the thread is already being waited by some other thread
                // printf("[%lu] %lu is being waited by %lu\n", current_waiting_join->id, thread->id, thread->waiting_join->id); fflush(NULL);

                set_signals();
                return 2;
            }

            thread->waiting_join = current_waiting_join;
            current_waiting_join->status = WAITING;
            delete_ult_first(&running_ult_list); // remove the current thread from the running list

            SWAP_TO_SCHEDULER(&(current_waiting_join->context)); // the scheduler would set the signals back, but just to make sure
        }

        // current thread is now running after the finish of the to-be-joined thread (or the thread was already in the finished state)     
        *retval = thread->result;

        VALGRIND_STACK_DEREGISTER(thread->stack);

    set_signals();
    return 0;
}

uint64_t ult_get_id() {
    init_lib();
    return running_ult_list.head->ult->id;
}

int ult_mutex_init(ult_mutex_t* mutex) {
    init_lib();

    unset_signals();

        mutex_counter += 1;
        uint64_t id = mutex_counter;
        
    set_signals();

    mutex->id = id;
    mutex->owner = NULL;
    init_ult_linked_list(&(mutex->waiting));

    return 0;
}

int ult_mutex_destroy(ult_mutex_t* mutex) {
    init_lib();

    unset_signals();

        if (mutex->owner != NULL) {
            set_signals();
            return 1;
        }

    set_signals();

    return 0;
}

int ult_mutex_lock(ult_mutex_t* mutex) {
    init_lib();

    unset_signals();

        ult_t* current = running_ult_list.head->ult;

        if (mutex->owner == NULL) {
            // the mutex is free
            mutex->owner = current;
            set_signals();
            return 0;
        }

        if (mutex->owner->id == current->id) { // comapring the pointers should also work, but for corectness comapre ids
            // the mutex is held by the running thread
            set_signals();
            return 0;
        }

        // the current thread should wait
        insert_ult_last(&(mutex->waiting), current);
        current->status = WAITING;
        delete_ult_first(&running_ult_list);

        SWAP_TO_SCHEDULER(&(current->context));

        // from now on the mutex is held by the curent thread
        insert_mutex_last(&(current->held_mutexes), mutex);

    set_signals();
    
    return 0;
}

int ult_mutex_unlock(ult_mutex_t* mutex) {
    init_lib();

    unset_signals();

        ult_t* current = running_ult_list.head->ult;

        if (mutex == NULL || mutex->owner == NULL || mutex->owner->id != current->id) {
            // exit with error if the mutex doesn't exist, has no owner or the current thread is not the owner
            set_signals();
            return 1;
        }

        // remove the mutex from current thread's held list
        mutex_node_t* mutex_node = current->held_mutexes.head;
        while (mutex_node != NULL) {
            ult_mutex_t* aux = mutex_node->mutex;

            if (aux->id == mutex->id) {
                delete_mutex_node(&(current->held_mutexes), mutex_node);
                break;  // in theory the mutex appears only once in the list so no need to preserve the next node
            }

            mutex_node = mutex_node->next;
        }

        // current thread frees the mutex
        mutex->owner = NULL;

        // if there are threads waiting for this mutex
        if (mutex->waiting.size > 0) { 
            // pass the ownership to the next thread in the waiting list
            mutex->owner = mutex->waiting.head->ult;
            delete_ult_first(&(mutex->waiting));

            // if there is a thread waiting, WAKE IT UP!
            mutex->owner->status = RUNNING;
            insert_ult_last(&running_ult_list, mutex->owner);
        }

        // maybe it would be a good idea to switch to the scheduler

    set_signals();
    
    return 0;
}
