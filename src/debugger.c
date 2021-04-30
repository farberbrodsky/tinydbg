#include "debugger.h"

static void waiter_thread(TinyDbg *handle) {
    pid_t my_pid = handle->pid;
    while (true) {
        int wstatus;
        waitpid(my_pid, &wstatus, WUNTRACED);
        struct TinyDbg_Event *new_event = calloc(1, sizeof(struct TinyDbg_Event));
        new_event->type = TinyDbg_EVENT_STOP;  // TODO check if it's a breakpoint
        // create a pointer to wstatus, and put it in the event
        int *wstatus_ptr = malloc(sizeof(int));
        *wstatus_ptr = wstatus;
        new_event->data = wstatus_ptr;
        // add to the event queue
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
        result->event_queue.head = NULL;
        result->event_queue.tail = NULL;
        result->pid = child_pid;
        // create waiter thread
        pthread_create(&result->waiter_thread, NULL, (void * (*)(void *))&waiter_thread, result);
        return result;
    }
}

void TinyDbg_free(TinyDbg *handle) {
    pthread_cancel(handle->waiter_thread);      // close waiter thread
    pthread_mutex_destroy(&handle->event_lock); // destroy the event mutex
    // free all events
    struct TinyDbg_Event *event = handle->event_queue.head;
    while (event != NULL) {
        struct TinyDbg_Event *next = event->next;
        TinyDbg_event_free(event);
        event = next;
    }
    free(handle);
}
