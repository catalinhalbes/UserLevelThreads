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

#define CLOCKID CLOCK_PROCESS_CPUTIME_ID // can use CLOCK_THREAD_CPUTIME_ID, CLOCK_PROCESS_CPUTIME_ID
#define SLEEP_CLOCK CLOCK_REALTIME
#define TIMER_SIG SIGUSR1
#define DEADLOCK_SIG SIGUSR2
#define TIMER_INTERVAL_NS 1000000 //ns = 1ms

static ult_t main_ult;

static ult_linked_list_t running_ult_list, not_finished_ults;
static volatile uint64_t ult_counter = 0;
static volatile uint64_t mutex_counter = 0;
static volatile uint64_t cond_counter = 0;
static uint32_t deadlock_counter = 0; // this value combined with the explore counter in the ult structure will indicate if a node in the lock graph was already explored in the current stage
                                      // we don't care about overflows, by this reason this variable could have been byte sized, but for alignment reasons the structure will use a 32bit unsigned

// alarm triggered?
static volatile uint8_t should_change_thread = 0;
static volatile uint8_t inside_protected_zone = 0;  // we still need to ignore signals even if we mask them, because masking will keep the signal until it is unmasked, 
                                                    // but we don't want to execute that signal immediately, because we would still be in the scheduler
sigset_t mask_sig, no_mask;

// Calling a function that creates a protected zone inside a protected zone will end all the protection zones upon returning (assuming protection zones are NOT reentrant)
// This means that to ensure the continuity of the protection zone no other function that creates protection zones should be called inside the protection zone
// Fixing this problem is also tricky. You can:
//      1. Duplicate the needed code so it doesn't overwrite the protection zone (leads to code duplication, not ideal)
//      2. Make the protection zones reentrant using a counter (leads to problems when manually calling the SCHEDULER function as it would require all protection zones to be exitted before calling)

static void start_protected_zone() {
    inside_protected_zone = 1;
    sigprocmask(SIG_BLOCK, &mask_sig, NULL);
}

static void end_protected_zone() {
    sigprocmask(SIG_SETMASK, &no_mask, NULL);
    inside_protected_zone = 0;
}

// TODO: what happens if a thread finished without releasing resources? 
//       It would be a deadlock situation, but not ciclycal!
//       worst part: if the thread is joined it can also be freed
void find_deadlocks() {
    start_protected_zone();

    deadlock_counter += 1;

    ult_linked_list_t explore_stack, path;
    init_ult_linked_list(&explore_stack);
    init_ult_linked_list(&path);

    uint64_t found_deadlocks = 0;

    ult_node_t* candidate = not_finished_ults.head;
    while (candidate != NULL) { // start exploring from the current node
        insert_ult_first(&explore_stack, candidate->ult);

        while(explore_stack.size > 0) {
            ult_t* current = explore_stack.head->ult; delete_ult_first(&explore_stack);

            if (current == NULL) {
                // we explored the current node entirely, now backtrack a level
                delete_ult_last(&path);
                continue;
            }

            if (current->deadlock_explore_counter == deadlock_counter) {
                // the node is already explored
                // look after a cycle in the explored path

                ult_node_t* path_node = path.head;
                uint8_t deadlock = 0;
                while (path_node != NULL) {
                    if (deadlock) {
                        // we found a deadlock, continue by printing the threads caugth in the deadlock cycle
                        printf(" -> %lu", path_node->ult->id);
                    } 
                    else if (current->id == path_node->ult->id) {
                        found_deadlocks += 1;
                        deadlock = 1;

                        if (found_deadlocks == 1) {
                            printf("\n\n====\n\n");
                        }

                        printf ("Deadlock %lu:\n %lu", found_deadlocks, current->id);
                    }
                    path_node = path_node->next;
                }
                if (deadlock) {
                    printf(" -> %lu \n\n", current->id); fflush(NULL);
                }
                continue;
            }

            // the current thread is not explored
            current->deadlock_explore_counter = deadlock_counter;
            
            insert_ult_last(&path, current);
            insert_ult_first(&explore_stack, NULL);

            ult_t* next_candidate = NULL;

            next_candidate = current->waiting_to_join;
            if (next_candidate != NULL) {
                insert_ult_first(&explore_stack, next_candidate);
            }

            if (current->waiting_mutex != NULL) {
                next_candidate = current->waiting_mutex->owner;
                insert_ult_first(&explore_stack, next_candidate);
            }

            if (current->waiting_cond != NULL) {
                // the thread is waiting a condition
                // any thread that is not waiting the same condition is a potential signaler
                ult_node_t* aux = not_finished_ults.head;
                while(aux != NULL) {
                    ult_t* c = aux->ult;

                    if (c->waiting_cond == NULL || c->waiting_cond->id != current->waiting_cond->id) {
                        // not waiting the same cond
                        insert_ult_first(&explore_stack, c);
                    }
                    
                    aux = aux->next;
                }
            }
        }
        candidate = candidate->next;
    }

    printf("====\nFound %lu deadlocks\n====\n\n", found_deadlocks); fflush(NULL);

    end_protected_zone();
}

