#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef GUARD_24e5ae45_8058_4b87_9cc3_e06d5681082b
#define GUARD_24e5ae45_8058_4b87_9cc3_e06d5681082b
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/uio.h>   // process_vm_readv, process_vm_writev
#include <pthread.h>
#include <sys/wait.h>
#include <sys/user.h>  // user_regs_struct
#include <sys/ptrace.h>
#include "event_queue.h"

typedef struct {
    uintptr_t *position;  // where is the breakpoint
    bool is_once;         // whether or not to delete this breakpoint immediately after use
    char original;        // what was there before the breakpoint
} TinyDbg_Breakpoint;

typedef struct {
    pid_t pid;                          // debugged process pid
    uintptr_t *breakpoint_positions;    // array of breakpoint positions
    TinyDbg_Breakpoint *breakpoints;    // array of breakpoint data, with the same indexes, this is for locality (cpu caching)
    size_t breakpoints_len;             // how many breakpoints are there
    pthread_mutex_t breakpoint_lock;    // mutex for reading/setting breakpoints
    pthread_t waiter_thread;            // this thread is used for waitpid-ing in the background
    pthread_mutex_t event_lock;         // mutex for using the event_queue
    pthread_cond_t event_ready;         // broadcast to when there is an event ready to be added
    pthread_mutex_t process_continued;  // unlocked when the process is continued
    struct TinyDbg_EventQueue event_queue;
} TinyDbg;

// Start the debugger
TinyDbg *TinyDbg_start(const char *filename, char *const argv[], char *const envp[]);
// Free a TinyDbg instance once it's done - delete the mutex, stop the thread, etc.
void TinyDbg_free(TinyDbg *handle);
// Get the registers of the debugged process
struct user_regs_struct TinyDbg_get_registers(TinyDbg *handle);
// Set the registers of the debugged process
void TinyDbg_set_regsisters(TinyDbg *handle, struct user_regs_struct regs);
// Read the debugged process's memory. Returns 0 on success, otherwise -1 and errno is set by process_vm_readv.
int TinyDbg_read_memory(TinyDbg *handle, void *dest, void *src, size_t amount);
// Write to the debugged process's memory. Returns 0 on success, otherwise -1 and errno is set by process_vm_writev.
int TinyDbg_write_memory(TinyDbg *handle, void *dest, void *src, size_t amount);
// Set a breakpoint that is only hit once (automatically deleted), returns -1 if couldn't read or -2 if couldn't write.
// errno is set like in TinyDbg_read_memory and in TinyDbg_write_memory.
int TinyDbg_set_breakpoint_once(TinyDbg *handle, void *ip);
// Continue a stopped process, using PTRACE_CONT
void TinyDbg_continue(TinyDbg *handle);
// Get the first event, such as a breakpoint being hit or the process being stopped, and return it (or NULL).
struct TinyDbg_Event *TinyDbg_get_event_nowait(TinyDbg *handle);
// Wait for an event, such as a breakpoint being hit or the process being stopped, and return it.
struct TinyDbg_Event *TinyDbg_get_event(TinyDbg *handle);

#endif
