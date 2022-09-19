#ifndef __CALLBACK_NOEMI_H__
#define __CALLBACK_NOEMI_H__

#include "../callback-target.h"

int ompt_initialize(ompt_function_lookup_t lookup, int initial_device_num,
                    ompt_data_t *tool_data) {
  ompt_initialize_original(lookup, initial_device_num, tool_data);

  ompt_set_callback = (ompt_set_callback_t)lookup("ompt_set_callback");
  ompt_get_callback = (ompt_get_callback_t)lookup("ompt_get_callback");
  ompt_get_state = (ompt_get_state_t)lookup("ompt_get_state");
  ompt_get_task_info = (ompt_get_task_info_t)lookup("ompt_get_task_info");
  ompt_get_task_memory = (ompt_get_task_memory_t)lookup("ompt_get_task_memory");
  ompt_get_thread_data = (ompt_get_thread_data_t)lookup("ompt_get_thread_data");
  ompt_get_parallel_info =
      (ompt_get_parallel_info_t)lookup("ompt_get_parallel_info");
  ompt_get_unique_id = (ompt_get_unique_id_t)lookup("ompt_get_unique_id");
  ompt_finalize_tool = (ompt_finalize_tool_t)lookup("ompt_finalize_tool");

  ompt_get_num_procs = (ompt_get_num_procs_t)lookup("ompt_get_num_procs");
  ompt_get_num_places = (ompt_get_num_places_t)lookup("ompt_get_num_places");
  ompt_get_place_proc_ids =
      (ompt_get_place_proc_ids_t)lookup("ompt_get_place_proc_ids");
  ompt_get_place_num = (ompt_get_place_num_t)lookup("ompt_get_place_num");
  ompt_get_partition_place_nums =
      (ompt_get_partition_place_nums_t)lookup("ompt_get_partition_place_nums");
  ompt_get_proc_id = (ompt_get_proc_id_t)lookup("ompt_get_proc_id");
  ompt_enumerate_states =
      (ompt_enumerate_states_t)lookup("ompt_enumerate_states");
  ompt_enumerate_mutex_impls =
      (ompt_enumerate_mutex_impls_t)lookup("ompt_enumerate_mutex_impls");

  // Register target-constructs-related callbacks
  register_ompt_callback_t(ompt_callback_target, ompt_callback_target_t);
  register_ompt_callback_t(ompt_callback_target_data_op,
                           ompt_callback_target_data_op_t);
  register_ompt_callback_t(ompt_callback_target_map,
                           ompt_callback_target_map_t);
  register_ompt_callback_t(ompt_callback_target_submit,
                           ompt_callback_target_submit_t);
  return 1; // success
}

void ompt_finalize(ompt_data_t *tool_data) {
  ompt_finalize_original(tool_data);
}

#ifdef __cplusplus
extern "C" {
#endif
ompt_start_tool_result_t *ompt_start_tool(unsigned int omp_version,
                                          const char *runtime_version) {
  static ompt_start_tool_result_t ompt_start_tool_result = {&ompt_initialize,
                                                            &ompt_finalize, 0};
  return &ompt_start_tool_result;
}
#ifdef __cplusplus
}
#endif

#endif //__CALLBACK_NOEMI_H__
