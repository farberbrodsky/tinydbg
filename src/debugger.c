#include "debugger.h"
#include <errno.h>

static inline void event_append(TinyDbg *handle, struct TinyDbg_Event *new_event) {
    pthread_mutex_lock(&handle->event_lock);
    pthread_cond_broadcast(&handle->event_ready);
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
        bool is_breakpoint = false;
        if (wstatus == 2943) {
            // should be a breakpoint, search for the info about it
            struct user_regs_struct regs = TinyDbg_get_registers(handle);
            void *ip = (void *)regs.rip;
            pthread_mutex_lock(&handle->breakpoint_lock);
            int i = 0;
            for (; i < handle->breakpoints_len; i++) {
                printf("Test against %p\n", ip);
                if (handle->breakpoints[i].position == ip) {
                    is_breakpoint = true;
                    break;
                }
            }
            if (is_breakpoint) {
                new_event->type = TinyDbg_EVENT_BREAKPOINT;
                void *breakpoint_clone = malloc(sizeof(TinyDbg_Breakpoint));
                memcpy(breakpoint_clone, &handle->breakpoints[i], sizeof(TinyDbg_Breakpoint));
                new_event->data = breakpoint_clone;
            }
            pthread_mutex_unlock(&handle->breakpoint_lock);
        }
        if (!is_breakpoint) {
            new_event->type = TinyDbg_EVENT_STOP;
            // create a pointer to wstatus, and put it in the event
            int *wstatus_ptr = malloc(sizeof(int));
            *wstatus_ptr = wstatus;
            new_event->data = wstatus_ptr;
        }
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
        result->pid = child_pid;
        // events
        pthread_mutex_init(&result->event_lock, NULL);
        pthread_cond_init(&result->event_ready, NULL);
        pthread_mutex_init(&result->process_continued, NULL);
        pthread_mutex_lock(&result->process_continued);
        result->event_queue.head = NULL;
        result->event_queue.tail = NULL;
        // breakpoints
        pthread_mutex_init(&result->breakpoint_lock, NULL);
        result->breakpoints_len = 0;
        result->breakpoint_positions = malloc(0);
        result->breakpoints = malloc(0);
        // create waiter thread
        pthread_create(&result->waiter_thread, NULL, (void * (*)(void *))&waitpid_thread, result);
        return result;
    }
}

void TinyDbg_free(TinyDbg *handle) {
    // events
    pthread_cancel(handle->waiter_thread);              // close waiter thread
    pthread_mutex_unlock(&handle->process_continued);   // if waiting on the process to continue, don't
    pthread_join(handle->waiter_thread, NULL);          // wait for it to close
    pthread_mutex_destroy(&handle->event_lock);         // destroy the event mutex
    pthread_cond_destroy(&handle->event_ready);
    pthread_mutex_destroy(&handle->process_continued);  // destroy the process_continued mutex
    // free all events
    struct TinyDbg_Event *event = handle->event_queue.head;
    while (event != NULL) {
        struct TinyDbg_Event *next = event->next;
        TinyDbg_Event_free(event);
        event = next;
    }
    // breakpoints, should be ok to destroy because other threads are already down
    pthread_mutex_destroy(&handle->breakpoint_lock);
    handle->breakpoints_len = 0;
    // NOTE right now breakpoints don't have any malloc'd pointers, but if they do we'd need to free them
    free(handle->breakpoint_positions);
    free(handle->breakpoints);
    // finishing touches
    free(handle);
}

struct user_regs_struct TinyDbg_get_registers(TinyDbg *handle) {
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGS, handle->pid, 0, &regs) == -1) {
        printf("PANIC!\n");
        exit(1);
    }
    return regs;
}

void TinyDbg_set_regsisters(TinyDbg *handle, struct user_regs_struct regs) {
    ptrace(PTRACE_SETREGS, handle->pid, 0, &regs);
}

