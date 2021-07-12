#include <stdio.h>
#include "debugger.h"

int main(int argc, char *argv[], char *argp[]) {
    printf("Test main!\n");
    TinyDbg *handle = TinyDbg_start("test", argv, argp);
    EventQueue_join(TinyDbg_continue(handle));

    struct user_regs_struct regs;
    EventQueue_join(TinyDbg_get_registers(handle, &regs));
    printf("rip is %llu\n", regs.rip);
    
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
