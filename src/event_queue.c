#include "event_queue.h"

void TinyDbg_event_free(struct TinyDbg_Event *event) {
    if (event->data != NULL) {
        free(event->data);
    }
    free(event);
}
