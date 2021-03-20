#ifndef __OMPT_TARGET_H__
#define __OMPT_TARGET_H__

#include "omp-tools.h"

#define ompt_callback(e) e##_callback

// The following two types of structs are used to pass target-related OMPT callbacks to libomptarget. The structs' definitions
// should be in sync with the definitions in libomptarget/src/ompt_internal.h

/* Struct to collect target callback pointers */
typedef struct ompt_target_callbacks_internal_s {
#define ompt_event_macro(event, callback, eventid)                             \
  callback ompt_callback(event);

    FOREACH_OMPT_51_TARGET_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_target_callbacks_internal_t;

/* Bitmap to mark OpenMP 5.1 target events as registered*/
typedef struct ompt_target_callbacks_active_s {
    unsigned int enabled : 1;
#define ompt_event_macro(event, callback, eventid) unsigned int event : 1;

    FOREACH_OMPT_51_TARGET_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_target_callbacks_active_t;

extern ompt_target_callbacks_internal_t ompt_target_callbacks;

extern ompt_target_callbacks_active_t ompt_target_enabled;

#endif //__OMPT_TARGET_H__