static inline void init_ult(ult_t* ult, uint64_t id, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    ult->id     = id;
    ult->status = RUNNING;
    ult->result = NULL;

    ult->joined_by                = NULL;
    ult->waiting_to_join          = NULL;
    ult->waiting_mutex            = NULL;
    ult->deadlock_explore_counter = 0;

    ult->arg           = arg;
    ult->start_routine = start_routine;
    ult->joined_by     = NULL;
}

static inline void init_ult_context(ult_t* ult, ucontext_t* link) {
    if (getcontext(&(ult->context)) != 0)
        BAIL("Get Context");

    VALGRIND_STACK_REGISTER(ult->stack, ult->stack + DEFAULT_ULT_STACK_SIZE);

    ult->context.uc_stack.ss_sp = ult->stack;
    ult->context.uc_stack.ss_size = sizeof(ult->stack);
    ult->context.uc_link = link;
}

void SCHEDULER(ult_t* current) {
    // printf("[scheduler / %ld] scheduler started\n", current->id); fflush(NULL);

    start_protected_zone(); // unset signals after switch to avoid scheduler being interrupted

    // check if we should change the execution to another thread
    if (should_change_thread) {
        rotate_ult_front_to_back(&running_ult_list);
        should_change_thread = 0;
        // printf("[scheduler] rotate from %lu to %lu\n", running_ult_list.tail->ult->id, running_ult_list.head->ult->id); fflush(NULL);
    }

    if (running_ult_list.size == 0) {
        find_deadlocks();
        BAIL("There are no running threads! This might indicate that a deadlock that involves all existing threads occured!");
    }

    ult_t* thread = running_ult_list.head->ult;
    while (/*thread != NULL &&*/ thread->status != RUNNING) { // if all threads are sleeping this loop will repeat until one wakes up
        if (thread->status == SLEEPING) {
            // printf("[scheduler] %lu is sleeping\n", thread->id); fflush(NULL);
            struct timespec current_time;

            if (clock_gettime(SLEEP_CLOCK, &current_time) == -1) {
                BAIL("Get Time");
            }

            const uint64_t ns_in_sec = 1000000000;
            uint64_t elapsed = (current_time.tv_sec - thread->sleep_time.tv_sec) * ns_in_sec + (current_time.tv_nsec - thread->sleep_time.tv_nsec);
            
            if (elapsed > thread->sleep_amount_nsec) {
                // the thread should wake up
                thread->status = RUNNING;
                // printf("[scheduler] waking up %lu\n", thread->id); fflush(NULL);
            }
            else {
                // the thread should remain sleeping
                // printf("[scheduler] %lu reamains sleeping\n", thread->id); fflush(NULL);
                rotate_ult_front_to_back(&running_ult_list);
                thread = running_ult_list.head->ult;
                // printf("[scheduler] rotate from %lu to %lu\n", running_ult_list.tail->ult->id, running_ult_list.head->ult->id); fflush(NULL);
            }
        }
    }

    end_protected_zone(); // set signal handlers before switch

    // printf("[scheduler] switch to %lu\n", running_ult_list.head->ult->id); fflush(NULL);
    // no need to swap if the current thread is the next scheduled for execution
    if (current->id != thread->id) {
        if (swapcontext(&(current->context), &(thread->context)) != 0) 
            BAIL("Swapcontext scheduler");
    }
}

