#ifndef __CALLBACK_TARGET_H__
#define __CALLBACK_TARGET_H__

#include "omp-tools.h"
#include "stdlib.h"
#define ompt_start_tool ompt_start_tool_original
#define ompt_initialize ompt_initialize_original
#define ompt_finalize ompt_finalize_original
#include "callback.h"
#undef ompt_start_tool
#undef ompt_initialize
#undef ompt_finalize

#if NOWAIT
#define NOWAIT_CLAUSE nowait
#else
#define NOWAIT_CLAUSE
#endif

static ompt_set_callback_t ompt_set_callback;
static ompt_get_callback_t ompt_get_callback;
static ompt_get_state_t ompt_get_state;
static ompt_get_task_info_t ompt_get_task_info;
static ompt_get_task_memory_t ompt_get_task_memory;
static ompt_get_thread_data_t ompt_get_thread_data;
static ompt_get_parallel_info_t ompt_get_parallel_info;
static ompt_get_unique_id_t ompt_get_unique_id;
static ompt_finalize_tool_t ompt_finalize_tool;
static ompt_get_num_procs_t ompt_get_num_procs;
static ompt_get_num_places_t ompt_get_num_places;
static ompt_get_place_proc_ids_t ompt_get_place_proc_ids;
static ompt_get_place_num_t ompt_get_place_num;
static ompt_get_partition_place_nums_t ompt_get_partition_place_nums;
static ompt_get_proc_id_t ompt_get_proc_id;
static ompt_enumerate_states_t ompt_enumerate_states;
static ompt_enumerate_mutex_impls_t ompt_enumerate_mutex_impls;

static const char *ompt_target_t_values[] = {
        "ompt_target_UNDEFINED",
        "ompt_target",                                    // 1
        "ompt_target_enter_data",                         // 2
        "ompt_target_exit_data",                          // 3
        "ompt_target_update",                             // 4
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_nowait",                             // 9
        "ompt_target_enter_data_nowait",                  // 10
        "ompt_target_exit_data_nowait",                   // 11
        "ompt_target_update_nowait"                       // 12
};

static const char *ompt_target_data_op_t_values[] = {
        "ompt_target_UNDEFINED",
        "ompt_target_data_alloc",                         // 1
        "ompt_target_data_transfer_to_device",            // 2
        "ompt_target_data_transfer_from_device",          // 3
        "ompt_target_data_delete",                        // 4
        "ompt_target_data_associate",                     // 5
        "ompt_target_data_disassociate",                  // 6
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_UNDEFINED",
        "ompt_target_data_alloc_async",                   // 17
        "ompt_target_data_transfer_to_device_async",      // 18
        "ompt_target_data_transfer_from_device_async",    // 19
        "ompt_target_data_delete_async"                   // 20
};

static void format_mapping_flag(int flag, char *buffer) {
  char *progress = buffer;

#define ADD_FS                                     \
  do {                                             \
    if (progress != buffer) {                      \
      progress += sprintf(progress, "|");          \
    }                                              \
  } while(0)

  if (flag & ompt_target_map_flag_to) {
    progress += sprintf(progress, "ompt_target_map_flag_to");
  }
  if (flag & ompt_target_map_flag_from) {
    ADD_FS;
    progress += sprintf(progress, "ompt_target_map_flag_from");
  }
  if (flag & ompt_target_map_flag_alloc) {
    ADD_FS;
    progress += sprintf(progress, "ompt_target_map_flag_alloc");
  }
  if (flag & ompt_target_map_flag_release) {
    ADD_FS;
    progress += sprintf(progress, "ompt_target_map_flag_release");
  }
  if (flag & ompt_target_map_flag_delete) {
    ADD_FS;
    progress += sprintf(progress, "ompt_target_map_flag_delete");
  }
  if (flag & ompt_target_map_flag_implicit) {
    ADD_FS;
    progress += sprintf(progress, "ompt_target_map_flag_implicit");
  }

#undef ADD_FS
}

#endif //__CALLBACK_TARGET_H__