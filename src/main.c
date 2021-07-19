#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "debugger.h"

int main(int argc, char *argv[], char *argp[]) {
    printf("Test main!\n");
    TinyDbg *handle = TinyDbg_start_advanced("test", argv, argp, TINYDBG_FLAG_NO_ASLR);
    EventQueue_join(TinyDbg_stop_on_syscall(handle));
    EventQueue_join(TinyDbg_continue(handle));

    sleep(1);
    struct user_regs_struct regs;
    EventQueue_join(TinyDbg_get_registers(handle, &regs));
    printf("rip is %llu\n", regs.rip);

    sleep(1);
    printf("Breakpoint: deployed at %llu!\n", regs.rip);
    EventQueue_join(TinyDbg_set_breakpoint(handle, regs.rip, false));

    size_t breakpoints_len;
    TinyDbg_Breakpoint *breakpoints = TinyDbg_list_breakpoints(handle, &breakpoints_len);
    for (int i = 0; i < breakpoints_len; i++) {
        printf("breakpoint #%d, position: %lu\n", i, breakpoints[i].position);
    }
    free(breakpoints);
    
    EventQueue_Consumer *consumer = EventQueue_new_consumer(handle->eq_debugger_events);
    while (true) {
        TinyDbg_Event *event; EventQueue_consume(consumer, (void **)&event);
        if (event->type == TinyDbg_event_type_exit) {
            printf("Exit!!! Code %d\n", event->content.stop_code);
            TinyDbg_Event_free(event);
            break;
        } else if (event->type == TinyDbg_event_type_stop) {
            printf("Stopped!!! Code %d\n", event->content.stop_code);
            EventQueue_join(TinyDbg_continue(handle));
        } else if (event->type == TinyDbg_event_type_breakpoint) {
            printf("Breaked!!!!\n");
            EventQueue_join(TinyDbg_continue(handle));
        } else if (event->type == TinyDbg_event_type_syscall) {
            printf("Syscall!!! number is %d\n", event->content.syscall_id);
            EventQueue_join(TinyDbg_continue(handle));
            TinyDbg_Event_free(event);
            EventQueue_consume(consumer, (void **)&event);  // next event is the after-syscall event
            EventQueue_join(TinyDbg_continue(handle));
        } else {
            printf("Wtf! I don't know what this is.\n");
        }
        TinyDbg_Event_free(event);
    }
    EventQueue_destroy_consumer(consumer);
    TinyDbg_free(handle);
}
