#ifndef __OMPT_TARGET_API_H__
#define __OMPT_TARGET_API_H__

#include "kmp_os.h"
#include "omp-tools.h"

#define _OMP_EXTERN extern "C"

#ifdef FROM_LIBOMPTARGET
#define OMPT_INTERFACE_ATTRIBUTE KMP_WEAK_ATTRIBUTE_INTERNAL
#else
#define OMPT_INTERFACE_ATTRIBUTE
#endif

/* Bitmap to mark OpenMP 5.1 target events as registered*/
typedef struct ompt_target_callbacks_active_s {
  unsigned int enabled : 1;
#define ompt_event_macro(event, callback, eventid) unsigned int event : 1;

  FOREACH_OMPT_51_TARGET_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_target_callbacks_active_t;

_OMP_EXTERN OMPT_INTERFACE_ATTRIBUTE bool
libomp_start_tool(ompt_target_callbacks_active_t *libomptarget_ompt_enabled);

#endif // __OMPT_TARGET_API_H__