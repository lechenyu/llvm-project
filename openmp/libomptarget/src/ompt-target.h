#ifndef __OMPT_TARGET_H__
#define __OMPT_TARGET_H__

#include "omp-tools.h"

#define ompt_callback(e) e##_callback

// The following two types of structs are used to pass target-related OMPT callbacks to libomptarget. The structs' definitions
// should be in sync with the definitions in libomptarget/src/ompt_internal.h
#define FOREACH_OMPT_TARGET_EVENT(macro)                                                                                 \
  macro (ompt_callback_device_initialize,  ompt_callback_device_initialize_t,  12) /* device initialize               */ \
  macro (ompt_callback_device_finalize,    ompt_callback_device_finalize_t,    13) /* device finalize                 */ \
  macro (ompt_callback_device_load,        ompt_callback_device_load_t,        14) /* device load                     */ \
  macro (ompt_callback_device_unload,      ompt_callback_device_unload_t,      15) /* device unload                   */ \
                                                                                                                         \
  /* Optional Events */                                                                                                  \
  macro (ompt_callback_target_emi,         ompt_callback_target_emi_t,         33) /* target                          */ \
  macro (ompt_callback_target_data_op_emi, ompt_callback_target_data_op_emi_t, 34) /* target data op                  */ \
  macro (ompt_callback_target_submit_emi,  ompt_callback_target_submit_emi_t,  35) /* target submit                   */ \
  macro (ompt_callback_target_map_emi,     ompt_callback_target_map_emi_t,     36) /* target map                      */

typedef struct ompt_target_callbacks_internal_s {
#define ompt_event_macro(event, callback, eventid)                             \
  callback ompt_callback(event);

  FOREACH_OMPT_TARGET_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_target_callbacks_internal_t;

typedef struct ompt_target_callbacks_active_s {
  unsigned int enabled : 1;
#define ompt_event_macro(event, callback, eventid) unsigned int event : 1;

  FOREACH_OMPT_TARGET_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_target_callbacks_active_t;

extern ompt_target_callbacks_internal_t ompt_target_callbacks;

extern ompt_target_callbacks_active_t ompt_target_enabled;

#endif //__OMPT_TARGET_H__
