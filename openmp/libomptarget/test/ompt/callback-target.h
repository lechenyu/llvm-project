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

char **target_kind_to_str;

static void on_ompt_callback_target_emi(
  ompt_target_t kind,
  ompt_scope_endpoint_t endpoint,
  int device_num,
  ompt_data_t *task_data,
  ompt_data_t *target_task_data,
  ompt_data_t *target_data,
  const void *codeptr_ra
 ) {

  switch(endpoint)
  {
    case ompt_scope_begin:
      if (target_data->ptr)
      {
        printf("%s\n", "0: target_data initially not null");
      }
      target_data->value = ompt_get_unique_id();

      printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_emi_begin: target_id=%" PRIu64
             ", target_task_id=%" PRIu64 ", task_id=%" PRIu64 ", device_num=%" PRIu32
             ", kind=%s" ", codeptr_ra=%p" "\n", ompt_get_thread_data()->value, target_data->value,
             target_task_data ? target_task_data->value : 0, task_data->value, device_num,
             target_kind_to_str[kind], codeptr_ra);
      break;
    case ompt_scope_end:
      printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_emi_end: target_id=%" PRIu64
             ", target_task_id=%" PRIu64 ", task_id=%" PRIu64 ", device_num=%" PRIu32
             ", kind=%s" ", codeptr_ra=%p" "\n", ompt_get_thread_data()->value, target_data->value,
             target_task_data ? target_task_data->value : 0, task_data->value, device_num,
             target_kind_to_str[kind], codeptr_ra);
      break;
    case ompt_scope_beginend:
      printf("ompt_scope_beginend should never be passed to %s\n", __func__);
      exit(-1);
  }
}

int ompt_initialize(
        ompt_function_lookup_t lookup,
        int initial_device_num,
        ompt_data_t *tool_data)
{
  ompt_initialize_original(lookup, initial_device_num, tool_data);

  ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
  ompt_get_callback = (ompt_get_callback_t) lookup("ompt_get_callback");
  ompt_get_state = (ompt_get_state_t) lookup("ompt_get_state");
  ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");
  ompt_get_task_memory = (ompt_get_task_memory_t)lookup("ompt_get_task_memory");
  ompt_get_thread_data = (ompt_get_thread_data_t) lookup("ompt_get_thread_data");
  ompt_get_parallel_info = (ompt_get_parallel_info_t) lookup("ompt_get_parallel_info");
  ompt_get_unique_id = (ompt_get_unique_id_t) lookup("ompt_get_unique_id");
  ompt_finalize_tool = (ompt_finalize_tool_t)lookup("ompt_finalize_tool");

  ompt_get_num_procs = (ompt_get_num_procs_t) lookup("ompt_get_num_procs");
  ompt_get_num_places = (ompt_get_num_places_t) lookup("ompt_get_num_places");
  ompt_get_place_proc_ids = (ompt_get_place_proc_ids_t) lookup("ompt_get_place_proc_ids");
  ompt_get_place_num = (ompt_get_place_num_t) lookup("ompt_get_place_num");
  ompt_get_partition_place_nums = (ompt_get_partition_place_nums_t) lookup("ompt_get_partition_place_nums");
  ompt_get_proc_id = (ompt_get_proc_id_t) lookup("ompt_get_proc_id");
  ompt_enumerate_states = (ompt_enumerate_states_t) lookup("ompt_enumerate_states");
  ompt_enumerate_mutex_impls = (ompt_enumerate_mutex_impls_t) lookup("ompt_enumerate_mutex_impls");

  // initialize target_kind_to_str
  target_kind_to_str = malloc(sizeof(char*) * (int)ompt_target_update_nowait);
  target_kind_to_str[ompt_target] = "ompt_target";
  target_kind_to_str[ompt_target_enter_data] = "ompt_target_enter_data";
  target_kind_to_str[ompt_target_exit_data] = "ompt_target_exit_data";
  target_kind_to_str[ompt_target_update] = "ompt_target_update";
  target_kind_to_str[ompt_target_nowait] = "ompt_target_nowait";
  target_kind_to_str[ompt_target_enter_data_nowait] = "ompt_target_enter_data_nowait";
  target_kind_to_str[ompt_target_exit_data_nowait] = "ompt_target_exit_data_nowait";
  target_kind_to_str[ompt_target_update_nowait] = "ompt_target_update_nowait";


  // Register target-constructs-related callbacks
  register_ompt_callback_t(ompt_callback_target_emi, ompt_callback_target_emi_t);
  return 1; //success
}

void ompt_finalize(ompt_data_t *tool_data)
{
  ompt_finalize_original(tool_data);
}

#ifdef __cplusplus
extern "C" {
#endif
ompt_start_tool_result_t* ompt_start_tool(
        unsigned int omp_version,
        const char *runtime_version)
{
  static ompt_start_tool_result_t ompt_start_tool_result = {&ompt_initialize,&ompt_finalize, 0};
  return &ompt_start_tool_result;
}
#ifdef __cplusplus
}
#endif

#endif //__CALLBACK_TARGET_H__
