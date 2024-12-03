#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
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

static ult_linked_list_t running_ult_list, not_finished_ults;
static volatile uint64_t ult_counter = 0;
static volatile uint64_t mutex_counter = 0;
static uint32_t deadlock_counter = 0; // this value combined with the explore counter in the ult structure will indicate if a node in the lock graph was already explored in the current stage
                                      // we don't care about overflows, by this reason this variable could have been byte sized, but for alignment reasons the structure will use a 32bit unsigned

// alarm triggered?
static volatile uint8_t should_change_thread = 0;
static volatile uint8_t should_look_after_deadlocks = 0;


static void set_signals();
static void unset_signals();

void find_deadlocks() {
    deadlock_counter += 1;

    ult_linked_list_t explore_stack, path_stack;
    init_ult_linked_list(&explore_stack);
    init_ult_linked_list(&path_stack);

    int found_deadlocks = 0;

    ult_node_t* candidate = not_finished_ults.head;
    while (candidate != NULL) { // start exploring from the current node
        insert_ult_first(&explore_stack, candidate->ult);

        while(explore_stack.size > 0) {
            ult_t* current = explore_stack.head->ult; delete_ult_first(&explore_stack);

            if (current == NULL) {
                // we explored the current node entirely, now backtrack a level
                delete_ult_first(&path_stack);
                continue;
            }

            if (current->deadlock_explore_counter == deadlock_counter) {
                // the node is already explored
                // look after a cycle in the explored path

                ult_node_t* path_node = path_stack.head;
                while (path_node != NULL) {
                    if (current->id == path_node->ult->id) {
                        found_deadlocks += 1;
                        break;
                    }
                    path_node = path_node->next;
                }
                continue;
            }

            // the current thread is not explored
            current->deadlock_explore_counter = deadlock_counter;
            
            insert_ult_first(&path_stack, current);
            insert_ult_first(&explore_stack, NULL);

            ult_t* next_candidate = current->waiting_to_join;
            if (next_candidate != NULL) {
                insert_ult_first(&explore_stack, next_candidate);
            }

            if (current->waiting_mutex != NULL) {
                next_candidate = current->waiting_mutex->owner;
                insert_ult_first(&explore_stack, next_candidate);
            }
        }
        candidate = candidate->next;
    }

    printf("\n\n====\nFound %d deadlocks\n====\n\n", found_deadlocks); fflush(NULL);
}

void sig_handler(int signum) {
    switch (signum) {
        case SIGUSR1:
            SWAP_TO_SCHEDULER(&(running_ult_list.head->ult->context));
            break;

        case SIGUSR2:
            should_look_after_deadlocks = 1;
            break;
    }
}

static void set_signals() {
    if (signal(SIGUSR1, sig_handler) == SIG_ERR)
        BAIL("Unable to catch SIGUSR1");
}

static void unset_signals() {
    if (signal(SIGUSR1, SIG_IGN) == SIG_ERR)
        BAIL("Unable to unset SIGUSR1");
}

static inline void init_ult(ult_t* ult, uint64_t id, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    ult->id     = id;
    ult->status = RUNNING;
    ult->result = NULL;

    ult->joined_by             = NULL;
    ult->waiting_to_join                  = NULL;
    ult->waiting_mutex            = NULL;
    ult->deadlock_explore_counter = 0;

    ult->arg           = arg;
    ult->start_routine = start_routine;
    ult->joined_by  = NULL;
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

        if (current->joined_by != NULL) {
            // printf("[%lu] wrapper, change status of (%lu) to RUNNING\n", current->id, current->joined_by->id); fflush(NULL);
            current->joined_by->status = RUNNING; // wake the thread waiting to join the current thread, without changing the run order
            current->joined_by->waiting_to_join = NULL;
            insert_ult_last(&running_ult_list, current->joined_by);
        }

        // printf("[%lu] wrapper exit\n", current->id); fflush(NULL);

    set_signals();

    // automatic return to scheduler
}

