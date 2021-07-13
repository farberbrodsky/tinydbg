#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "debugger.h"

int main(int argc, char *argv[], char *argp[]) {
    printf("Test main!\n");
    TinyDbg *handle = TinyDbg_start("test", argv, argp);
    EventQueue_join(TinyDbg_continue(handle));

    sleep(1);
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
    int fd = open("./something", O_CREAT, O_RDWR);
    write(fd, buf, 64);
    write(2, buf, 64);
    close(fd);
    
    EventQueue_Consumer *consumer = EventQueue_new_consumer(handle->eq_debugger_events);
    while (true) {
        TinyDbg_Event *event; EventQueue_consume(consumer, (void **)&event);
        if (event->type == TinyDbg_event_type_exit) {
            printf("Exit!!! Code %d\n", event->content.stop_code);
            TinyDbg_Event_free(event);
            break;
        }
        TinyDbg_Event_free(event);
    }
    EventQueue_destroy_consumer(consumer);

    TinyDbg_free(handle);
}
