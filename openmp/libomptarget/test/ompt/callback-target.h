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

static void on_ompt_callback_target_emi(
        ompt_target_t kind,
        ompt_scope_endpoint_t endpoint,
        int device_num,
        ompt_data_t *task_data,
        ompt_data_t *target_task_data,
        ompt_data_t *target_data,
        const void *codeptr_ra)
{
  switch(endpoint)
  {
    case ompt_scope_begin:
      if (target_data->ptr)
      {
        printf("%s\n", "0: target_data initially not null");
      }
      target_data->value = ompt_get_unique_id();

      printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_emi_begin: task_id=%" PRIu64
      ", target_task_id=%" PRIu64 ", target_id=%" PRIu64 ", device_num=%" PRIu32
      ", kind=%s" ", codeptr_ra=%p" "\n", ompt_get_thread_data()->value, task_data->value,
              target_task_data ? target_task_data->value : 0, target_data->value, device_num,
              ompt_target_t_values[kind], codeptr_ra);
      break;
    case ompt_scope_end:
      printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_emi_end: task_id=%" PRIu64
      ", target_task_id=%" PRIu64 ", target_id=%" PRIu64 ", device_num=%" PRIu32
      ", kind=%s" ", codeptr_ra=%p" "\n", ompt_get_thread_data()->value, task_data->value,
              target_task_data ? target_task_data->value : 0, target_data->value, device_num,
              ompt_target_t_values[kind], codeptr_ra);
      break;
    case ompt_scope_beginend:
      printf("ompt_scope_beginend should never be passed to %s\n", __func__);
      exit(-1);
  }
}

static void on_ompt_callback_target_data_op_emi(
        ompt_scope_endpoint_t endpoint,
        ompt_data_t *target_task_data,
        ompt_data_t *target_data,
        ompt_id_t *host_op_id,
        ompt_target_data_op_t optype,
        void *src_addr,
        int src_device_num,
        void *dest_addr,
        int dest_device_num,
        size_t bytes,
        const void *codeptr_ra)
{
  switch (endpoint) {
    case ompt_scope_begin:
      *host_op_id = ompt_get_unique_id();
      printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_data_op_emi_begin: target_task_id=%" PRIu64
      ", target_id=%" PRIu64 ", host_op_id=%" PRIu64 ", optype=%s, src_addr=%p, src_device_num=%" PRIu32
      ", dest_addr=%p, dest_device_num=%" PRIu32 ", bytes=%" PRIu64 ", codeptr_ra=%p" "\n",
          ompt_get_thread_data()->value, target_task_data ? target_task_data->value : 0, target_data->value, *host_op_id,
          ompt_target_data_op_t_values[optype], src_addr, src_device_num, dest_addr, dest_device_num, bytes, codeptr_ra);
      break;
    case ompt_scope_end:
      printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_data_op_emi_end: target_task_id=%" PRIu64
      ", target_id=%" PRIu64 ", host_op_id=%" PRIu64 ", optype=%s, src_addr=%p, src_device_num=%" PRIu32
      ", dest_addr=%p, dest_device_num=%" PRIu32 ", bytes=%" PRIu64 ", codeptr_ra=%p" "\n",
          ompt_get_thread_data()->value, target_task_data ? target_task_data->value : 0, target_data->value, *host_op_id,
          ompt_target_data_op_t_values[optype], src_addr, src_device_num, dest_addr, dest_device_num, bytes, codeptr_ra);
      break;
    case ompt_scope_beginend:
      if (optype == ompt_target_data_associate || optype == ompt_target_data_disassociate) {
        *host_op_id = ompt_get_unique_id();
        printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_data_op_emi_beginend: target_task_id=%" PRIu64
        ", target_id=%" PRIu64 ", host_op_id=%" PRIu64 ", optype=%s, src_addr=%p, src_device_num=%" PRIu32
        ", dest_addr=%p, dest_device_num=%" PRIu32 ", bytes=%" PRIu64 ", codeptr_ra=%p" "\n",
                ompt_get_thread_data()->value, target_task_data ? target_task_data->value : 0, target_data->value, *host_op_id,
                ompt_target_data_op_t_values[optype], src_addr, src_device_num, dest_addr, dest_device_num, bytes, codeptr_ra);
      } else {
        printf("ompt_scope_beginend should never be passed to %s\n", __func__);
        exit(-1);
      }
      break;
  }
}

static void on_ompt_callback_target_map_emi(
        ompt_data_t *target_data,
        unsigned int nitems,
        void **host_addr,
        void **device_addr,
        size_t *bytes,
        unsigned int *mapping_flags,
        const void *codeptr_ra)
{
  char buffer[2048];
  printf("%" PRIu64 ":" _TOOL_PREFIX " ompt_event_target_map_emi: target_id=%" PRIu64
         ", nitems=%" PRIu32 ", codeptr_ra=%p" "\n", ompt_get_thread_data()->value, target_data->value,
         nitems, codeptr_ra);
  for (int i = 0; i < nitems; i++)
  {
    format_mapping_flag(mapping_flags[i], buffer);
    printf("%" PRIu64 ":" _TOOL_PREFIX " map: host_addr=%p, device_addr=%p, bytes=%" PRIu64 ", mapping_flag=%s" "\n",
            ompt_get_thread_data()->value, host_addr[i], device_addr[i], bytes[i], buffer);
    buffer[0] = '\0'; // reset the buffer
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

  // Register target-constructs-related callbacks
  register_ompt_callback_t(ompt_callback_target_emi, ompt_callback_target_emi_t);
  register_ompt_callback_t(ompt_callback_target_data_op_emi, ompt_callback_target_data_op_emi_t);
  register_ompt_callback_t(ompt_callback_target_map_emi, ompt_callback_target_map_emi_t);
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