int TinyDbg_read_memory(TinyDbg *handle, void *dest, void *src, size_t amount) {
    struct iovec iov_remote = {src, amount};
    struct iovec iov_local = {dest, amount};
    pthread_mutex_lock(&handle->breakpoint_lock);
    int result = process_vm_readv(handle->pid, &iov_local, 1, &iov_remote, 1, 0);
    pthread_mutex_unlock(&handle->breakpoint_lock);
    if (result < 0) return result;
    return 0;
}

/*
int TinyDbg_write_memory(TinyDbg *handle, void *dest, void *src, size_t amount) {
    struct iovec iov_local = {src, amount};
    struct iovec iov_remote = {dest, amount};
    // it currently results with EFAULT, which means it's out of the address space, idk why
    return process_vm_writev(handle->pid, &iov_local, 1, &iov_remote, 1, 0);
}
changed to /proc/pid/mem because it seems to be working when process_vm_writev isn't
*/
int TinyDbg_write_memory(TinyDbg *handle, void *dest, void *src, size_t amount) {
    char mem_file_str[31]; // len("/proc/18446744073709551616/mem0") is 31 (64-bit max)
    sprintf(mem_file_str, "/proc/%d/mem", handle->pid);
    pthread_mutex_lock(&handle->breakpoint_lock);
    FILE *mem_file = fopen(mem_file_str, "w");
    fseek(mem_file, (long)dest, SEEK_SET);
    int result = fwrite(src, amount, 1, mem_file);
    if (result < 0) { fclose(mem_file); pthread_mutex_unlock(&handle->breakpoint_lock); return result; }
    fclose(mem_file);
    pthread_mutex_unlock(&handle->breakpoint_lock);
    return 0;
}

int TinyDbg_set_breakpoint_once(TinyDbg *handle, void *ip) {
    char original;
    char int3 = '\x03';
    if (TinyDbg_read_memory(handle, &original, ip, 1) != 0) return -1;  // read original content
    if (TinyDbg_write_memory(handle, ip, &int3, 1) != 0) return -2;     // write the int3
    pthread_mutex_lock(&handle->breakpoint_lock);
    handle->breakpoints_len += 1;
    handle->breakpoints = realloc(handle->breakpoints, handle->breakpoints_len * sizeof(TinyDbg_Breakpoint));
    handle->breakpoints[handle->breakpoints_len - 1].is_once = true;
    handle->breakpoints[handle->breakpoints_len - 1].original = original;
    handle->breakpoints[handle->breakpoints_len - 1].position = ip;
    // TODO write this to a list of breakpoints, so we can know whether it's for one time or not
    pthread_mutex_unlock(&handle->breakpoint_lock);
    return 0;
}

void TinyDbg_Breakpoint_print(TinyDbg_Breakpoint *breakpoint) {
    printf("Breakpoint(is_once = %d, position = %p)\n", breakpoint->is_once, breakpoint->position);
}

void TinyDbg_continue(TinyDbg *handle) {
    // lock process_continued again, worst case scenario you get EDEADLK for already owning it
    pthread_mutex_trylock(&handle->process_continued);
    pthread_mutex_unlock(&handle->process_continued);  // unlock it for real now
    ptrace(PTRACE_CONT, handle->pid, 0, 0);
}

static struct TinyDbg_Event *TinyDbg_get_event_nowait_nolock(TinyDbg *handle) {
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

    return current_head;
}

struct TinyDbg_Event *TinyDbg_get_event_nowait(TinyDbg *handle) {
    pthread_mutex_lock(&handle->event_lock);
    struct TinyDbg_Event *result = TinyDbg_get_event_nowait_nolock(handle);
    pthread_mutex_unlock(&handle->event_lock);
    return result;
}

struct TinyDbg_Event *TinyDbg_get_event(TinyDbg *handle) {
    struct TinyDbg_Event *current_result = TinyDbg_get_event_nowait(handle);
    if (current_result != NULL) return current_result;
    // have to wait...
    pthread_mutex_lock(&handle->event_lock);
    pthread_cond_wait(&handle->event_ready, &handle->event_lock);
    struct TinyDbg_Event *result = TinyDbg_get_event_nowait_nolock(handle);
    pthread_mutex_unlock(&handle->event_lock);
    return result;
}
