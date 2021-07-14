#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "debugger.h"

int main(int argc, char *argv[], char *argp[]) {
    printf("Test main!\n");
    TinyDbg *handle = TinyDbg_start("test", argv, argp);
    EventQueue_join(TinyDbg_continue(handle));

    sleep(1);
    EventQueue_join(TinyDbg_stop(handle));
    struct user_regs_struct regs;
    EventQueue_join(TinyDbg_get_registers(handle, &regs));
    printf("rip is %llu\n", regs.rip);

    unsigned char buf[64];
    struct iovec local;
    struct iovec remote;
    local.iov_base = buf;
    local.iov_len = 64;
    remote.iov_base = (void *)regs.rip;
    remote.iov_len = 64;
    EventQueue_join(TinyDbg_get_memory(handle, local, remote));
    // write(2, buf, 64);
    // puts("\n");
    // EventQueue_join(TinyDbg_set_breakpoint(handle, regs.rip, true));
    EventQueue_join(TinyDbg_continue(handle));
    
    EventQueue_Consumer *consumer = EventQueue_new_consumer(handle->eq_debugger_events);
    while (true) {
        TinyDbg_Event *event; EventQueue_consume(consumer, (void **)&event);
        if (event->type == TinyDbg_event_type_exit) {
            printf("Exit!!! Code %d\n", event->content.stop_code);
            TinyDbg_Event_free(event);
            break;
        } else if (event->type == TinyDbg_event_type_stop) {
            printf("Stopped!!! Code %d\n", event->content.stop_code);
        } else {
            printf("Wtf! I don't know what this is.\n");
        }
        TinyDbg_Event_free(event);
    }
    EventQueue_destroy_consumer(consumer);
    TinyDbg_free(handle);
}
