#include "debugger.h"
#include <arpa/inet.h>

static void unlock_mutex(pthread_mutex_t *mutex) {
    pthread_mutex_unlock(mutex);
}

static void waitpid_thread(TinyDbg *handle) {
    bool first_time = true;

    pid_t my_pid = handle->pid;
    pthread_cleanup_push((void (*)(void *))unlock_mutex, &handle->process_continued);

    while (true) {
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  // only cancel when waitpiding
        pthread_mutex_lock(&handle->process_continued);  // wait for process to be continued, which is when the lock is unlocked

        int wstatus;
        waitpid(my_pid, &wstatus, 0);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        pthread_mutex_unlock(&handle->process_continued);
        TinyDbg_procman_request *event = malloc(sizeof(TinyDbg_procman_request));
        event->type = TinyDbg_INTERNAL_procman_request_type_waitpid;
        event->content = (void *)(size_t)wstatus;
        EventQueue_join(EventQueue_add_joinable(handle->eq_process_manager, event));  // let the manager lock now
    }

    pthread_cleanup_pop(0);
}

struct process_manager_thread_args {
    TinyDbg *handle;
    const char *filename;
    char *const *argv;
    char *const *envp;
};

static void process_manager_thread(struct process_manager_thread_args *args) {
    TinyDbg *handle = args->handle;
    const char *filename = args->filename;
    char *const *argv = args->argv;
    char *const *envp = args->envp;
    free(args);

    pid_t child_pid = vfork();
    if (child_pid == 0) {
        // am child
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execve(filename, argv, envp);
    } else {
        bool is_stopped = true;
        pthread_mutex_lock(&handle->process_continued);  // process is currently stopped
        handle->pid = child_pid;
        waitpid(child_pid, NULL, 0);
        ptrace(PTRACE_SETOPTIONS, child_pid, NULL, PTRACE_O_EXITKILL);  // don't let the traced process run after i'm done

        EventQueue_Consumer *consumer = EventQueue_new_consumer(handle->eq_process_manager);
        void *data; EventQueue_consume(consumer, &data);  // i'm ready now, the creator should push an empty event

        while (true) {
            TinyDbg_procman_request *data;
            if (EventQueue_consume(consumer, (void **)(&data)) == 'K') return EventQueue_destroy_consumer(consumer);
            if (data->type == TinyDbg_procman_request_type_continue) {
                // check wheter this was at a breakpoint, if so delete it
                ptrace(PTRACE_CONT, handle->pid, 0, 0);
                is_stopped = false;
                pthread_mutex_unlock(&handle->process_continued);  // process is currently continued
            } else if (data->type == TinyDbg_INTERNAL_procman_request_type_waitpid) {
                // got a stop code
                int wstatus = (int)(size_t)data->content;
                if (WIFEXITED(wstatus)) {
                    // process is done
                    TinyDbg_Event *dbg_event = malloc(sizeof(TinyDbg_Event));
                    dbg_event->type = TinyDbg_event_type_exit;
                    dbg_event->content.stop_code = WEXITSTATUS(wstatus);
                    EventQueue_add(handle->eq_debugger_events, dbg_event);
                } else if (WIFSTOPPED(wstatus)) {
                    // process has stopped
                    is_stopped = true;
                    pthread_mutex_lock(&handle->process_continued);  // process is currently supposed to be stopped

                    // check whether this was a breakpoint
                    struct user_regs_struct regs;
                    ptrace(PTRACE_GETREGS, handle->pid, 0, &regs);
                    regs.rip--;

                    pthread_mutex_lock(&handle->breakpoint_lock);
                    bool found_breakpoint = false;
                    TinyDbg_Breakpoint breakpoint;
                    for (size_t i = 0; i < handle->breakpoints_len; i++) {
                        if (handle->breakpoint_positions[i] == regs.rip) {
                            // found!
                            found_breakpoint = true;
                            breakpoint = handle->breakpoints[i];
                            // if the breakpoint is set to be once, delete it from the list
                            if (breakpoint.is_once) {
                                for (int j = i; j < handle->breakpoints_len - 1; j++) {
                                    handle->breakpoint_positions[j] = handle->breakpoint_positions[j + 1];
                                }
                                for (int j = i; j < handle->breakpoints_len - 1; j++) {
                                    handle->breakpoints[j] = handle->breakpoints[j + 1];
                                }
                                handle->breakpoints_len--;
                            }
                            break;
                        }
                    }
                    pthread_mutex_unlock(&handle->breakpoint_lock);

                    TinyDbg_Event *dbg_event = malloc(sizeof(TinyDbg_Event));
                    if (found_breakpoint) {
                        // we stopped on a breakpoint!!!
                        dbg_event->type = TinyDbg_event_type_breakpoint;
                        dbg_event->content.breakpoint = breakpoint;
                        EventQueue_add(handle->eq_debugger_events, dbg_event);
                        // change the rip so it's just before the breakpoint
                        ptrace(PTRACE_SETREGS, handle->pid, 0, &regs);
                        // revert the first instruction
                        unsigned long mem = ptrace(PTRACE_PEEKTEXT, handle->pid, regs.rip, NULL);
                        ((char *)(&mem))[0] = breakpoint.original;
                        ptrace(PTRACE_POKETEXT, handle->pid, regs.rip, mem);
                    } else {
                        dbg_event->type = TinyDbg_event_type_stop;
                        dbg_event->content.stop_code = WSTOPSIG(wstatus);
                        EventQueue_add(handle->eq_debugger_events, dbg_event);
                    }
                }
            } else if (data->type == TinyDbg_procman_request_type_stop
                    || data->type == TinyDbg_procman_request_type_get_regs
                    || data->type == TinyDbg_procman_request_type_set_regs
                    || data->type == TinyDbg_procman_request_type_get_mem
                    || data->type == TinyDbg_procman_request_type_set_mem
                    || data->type == TinyDbg_procman_request_type_set_breakp
                    || data->type == TinyDbg_procman_request_type_singlestep) {  // stuff which needs stopping first
                
                if (!is_stopped) {
                    pthread_cancel(handle->waiter_thread); pthread_join(handle->waiter_thread, NULL);  // stop the waitpiding
                    pthread_mutex_lock(&handle->process_continued);  // process is currently supposed to be stopped
                    kill(handle->pid, SIGSTOP);
                    waitpid(handle->pid, NULL, 0);  // wait for it to stop
                }

                if (data->type == TinyDbg_procman_request_type_get_regs) {
                    ptrace(PTRACE_GETREGS, handle->pid, 0, data->content);
                } else if (data->type == TinyDbg_procman_request_type_set_regs) {
                    ptrace(PTRACE_SETREGS, handle->pid, 0, data->content);
                } else if (data->type == TinyDbg_procman_request_type_get_mem) {
                    TinyDbg_procman_request_get_mem *x = data->content;
                    process_vm_readv(handle->pid, &x->local_iov, 1, &x->remote_iov, 1, 0);
                    free(x);
                } else if (data->type == TinyDbg_procman_request_type_set_mem) {
                    TinyDbg_procman_request_set_mem *x = data->content;
                    process_vm_writev(handle->pid, &x->local_iov, 1, &x->remote_iov, 1, 0);
                    free(x);
                } else if (data->type == TinyDbg_procman_request_type_set_breakp) {
                    // set a breakpoint!
                    TinyDbg_procman_request_set_breakp *x = data->content;
                    TinyDbg_Breakpoint my_breakpoint;
                    my_breakpoint.is_once = x->is_once;

                    unsigned long data_at_position = ptrace(PTRACE_PEEKTEXT, handle->pid, x->position, NULL);
                    my_breakpoint.original = ((char *)(&data_at_position))[0];
                    ((char *)(&data_at_position))[0] = '\xcc';
                    ptrace(PTRACE_POKETEXT, handle->pid, x->position, data_at_position);  // write this with the \xcc in there

                    // write this breakpoint
                    pthread_mutex_lock(&handle->breakpoint_lock);
                    handle->breakpoints_len++;

                    handle->breakpoint_positions = realloc(handle->breakpoint_positions, sizeof(uintptr_t) * handle->breakpoints_len);
                    handle->breakpoint_positions[handle->breakpoints_len - 1] = x->position;

                    handle->breakpoints = realloc(handle->breakpoints, sizeof(TinyDbg_Breakpoint) * handle->breakpoints_len);
                    handle->breakpoints[handle->breakpoints_len - 1] = my_breakpoint;

                    pthread_mutex_unlock(&handle->breakpoint_lock);
                    free(x);
                } else if (data->type == TinyDbg_procman_request_type_singlestep) {
                    ptrace(PTRACE_SINGLESTEP, handle->pid, NULL, NULL);
                    waitpid(handle->pid, NULL, 0);
                }

                if (!is_stopped) {
                    pthread_create(&handle->waiter_thread, NULL, (void * (*)(void *))&waitpid_thread, handle);  // restart the waitpiding
                    if (data->type != TinyDbg_procman_request_type_stop) {
                        ptrace(PTRACE_CONT, handle->pid, 0, 0);
                    } else {
                        is_stopped = true;
                    }
                }
                // TODO what if this causes a race condition where the process continues before the thread is waitpid-ing?
                pthread_mutex_unlock(&handle->process_continued);
            }

            free(data);
        }
    }
}

