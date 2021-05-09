#include "event_queue.h"

void TinyDbg_Event_free(struct TinyDbg_Event *event) {
    if (event != NULL) {
        if (event->data != NULL) {
            free(event->data);
        }
        free(event);
    }
}

void TinyDbg_Event_print(struct TinyDbg_Event *event) {
    if (event->type == TinyDbg_EVENT_STOP) {
        // Stop event
        printf("StopEvent(%d)\n", *(int *)event->data);
    } else if (event->type == TinyDbg_EVENT_BREAKPOINT) {
        // Breakpoint event
        printf("BreakpointEvent\n");
    } else {
        printf("UnknownEvent\n");
    }
}
