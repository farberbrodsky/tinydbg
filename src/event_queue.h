#include <stdio.h>
#include <stdlib.h>

enum TinyDbg_EventType {
    TinyDbg_EVENT_BREAKPOINT,
    TinyDbg_EVENT_STOP
};

struct TinyDbg_Event {
    enum TinyDbg_EventType type;  // what event is this
    void *data;                   // additional data, per event
    struct TinyDbg_Event *next;   // the next event in the queue
};
void TinyDbg_Event_free(struct TinyDbg_Event *event);  // frees an event

struct TinyDbg_EventQueue {
    struct TinyDbg_Event *head;
    struct TinyDbg_Event *tail;
};

void TinyDbg_Event_print(struct TinyDbg_Event *event);