TinyDbg *TinyDbg_start(const char *filename, char *const argv[], char *const envp[]) {
    // create the object
    TinyDbg *result = calloc(1, sizeof(TinyDbg));

    result->breakpoint_positions = malloc(0);
    result->breakpoints = malloc(0);
    result->breakpoints_len = 0;
    pthread_mutex_init(&result->breakpoint_lock, NULL);

    pthread_mutex_init(&result->process_continued, NULL);
    result->eq_process_manager = EventQueue_new();
    result->eq_debugger_events = EventQueue_new();

    struct process_manager_thread_args *procman_args = malloc(sizeof(struct process_manager_thread_args));
    procman_args->handle = result;
    procman_args->filename = filename;
    procman_args->argv = argv;
    procman_args->envp = envp;

    pthread_create(&result->process_manager_thread, NULL, (void * (*)(void *))&process_manager_thread, procman_args);
    // send empty join to process_manager_thread, it will return only once it's done ptracing
    EventQueue_join(EventQueue_add_joinable(result->eq_process_manager, NULL));

    // create the waiter thread, now that the pid is defined
    pthread_create(&result->waiter_thread, NULL, (void * (*)(void *))&waitpid_thread, result);

    return result;
}

void TinyDbg_free(TinyDbg *handle) {
    free(handle->breakpoint_positions);
    free(handle->breakpoints);
    pthread_mutex_destroy(&handle->breakpoint_lock);

    pthread_cancel(handle->waiter_thread);
    pthread_join(handle->waiter_thread, NULL);

    EventQueue_free(handle->eq_debugger_events);
    EventQueue_free(handle->eq_process_manager);

    pthread_join(handle->process_manager_thread, NULL);
    pthread_mutex_destroy(&handle->process_continued);

    free(handle);
}

