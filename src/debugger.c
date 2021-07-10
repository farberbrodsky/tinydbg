#include "debugger.h"

static void waitpid_thread(TinyDbg *handle) {
    pid_t my_pid = handle->pid;
    while (true) {
        pthread_mutex_lock(&handle->process_continued);  // wait for process to be continued, which is when the lock is unlocked
        int wstatus;
        waitpid(my_pid, &wstatus, 0);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if (WIFEXITED(wstatus)) {
            // debugged program is done
            TinyDbg_Event *event = malloc(sizeof(TinyDbg_Event));
            event->type = TinyDbg_event_type_exit;
            event->content.stop_code = WEXITSTATUS(wstatus);
            EventQueue_add(handle->eq_debugger_events, event);
        }
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    }
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
                // continue
                ptrace(PTRACE_CONT, handle->pid, 0, 0);
                pthread_mutex_unlock(&handle->process_continued);  // process is currently continued
            }
            if (data->type == TinyDbg_procman_request_type_stop) {
                // send a sigstop
                kill(handle->pid, SIGSTOP);
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
    // TODO free all breakpoints
    free(handle->breakpoints);

    pthread_mutex_destroy(&handle->process_continued);
    EventQueue_free(handle->eq_process_manager);
    EventQueue_free(handle->eq_debugger_events);

    pthread_cancel(handle->waiter_thread);
    pthread_cancel(handle->process_manager_thread);
    free(handle);
}

static EventQueue_JoinHandle *TinyDbg_send_empty_procman_request(TinyDbg *handle, TinyDbg_procman_request_type type) {
    TinyDbg_procman_request *data = calloc(1, sizeof(TinyDbg_procman_request));
    data->type = type;
    return EventQueue_add_joinable(handle->eq_process_manager, data);
}

// Wrappers for the previous function
EventQueue_JoinHandle *TinyDbg_stop(TinyDbg *handle) {
    return TinyDbg_send_empty_procman_request(handle, TinyDbg_procman_request_type_stop);
}
EventQueue_JoinHandle *TinyDbg_continue(TinyDbg *handle) {
    return TinyDbg_send_empty_procman_request(handle, TinyDbg_procman_request_type_continue);
}