void scheduler_worker() {
    // printf("[scheduler] scheduler started\n");

    while (1) {
            // check for deadlocks?
            if (should_look_after_deadlocks) {
                find_deadlocks();
                should_look_after_deadlocks = 0;
            }

            // check if we should change the execution to another thread
            if (should_change_thread) {
                rotate_ult_front_to_back(&running_ult_list);
                should_change_thread = 0;
            }

            ult_t* thread = running_ult_list.head->ult;

            while (thread->status != RUNNING) {
                if (thread->status == SLEEPING) {
                    struct timespec current_time;

                    if (clock_gettime(CLOCKID, &current_time) == -1) {
                        BAIL("Get Time");
                    }

                    const uint64_t ns_in_sec = 1000000000;
                    uint64_t elapsed = (current_time.tv_sec - thread->sleep_time.tv_sec) * ns_in_sec + (current_time.tv_nsec - thread->sleep_time.tv_nsec);
                    
                    if (elapsed > thread->sleep_amount_nsec) {
                        // the thread should wake up
                        thread->status = RUNNING;
                    }
                    else {
                        // the thread should remain sleeping
                        rotate_ult_front_to_back(&running_ult_list);
                        thread = running_ult_list.head->ult;
                    }
                }
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
        init_ult_linked_list(&not_finished_ults);

        // this is the first call to the library
        unset_signals();
        init_scheduler();
        init_timer();
        init_main(); // we initialize main after initializing all other lib parts to make sure that we don't execute the code twice

        if (signal(SIGUSR2, sig_handler) == SIG_ERR)
            BAIL("Unable to catch SIGUSR2");
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
        insert_ult_last(&not_finished_ults, thread);
    set_signals();

    // TODO: maybe it would be more 'fair' to call swap
    return 0;
}

int ult_join(ult_t* thread, void** retval) {
    init_lib();

    unset_signals();

        ult_t* current_waiting_join = running_ult_list.head->ult;

        // printf("[%lu] waiting to join: %lu\n", current_waiting_join->id, thread->id); fflush(NULL);

        if (thread->joined_by != NULL) {
            // the thread is already being waited by some other thread
            // printf("[%lu] %lu was already waited by %lu\n", current_waiting_join->id, thread->id, thread->joined_by->id); fflush(NULL);

            set_signals();
            return 2;
        }

        if (thread->status != FINISHED) {
            // printf("[%lu] %lu did not finish yet\n", current_waiting_join->id, thread->id); fflush(NULL);

            if (thread->joined_by != NULL) {
                // the thread is already being waited by some other thread
                // printf("[%lu] %lu is being waited by %lu\n", current_waiting_join->id, thread->id, thread->joined_by->id); fflush(NULL);

                set_signals();
                return 2;
            }

            thread->joined_by = current_waiting_join;
            current_waiting_join->status = WAITING;
            current_waiting_join->waiting_to_join = thread;
            delete_ult_first(&running_ult_list); // remove the current thread from the running list

            unset_signals();
            SWAP_TO_SCHEDULER(&(current_waiting_join->context)); // the scheduler would set the signals back
            set_signals();
        }

        // current thread is now running after the finish of the to-be-joined thread (or the thread was already in the finished state)
        if (retval != NULL) {
            *retval = thread->result;
        }

        // remove the thread from the not finished list
        ult_node_t* current = not_finished_ults.head;
        while (current != NULL) {
            if (current->ult->id == thread->id) {
                // there won't be duplicate ids, we can exit immediately after deleting this node so there is no need to save the next node pointer
                delete_ult_node(&not_finished_ults, current);
                break;
            }

            current = current->next;
        }

        VALGRIND_STACK_DEREGISTER(thread->stack);

    set_signals();
    return 0;
}

void ult_sleep(uint64_t sec, uint64_t nsec) {
    ult_t* current = running_ult_list.head->ult;

    if (clock_gettime(CLOCKID, &(current->sleep_time)) == -1) {
        BAIL("Get Time");
    }

    current->sleep_amount_nsec = sec * 1000000000 + nsec;
    current->status = SLEEPING;

    SWAP_TO_SCHEDULER(&(current->context));
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
        current->waiting_mutex = mutex;
        delete_ult_first(&running_ult_list);

        unset_signals();
        SWAP_TO_SCHEDULER(&(current->context));
        set_signals();

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

        // current thread frees the mutex
        mutex->owner = NULL;

        // if there are threads waiting for this mutex
        if (mutex->waiting.size > 0) { 
            // pass the ownership to the next thread in the waiting list
            mutex->owner = mutex->waiting.head->ult;
            delete_ult_first(&(mutex->waiting));

            // if there is a thread waiting, WAKE IT UP!
            mutex->owner->status = RUNNING;
            mutex->owner->waiting_mutex = NULL;
            insert_ult_last(&running_ult_list, mutex->owner);
        }

        // maybe it would be a good idea to switch to the scheduler

    set_signals();
    
    return 0;
}