static EventQueue_JoinHandle *TinyDbg_send_procman_request(TinyDbg *handle, TinyDbg_procman_request_type type, void *content) {
    TinyDbg_procman_request *data = calloc(1, sizeof(TinyDbg_procman_request));
    data->type = type;
    data->content = content;
    return EventQueue_add_joinable(handle->eq_process_manager, data);
}

// Wrappers for the previous function
EventQueue_JoinHandle *TinyDbg_stop(TinyDbg *handle) {
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_stop, NULL);
}
EventQueue_JoinHandle *TinyDbg_continue(TinyDbg *handle) {
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_continue, NULL);
}
EventQueue_JoinHandle *TinyDbg_singlestep(TinyDbg *handle) {
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_singlestep, NULL);
}
EventQueue_JoinHandle *TinyDbg_get_registers(TinyDbg *handle, struct user_regs_struct *save_to) {
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_get_regs, save_to);
}
EventQueue_JoinHandle *TinyDbg_set_registers(TinyDbg *handle, struct user_regs_struct *take_from) {
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_set_regs, take_from);
}
EventQueue_JoinHandle *TinyDbg_get_memory(TinyDbg *handle, struct iovec local_iov, struct iovec remote_iov) {
    TinyDbg_procman_request_get_mem *x = malloc(sizeof(TinyDbg_procman_request_get_mem));
    x->local_iov = local_iov;
    x->remote_iov = remote_iov;
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_get_mem, x);
}
EventQueue_JoinHandle *TinyDbg_set_memory(TinyDbg *handle, struct iovec local_iov, struct iovec remote_iov) {
    TinyDbg_procman_request_set_mem *x = malloc(sizeof(TinyDbg_procman_request_get_mem));
    x->local_iov = local_iov;
    x->remote_iov = remote_iov;
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_set_mem, x);
}

EventQueue_JoinHandle *TinyDbg_set_breakpoint(TinyDbg *handle, uintptr_t position, bool is_once) {
    TinyDbg_procman_request_set_breakp *x = malloc(sizeof(TinyDbg_procman_request_set_breakp));
    x->position = position;
    x->is_once = is_once;
    return TinyDbg_send_procman_request(handle, TinyDbg_procman_request_type_set_breakp, x);
}

void TinyDbg_Event_free(TinyDbg_Event *event) {
    free(event);
}