static inline void wrapper_exit(ult_t* current, void* result) {
    start_protected_zone();

    current->result = result;
    current->status = FINISHED;

    // remove the current thread from the running list
    delete_ult_first(&running_ult_list);    // in theory the current thread is the first
    // the memory is freed after join

    if (current->joined_by != NULL) {
        printf("[%lu] wrapper, change status of (%lu) to RUNNING\n", current->id, current->joined_by->id); fflush(NULL);
        current->joined_by->status = RUNNING; // wake the thread waiting to join the current thread, without changing the run order
        current->joined_by->waiting_to_join = NULL;
        insert_ult_last(&running_ult_list, current->joined_by);
    }

    printf("[%lu] wrapper exit\n", current->id); fflush(NULL);

    SCHEDULER(current);
    end_protected_zone();
}

void wrapper() {
    ult_t* current = running_ult_list.head->ult;
    void* result;

    printf("[%lu] wrapper enter\n", current->id); fflush(NULL);
    result = current->start_routine(current->arg);
    printf("[%lu] routine finished\n", current->id); fflush(NULL);

    wrapper_exit(current, result);
}

void sig_handler(int signum, siginfo_t *si, void *uc) {
    // printf("[handler %lu] received %d, protect: %s\n", running_ult_list.head->ult->id, signum, inside_protected_zone? "true": "false"); fflush(NULL);

    if (inside_protected_zone && signum != DEADLOCK_SIG) {
        // printf("[handler] IGNORE %d\n", signum); fflush(NULL);
        return;
    }

    switch (signum) {
        case TIMER_SIG:
            should_change_thread = 1;
            SCHEDULER(running_ult_list.head->ult);
            break;

        case DEADLOCK_SIG:
            find_deadlocks();
            break;
    }
}

static void init_main() {
    uint64_t id = 1;
    ult_counter = 1;

    init_ult(&main_ult, id, NULL, NULL);
    init_ult_context(&main_ult, NULL); // when main is done the entire program is done, no cleanup will be done after

    insert_ult_last(&running_ult_list, &main_ult);
}

