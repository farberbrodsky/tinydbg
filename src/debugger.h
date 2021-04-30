#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/uio.h>  // process_vm_readv, process_vm_writev
#include <pthread.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include "event_queue.h"

typedef struct {
    pid_t pid;                   // debugged process pid
    pthread_t waiter_thread;     // this thread is used for waitpid-ing in the background
    pthread_mutex_t event_lock;  // mutex for using the event_queue
    struct TinyDbg_EventQueue event_queue;
} TinyDbg;

// Start the debugger
TinyDbg *TinyDbg_start(const char *filename, char *const argv[], char *const envp[]);
// Get the registers of the debugged process
struct user_regs_struct TinyDbg_get_regsisters(TinyDbg *handle);
// Set the registers of the debugged process
void TinyDbg_set_regsisters(TinyDbg *handle, struct user_regs_struct regs);
// Read the debugged process's memory
void TinyDbg_read_memory(TinyDbg *handle, void *dst, void *src, size_t amount);
// Write to the debugged process's memory
void TinyDbg_write_memory(TinyDbg *handle, void *dst, void *src, size_t amount);
// Set a breakpoint that is only hit once (automatically deleted)
void TinyDbg_set_breakpoint_once(TinyDbg *handle, void *ip);
// Continue a stopped process, using PTRACE_CONT
void TinyDbg_continue(TinyDbg *handle);
// Waits for an event, such as a breakpoint being hit or the process being stopped, and returns it.
struct TinyDbg_Event TinyDbg_get_event();
// Free a TinyDbg instance once it's done - delete the mutex, stop the thread, etc.
void TinyDbg_free(TinyDbg *handle);
