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

_OMP_EXTERN OMPT_INTERFACE_ATTRIBUTE void
libomp_ompt_callback_target_emi(ompt_target_t kind,
                                ompt_scope_endpoint_t endpoint, int device_num,
                                void *codeptr);

_OMP_EXTERN OMPT_INTERFACE_ATTRIBUTE void
libomp_ompt_callback_target_submit_emi(ompt_scope_endpoint_t endpoint,
                                       unsigned int requested_num_teams);

_OMP_EXTERN OMPT_INTERFACE_ATTRIBUTE void
libomp_ompt_callback_target_data_op_emi(ompt_scope_endpoint_t endpoint,
                                        ompt_target_data_op_t optype,
                                        void *src_addr, int src_device_num,
                                        void *dest_addr, int dest_device_num,
                                        size_t bytes, bool ompRoutine,
                                        void *codeptr);

_OMP_EXTERN OMPT_INTERFACE_ATTRIBUTE void
libomp_ompt_callback_target_map_emi(unsigned int nitems, void **host_addr,
                                    void **device_addr, size_t *bytes,
                                    unsigned int *mapping_flags, void *codeptr);

#endif // __OMPT_TARGET_API_H__