static void init_timer() {
    timer_t             timerid;
    struct sigevent     sev;
    struct itimerspec   its;

    // Set up the timer
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = TIMER_SIG;
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

static void init_signals() {
    // prepare signal masks
    sigemptyset(&mask_sig);
    sigaddset(&mask_sig, TIMER_SIG);
    // sigaddset(&mask_sig, DEADLOCK_SIG);
    sigemptyset(&no_mask);

    // setup signal handlers
    struct sigaction sa;

    sa.sa_sigaction = sig_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(TIMER_SIG, &sa, NULL);
    sigaction(DEADLOCK_SIG, &sa, NULL);
}

static inline void init_lib() {
    if (ult_counter == 0) {
        printf("Initializing library\n");
        init_ult_linked_list(&running_ult_list);
        init_ult_linked_list(&not_finished_ults);

        // this is the first call to the library
        init_signals();
        init_timer();
        init_main(); // we initialize main after initializing all other lib parts to make sure that we don't execute the code twice
    }
}

// 'public' members

int ult_create(ult_t* thread, voidptr_arg_voidptr_ret_func start_routine, void* arg) {
    init_lib();

    ult_t* running = running_ult_list.head->ult;
    printf("[%lu] create: %lu\n", running->id, ult_counter + 1); fflush(NULL);

    start_protected_zone(); // protect this area from being interrupted
        ult_counter += 1;
        uint64_t id = ult_counter;
    end_protected_zone(); // end of protected zone

    init_ult(thread, id, start_routine, arg);
    init_ult_context(thread, NULL);    // when done return to the scheduler

    // cast wraper to a function without parameters that returns void (void (*)(void)) to avoid compiler warning if wrapper has parameters
    makecontext(&(thread->context), wrapper, 0);  // passing pointers as parameters to makecontext might not be portable, so we save the parameters in ult_data
        
    start_protected_zone();
        insert_ult_last(&running_ult_list, thread);
        insert_ult_last(&not_finished_ults, thread);
    end_protected_zone();

    // TODO: maybe it would be more 'fair' to call swap
    return 0;
}

int ult_join(ult_t* thread, void** retval) {
    init_lib();

    start_protected_zone();

    ult_t* current_waiting_join = running_ult_list.head->ult;

    printf("[%lu] waiting to join: %lu\n", current_waiting_join->id, thread->id); fflush(NULL);

    if (thread->joined_by != NULL) {
        // the thread is already being waited by some other thread
        printf("[%lu] %lu was already waited by %lu\n", current_waiting_join->id, thread->id, thread->joined_by->id); fflush(NULL);

        end_protected_zone();
        return 2;
    }

    if (thread->status != FINISHED) {
        printf("[%lu] %lu did not finish yet\n", current_waiting_join->id, thread->id); fflush(NULL);

        if (thread->joined_by != NULL) {
            // the thread is already being waited by some other thread
            printf("[%lu] %lu is being waited by %lu\n", current_waiting_join->id, thread->id, thread->joined_by->id); fflush(NULL);

            end_protected_zone();
            return 2;
        }

        thread->joined_by = current_waiting_join;
        current_waiting_join->status = WAITING;
        current_waiting_join->waiting_to_join = thread;
        delete_ult_first(&running_ult_list); // remove the current thread from the running list

        SCHEDULER(current_waiting_join); // the scheduler would set the signals back
        start_protected_zone();
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

    end_protected_zone();

    return 0;
}

void ult_sleep(uint64_t sec, uint64_t nsec) {
    ult_t* current = running_ult_list.head->ult;

    printf("[%lu] sleep\n", current->id); fflush(NULL);

    if (clock_gettime(SLEEP_CLOCK, &(current->sleep_time)) == -1) {
        BAIL("Get Time");
    }

    current->sleep_amount_nsec = sec * 1000000000 + nsec;
    current->status = SLEEPING;

    should_change_thread = 1;
    SCHEDULER(current);
}

uint64_t ult_get_id() {
    init_lib();
    return running_ult_list.head->ult->id;
}

void ult_exit(void* retval) {
    init_lib();

    ult_t* current = running_ult_list.head->ult;

    wrapper_exit(current, retval);
}

int ult_mutex_init(ult_mutex_t* mutex) {
    init_lib();

    start_protected_zone();
        mutex_counter += 1;
        uint64_t id = mutex_counter;
    end_protected_zone();

    mutex->id = id;
    mutex->owner = NULL;
    init_ult_linked_list(&(mutex->waiting));

    return 0;
}

int ult_mutex_destroy(ult_mutex_t* mutex) {
    init_lib();

    start_protected_zone();

    if (mutex->owner != NULL) {
        end_protected_zone();
        return 1;
    }

    destroy_ult_list(&(mutex->waiting));

    end_protected_zone();

    return 0;
}

int ult_mutex_lock(ult_mutex_t* mutex) {
    init_lib();

    start_protected_zone();

    ult_t* current = running_ult_list.head->ult;

    if (mutex->owner == NULL) {
        // the mutex is free
        printf("[%lu] mutex %lu is free\n", current->id, mutex->id); fflush(NULL);
        mutex->owner = current;
        end_protected_zone();
        return 0;
    }

    if (mutex->owner->id == current->id) { // comapring the pointers should also work, but for corectness comapre ids
        // the mutex is held by the running thread
        printf("[%lu] mutex %lu is already held by me\n", current->id, mutex->id); fflush(NULL);
        end_protected_zone();
        return 0;
    }

    printf("[%lu] mutex %lu is held by %lu\n", current->id, mutex->id, mutex->owner->id); fflush(NULL);

    // the current thread should wait
    insert_ult_last(&(mutex->waiting), current);
    current->status = WAITING;
    current->waiting_mutex = mutex;
    delete_ult_first(&running_ult_list);

    printf("[%lu] switched to WAITING at mutex %lu\n", current->id, mutex->id); fflush(NULL);

    SCHEDULER(current); // the scheduler will reset the signals

    end_protected_zone();
    
    return 0;
}

int ult_mutex_unlock(ult_mutex_t* mutex) {
    init_lib();

    start_protected_zone();

    ult_t* current = running_ult_list.head->ult;

    if (mutex == NULL || mutex->owner == NULL || mutex->owner->id != current->id) {
        // exit with error if the mutex doesn't exist, has no owner or the current thread is not the owner
        end_protected_zone();
        return 1;
    }

    // current thread frees the mutex
    mutex->owner = NULL;

    // if there are threads waiting for this mutex
    if (mutex->waiting.size > 0) { 
        // pass the ownership to the next thread in the waiting list
        mutex->owner = mutex->waiting.head->ult;
        delete_ult_first(&(mutex->waiting));
        printf("[%lu] Mutex Wake up %lu\n", current->id, mutex->owner->id); fflush(NULL);
        // if there is a thread waiting, WAKE IT UP!
        mutex->owner->status = RUNNING;
        mutex->owner->waiting_mutex = NULL;
        insert_ult_last(&running_ult_list, mutex->owner);
    }

    end_protected_zone();
    
    return 0;
}

int ult_cond_init(ult_cond_t* cond) {
    init_lib();

    start_protected_zone();
        cond_counter += 1;
        uint64_t id = cond_counter;
    end_protected_zone();

    cond->id = id;
    init_ult_linked_list(&(cond->waiting));

    return 0;
}

int ult_cond_destroy(ult_cond_t* cond) {
    init_lib();

    start_protected_zone();

    if (cond->waiting.size != 0) {
        end_protected_zone();
        return 1;
    }

    destroy_ult_list(&(cond->waiting));

    end_protected_zone();

    return 0;
}

int ult_cond_wait(ult_cond_t* cond, ult_mutex_t* mutex) {
    init_lib();

    start_protected_zone();

    // we cannot use ult_mutex_unlock as it would break the protection zone, 
    // and it is needed that the mutex unocking and waiting to be done atomically
    // ult_mutex_unlock(mutex);

    ult_t* current = running_ult_list.head->ult;

    // unlock the mutex atomically with waiting to make sure that no signals are missed
    if (mutex != NULL && mutex->owner != NULL && mutex->owner->id == current->id) {
        mutex->owner = NULL;

        if (mutex->waiting.size > 0) { 
            mutex->owner = mutex->waiting.head->ult;
            delete_ult_first(&(mutex->waiting));
            printf("[%lu] Mutex Wake up (cond) %lu\n", current->id, mutex->owner->id); fflush(NULL);
            mutex->owner->status = RUNNING;
            mutex->owner->waiting_mutex = NULL;
            insert_ult_last(&running_ult_list, mutex->owner);
        }
    }

    insert_ult_last(&(cond->waiting), current);
    current->status = WAITING;
    current->waiting_cond = cond;
    delete_ult_first(&running_ult_list);

    printf("[%lu] switched to WAITING at cond var %lu\n", current->id, cond->id); fflush(NULL);

    SCHEDULER(current);

    ult_mutex_lock(mutex);

    end_protected_zone();

    return 0;
}

int ult_cond_signal(ult_cond_t* cond) {
    init_lib();

    start_protected_zone();

    if (cond->waiting.size == 0) {
        end_protected_zone();
        return 1;
    }

    ult_t* ult_to_start = cond->waiting.head->ult;
    delete_ult_first(&(cond->waiting));
    ult_to_start->status = RUNNING;
    ult_to_start->waiting_cond = NULL;
    insert_ult_last(&running_ult_list, ult_to_start);

    end_protected_zone();

    return 0;
}

int ult_cond_broadcast(ult_cond_t* cond) {
    init_lib();

    start_protected_zone();

    while (cond->waiting.size != 0) {
        ult_t* ult_to_start = cond->waiting.head->ult;
        delete_ult_first(&(cond->waiting));
        ult_to_start->status = RUNNING;
        ult_to_start->waiting_cond = NULL;
        insert_ult_last(&running_ult_list, ult_to_start);
    }

    end_protected_zone();

    return 0;
}
