#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef GUARD_DEBUGGER_H
#define GUARD_DEBUGGER_H
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/ptrace.h>
#include "../event_queue_c/event_queue.h"

// Breakpoints are stored in pointers.
// They are freed with free() because they don't store any more pointers
typedef struct {
    uintptr_t position;   // where is the breakpoint
    bool is_once;         // whether or not to delete this breakpoint immediately after use
    char original;        // what was there before the breakpoint
} TinyDbg_Breakpoint;

typedef struct {
    pid_t pid;                          // debugged process pid

    uintptr_t *breakpoint_positions;    // array of breakpoint positions
    TinyDbg_Breakpoint *breakpoints;    // array of breakpoint data, with the same indexes, this is for locality (cpu caching)
    size_t breakpoints_len;             // how many breakpoints are there
    pthread_mutex_t breakpoint_lock;    // accessing breakpoints may not require reading memory from the process - so you don't have to wait for the process manager

    pthread_t waiter_thread;            // this thread is used for waitpid-ing in the background
    pthread_t process_manager_thread;   // this thread manages the process - ptraces and reads/writes to memory

    pthread_mutex_t process_continued;  // locked/unlocked by manager when the process is continued

    EventQueue *eq_process_manager;     // event queue for the process manager - send your ptrace/memory/breakpoint requests here
    EventQueue *eq_debugger_events;     // event queue for events e.g. breakpoint hit or process stopped
} TinyDbg;

// Start the debugger
TinyDbg *TinyDbg_start(const char *filename, char *const argv[], char *const envp[]);

// Free a TinyDbg instance once it's done - delete the mutex, stop the thread, etc.
void TinyDbg_free(TinyDbg *handle);

// TODO define the event structs and the union and enum for them
typedef enum {
    TinyDbg_procman_request_type_stop,
    TinyDbg_procman_request_type_continue,
    TinyDbg_procman_request_type_singlestep,
    TinyDbg_procman_request_type_get_regs,
    TinyDbg_procman_request_type_set_regs,
    TinyDbg_procman_request_type_get_mem,
    TinyDbg_procman_request_type_set_mem,
    TinyDbg_procman_request_type_set_breakp,
    TinyDbg_INTERNAL_procman_request_type_waitpid,
} TinyDbg_procman_request_type;

typedef struct {
    struct iovec local_iov;
    struct iovec remote_iov;
} TinyDbg_procman_request_get_mem;
typedef TinyDbg_procman_request_get_mem TinyDbg_procman_request_set_mem;

typedef struct {
    uintptr_t position;
    bool is_once;
} TinyDbg_procman_request_set_breakp;

typedef struct {
    TinyDbg_procman_request_type type;
    void *content;
} TinyDbg_procman_request;

typedef enum {
    TinyDbg_event_type_exit,
    TinyDbg_event_type_stop,
    TinyDbg_event_type_breakpoint,
} TinyDbg_Event_type;

typedef struct {
    TinyDbg_Event_type type;
    union TinyDbg_Event_content {
        int stop_code;
        TinyDbg_Breakpoint breakpoint;
    } content;
} TinyDbg_Event;

void TinyDbg_Event_free(TinyDbg_Event *event);

EventQueue_JoinHandle *TinyDbg_stop(TinyDbg *handle);
EventQueue_JoinHandle *TinyDbg_continue(TinyDbg *handle);
EventQueue_JoinHandle *TinyDbg_singlestep(TinyDbg *handle);
EventQueue_JoinHandle *TinyDbg_get_registers(TinyDbg *handle, struct user_regs_struct *save_to);
EventQueue_JoinHandle *TinyDbg_set_registers(TinyDbg *handle, struct user_regs_struct *save_to);
EventQueue_JoinHandle *TinyDbg_get_memory(TinyDbg *handle, struct iovec local_iov, struct iovec remote_iov);
EventQueue_JoinHandle *TinyDbg_set_memory(TinyDbg *handle, struct iovec local_iov, struct iovec remote_iov);
EventQueue_JoinHandle *TinyDbg_set_breakpoint(TinyDbg *handle, uintptr_t position, bool is_once);

TinyDbg_Breakpoint *TinyDbg_list_breakpoints(TinyDbg *handle, size_t *breakpoints_len);

#endif
