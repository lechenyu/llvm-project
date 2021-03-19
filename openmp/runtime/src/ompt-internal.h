/*
 * ompt-internal.h - header of OMPT internal data structures
 */

//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __OMPT_INTERNAL_H__
#define __OMPT_INTERNAL_H__

#include "ompt-event-specific.h"
#include "omp-tools.h"

#define OMPT_VERSION 1

#define _OMP_EXTERN extern "C"

#define OMPT_INVOKER(x)                                                        \
  ((x == fork_context_gnu) ? ompt_parallel_invoker_program                     \
                           : ompt_parallel_invoker_runtime)

#define ompt_callback(e) e##_callback

typedef struct ompt_callbacks_internal_s {
#define ompt_event_macro(event, callback, eventid)                             \
  callback ompt_callback(event);

  FOREACH_OMPT_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_callbacks_internal_t;

typedef struct ompt_callbacks_active_s {
  unsigned int enabled : 1;
#define ompt_event_macro(event, callback, eventid) unsigned int event : 1;

  FOREACH_OMPT_EVENT(ompt_event_macro)

#undef ompt_event_macro
} ompt_callbacks_active_t;

#define TASK_TYPE_DETAILS_FORMAT(info)                                         \
  ((info->td_flags.task_serial || info->td_flags.tasking_ser)                  \
       ? ompt_task_undeferred                                                  \
       : 0x0) |                                                                \
      ((!(info->td_flags.tiedness)) ? ompt_task_untied : 0x0) |                \
      (info->td_flags.final ? ompt_task_final : 0x0) |                         \
      (info->td_flags.merged_if0 ? ompt_task_mergeable : 0x0)

typedef struct {
  ompt_frame_t frame;
  ompt_data_t task_data;
  struct kmp_taskdata *scheduling_parent;
  int thread_num;
} ompt_task_info_t;

typedef struct {
  ompt_data_t parallel_data;
  void *master_return_address;
} ompt_team_info_t;

typedef struct ompt_lw_taskteam_s {
  ompt_team_info_t ompt_team_info;
  ompt_task_info_t ompt_task_info;
  int heap;
  struct ompt_lw_taskteam_s *parent;
} ompt_lw_taskteam_t;

typedef struct {
  ompt_data_t thread_data;
  ompt_data_t task_data; /* stored here from implicit barrier-begin until
                            implicit-task-end */
  void *return_address; /* stored here on entry of runtime */
  ompt_state_t state;
  ompt_wait_id_t wait_id;
  int ompt_task_yielded;
  int parallel_flags; // information for the last parallel region invoked
  void *idle_frame;
} ompt_thread_info_t;

// The following two types of structs are used to pass target-related OMPT callbacks to libomptarget. The structs' definitions
// should be in sync with the definitions in libomptarget/src/ompt_internal.h
#define FOREACH_OMPT_TARGET_EVENT(macro)                                                                                   \
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

extern ompt_callbacks_internal_t ompt_callbacks;

#if OMPT_SUPPORT && OMPT_OPTIONAL
#if USE_FAST_MEMORY
#define KMP_OMPT_DEPS_ALLOC __kmp_fast_allocate
#define KMP_OMPT_DEPS_FREE __kmp_fast_free
#else
#define KMP_OMPT_DEPS_ALLOC __kmp_thread_malloc
#define KMP_OMPT_DEPS_FREE __kmp_thread_free
#endif
#endif /* OMPT_SUPPORT && OMPT_OPTIONAL */

#ifdef __cplusplus
extern "C" {
#endif

void ompt_pre_init(void);
void ompt_post_init(void);
void ompt_fini(void);

#define OMPT_GET_RETURN_ADDRESS(level) __builtin_return_address(level)
#define OMPT_GET_FRAME_ADDRESS(level) __builtin_frame_address(level)

int __kmp_control_tool(uint64_t command, uint64_t modifier, void *arg);

extern ompt_callbacks_active_t ompt_enabled;

#if KMP_OS_WINDOWS
#define UNLIKELY(x) (x)
#define OMPT_NOINLINE __declspec(noinline)
#else
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define OMPT_NOINLINE __attribute__((noinline))
#endif

#ifdef __cplusplus
}
#endif

#endif
