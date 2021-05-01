#include "debugger.h"

static inline void event_append(TinyDbg *handle, struct TinyDbg_Event *new_event) {
    pthread_mutex_lock(&handle->event_lock);
    if (handle->event_queue.head == NULL) {
        handle->event_queue.head = new_event;
        handle->event_queue.tail = new_event;
    } else {
        handle->event_queue.tail->next = new_event;
        handle->event_queue.tail = new_event;
    }
    pthread_mutex_unlock(&handle->event_lock);
}

static void waitpid_thread(TinyDbg *handle) {
    pid_t my_pid = handle->pid;
    while (true) {
        pthread_mutex_lock(&handle->process_continued);  // wait for process to be continued
        int wstatus;
        waitpid(my_pid, &wstatus, 0);
        struct TinyDbg_Event *new_event = calloc(1, sizeof(struct TinyDbg_Event));
        new_event->type = TinyDbg_EVENT_STOP;  // TODO check if it's a breakpoint
        // create a pointer to wstatus, and put it in the event
        int *wstatus_ptr = malloc(sizeof(int));
        *wstatus_ptr = wstatus;
        new_event->data = wstatus_ptr;
        // add to the event queue
        event_append(handle, new_event);
    }
}

TinyDbg *TinyDbg_start(const char *filename, char *const argv[], char *const envp[]) {
    int child_pid = vfork();  // like fork but blocking, and without cloning anything, for efficiency
    if (child_pid == 0) {
        // am child
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        execve(filename, argv, envp);
        // execve does not return
    } else {
        // am parent
        // wait for process to be ready
        waitpid(child_pid, NULL, 0);
        // create object
        TinyDbg *result = malloc(sizeof(TinyDbg));
        pthread_mutex_init(&result->event_lock, NULL);
        pthread_mutex_init(&result->process_continued, NULL);
        pthread_mutex_lock(&result->process_continued);
        result->event_queue.head = NULL;
        result->event_queue.tail = NULL;
        result->pid = child_pid;
        // create waiter thread
        pthread_create(&result->waiter_thread, NULL, (void * (*)(void *))&waitpid_thread, result);
        return result;
    }
}

void TinyDbg_free(TinyDbg *handle) {
    pthread_cancel(handle->waiter_thread);              // close waiter thread
    pthread_mutex_unlock(&handle->process_continued);   // if waiting on the process to continue, don't
    pthread_join(handle->waiter_thread, NULL);          // wait for it to close
    pthread_mutex_destroy(&handle->event_lock);         // destroy the event mutex
    pthread_mutex_destroy(&handle->process_continued);  // destroy the process_continued mutex
    // free all events
    struct TinyDbg_Event *event = handle->event_queue.head;
    while (event != NULL) {
        struct TinyDbg_Event *next = event->next;
        TinyDbg_Event_free(event);
        event = next;
    }
    free(handle);
}

struct user_regs_struct TinyDbg_get_registers(TinyDbg *handle) {
    struct user_regs_struct regs;
    ptrace(PTRACE_GETREGS, handle->pid, 0, &regs);
    return regs;
}

void TinyDbg_set_regsisters(TinyDbg *handle, struct user_regs_struct regs) {
    ptrace(PTRACE_SETREGS, handle->pid, 0, &regs);
}

int TinyDbg_read_memory(TinyDbg *handle, void *dest, void *src, size_t amount) {
    struct iovec iov_remote = {src, amount};
    struct iovec iov_local = {dest, amount};
    return process_vm_readv(handle->pid, &iov_local, 1, &iov_remote, 1, 0);
}

int TinyDbg_write_memory(TinyDbg *handle, void *dest, void *src, size_t amount) {
    struct iovec iov_local = {src, amount};
    struct iovec iov_remote = {dest, amount};
    return process_vm_writev(handle->pid, &iov_local, 1, &iov_remote, 1, 0);
}

int TinyDbg_set_breakpoint_once(TinyDbg *handle, void *ip) {
    char original;
    char int3 = '\x03';
    if (TinyDbg_read_memory(handle, &original, ip, 1) == -1) return -1;  // read original content
    if (TinyDbg_write_memory(handle, ip, &int3, 1) == -1) return -2;     // write the int3
    // TODO write this to a list of breakpoints, so we can know whether it's for one time or not
    return 0;
}

void TinyDbg_continue(TinyDbg *handle) {
    // lock process_continued again, worst case scenario you get EDEADLK for already owning it
    pthread_mutex_trylock(&handle->process_continued);
    pthread_mutex_unlock(&handle->process_continued);  // unlock it for real now
    ptrace(PTRACE_CONT, handle->pid, 0, 0);
}

struct TinyDbg_Event *TinyDbg_get_event(TinyDbg *handle) {
    pthread_mutex_lock(&handle->event_lock);
    // pop the first event
    struct TinyDbg_Event *current_head = handle->event_queue.head;
    if (current_head == NULL) {
        pthread_mutex_unlock(&handle->event_lock);
        return NULL;
    }

    handle->event_queue.head = current_head->next;

    if (current_head->next == NULL) {
        // the queue is empty now
        handle->event_queue.tail = NULL;
    }

    pthread_mutex_unlock(&handle->event_lock);
    return current_head;
}
