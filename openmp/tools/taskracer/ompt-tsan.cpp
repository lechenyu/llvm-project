#include <iostream>
#include <cstdint>
#include <cassert>
#include "omp-tools.h"
#include "dlfcn.h"

enum NodeType {
  ROOT,
  FINISH,
  ASYNC_I,
  ASYNC_E,
  FUTURE,
  STEP,
  TASKWAIT,
  PARALLEL,
  NODE_TYPE_END
};

enum class Sid : uint8_t {};

enum class Epoch : uint16_t {};
typedef struct TreeNode {
 public:
  int corresponding_id;
  NodeType node_type;
  int depth;
  int number_of_child;
  int is_parent_nth_child;
  int preceeding_taskwait;
  Sid sid;
  Epoch ev;
  TreeNode *parent;
  TreeNode *children_list_head;
  TreeNode *children_list_tail;
  TreeNode *next_sibling;
} TreeNode;

// static constexpr TreeNode *kNotOpenMPTask = nullptr;
static constexpr int kNullStepId = -1;
static constexpr size_t kDefaultStackSize = 1024;
static TreeNode *root = nullptr;
static TreeNode kNullTaskWait = {kNullStepId, TASKWAIT};

class task_t;
class finish_t {
public:
  std::atomic<TreeNode *> node_in_dpst;
  finish_t *parent;
  task_t *belonging_task;
public:
  finish_t(TreeNode *node, finish_t *parent, task_t *task)
      : node_in_dpst(node), parent(parent), belonging_task(task) {}
};

class task_t {
public:
  TreeNode *node_in_dpst;
  finish_t *current_finish;
  finish_t *enclosed_finish;
  TreeNode *current_taskwait;
  int current_step_id;
  bool initialized;
public:
  task_t(TreeNode *node, finish_t *enclosed_finish, bool initialized)
      : node_in_dpst(node), current_finish(nullptr),
        enclosed_finish(enclosed_finish), current_taskwait(&kNullTaskWait),
        current_step_id(kNullStepId), initialized(initialized) {}
};

class parallel_t {
public:
  int parallelism;
  std::atomic<int> remaining_task;
  task_t *encounter_task;
public:
  parallel_t(int parallelism, task_t *task)
      : parallelism(parallelism), remaining_task(parallelism),
        encounter_task(task) {}
  int count_down_on_barrier() {
    int val = remaining_task.fetch_sub(1, std::memory_order_acq_rel);
    return val;
  }
  void reset_remaining_task() {
    remaining_task.store(parallelism, std::memory_order_release);
  }
};

extern "C" {
void __attribute__((weak)) __tsan_print();

TreeNode __attribute__((weak)) *__tsan_init_DPST();

TreeNode *__attribute__((weak))
__tsan_alloc_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait);

TreeNode *__attribute__((weak))
__tsan_insert_internal_node(TreeNode *node, TreeNode *parent);

TreeNode *__attribute__((weak))
__tsan_alloc_insert_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait);

TreeNode *__attribute__((weak)) __tsan_insert_leaf(TreeNode *parent, int preceeding_taskwait);

void __attribute__((weak)) __tsan_print_DPST_info(bool print_dpst);

void __attribute__((weak)) __tsan_reset_step_in_tls();

void __attribute__((weak)) __tsan_set_step_in_tls(int step_id);

void __attribute__((weak)) AnnotateNewMemory(const char *f, int l, const volatile void *mem, size_t size);
} // extern "C"


#define SET_OPTIONAL_CALLBACK_T(event, type, result, level)                    \
  do {                                                                         \
    ompt_callback_##type##_t ta_##event = &ompt_ta_##event;                    \
    result = ompt_set_callback(ompt_callback_##event,                          \
                               (ompt_callback_t)ta_##event);                   \
    if (result < level)                                                        \
      printf("Registered callback '" #event "' is not supported at " #level    \
             " (%i)\n",                                                        \
             result);                                                          \
  } while (0)

#define SET_CALLBACK_T(event, type)                                            \
  do {                                                                         \
    int res;                                                                   \
    SET_OPTIONAL_CALLBACK_T(event, type, res, ompt_set_always);                \
  } while (0)

#define SET_CALLBACK(event) SET_CALLBACK_T(event, event)

#define GET_ENTRY_POINT(name)                                                  \
  do {                                                                         \
    ompt_##name = (ompt_##name##_t) lookup("ompt_"#name);                      \
    if (ompt_##name == NULL) {                                                 \
      std::cerr << "Could not find 'ompt_" #name "', exiting..." << std::endl; \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

#define TsanNewMemory(addr, size)                                              \
  AnnotateNewMemory(__FILE__, __LINE__, addr, size)

ompt_get_task_info_t ompt_get_task_info;
ompt_set_callback_t ompt_set_callback;
ompt_get_thread_data_t ompt_get_thread_data;
ompt_get_task_memory_t ompt_get_task_memory;


static std::atomic<int> task_id_counter(1);
static std::atomic<int> sync_id_counter(1);
static std::atomic<int> thread_id_counter(0);

static inline int insert_leaf(TreeNode *internal_node, task_t *task) {
  TreeNode *new_step = __tsan_insert_leaf(internal_node, task->current_taskwait->corresponding_id);
  int step_id = new_step->corresponding_id;
  task->current_step_id = step_id;
  __tsan_set_step_in_tls(step_id);
  return step_id;
}

static inline TreeNode *find_parent(task_t *task) {
  if (task->current_finish) {
    return task->current_finish->node_in_dpst.load(std::memory_order_acquire);
  } else {
    return task->node_in_dpst;
  }
}

static inline TreeNode *find_parent(finish_t *finish) {
  if (finish->parent) {
    return finish->parent->node_in_dpst.load(std::memory_order_acquire);
  } else {
    return finish->belonging_task->node_in_dpst;
  }
}

static void ompt_ta_parallel_begin
(
  ompt_data_t *encountering_task_data,
  const ompt_frame_t *encountering_task_frame,
  ompt_data_t *parallel_data,
  unsigned int requested_parallelism,
  int flags,
  const void *codeptr_ra
)
{
  // insert a FINISH node because of the implicit barrier
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode *parent = find_parent(current_task);
  // 1. Update DPST
  TreeNode *new_finish_node = __tsan_alloc_insert_internal_node(
      sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
      parent, current_task->current_taskwait->corresponding_id);

  // 2. Set current task's current_finish to this finish
  finish_t *finish = new finish_t{
      new_finish_node, current_task->current_finish, current_task};
  current_task->current_finish = finish;
  parallel_data->ptr = new parallel_t(requested_parallelism, current_task);
  insert_leaf(new_finish_node, current_task);
}


static void ompt_ta_parallel_end
(
  ompt_data_t *parallel_data,
  ompt_data_t *encountering_task_data,
  int flags,
  const void *codeptr_ra
)
{
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  finish_t* finish = current_task->current_finish;
  current_task->current_finish = finish->parent;
  TreeNode *parent = find_parent(finish);
  delete finish;
  parallel_t *parallel = (parallel_t *)parallel_data->ptr;
  delete parallel;
  parallel_data->ptr = nullptr;
  insert_leaf(parent, current_task);
}


static void ompt_ta_implicit_task(
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  unsigned int actual_parallelism,
  unsigned int index,
  int flags
)
{
  if (flags & ompt_task_initial) {
    if (endpoint == ompt_scope_begin) {
      printf("OMPT! initial task begins, should only appear once !! \n");
      if (!root) {
        root = __tsan_init_DPST();
      }
      TreeNode *initial = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
          root, kNullStepId);
      finish_t *implicit_parallel = new finish_t{initial, nullptr, nullptr};
      task_t* main_ti = new task_t{root, nullptr, true};
      main_ti->current_finish = implicit_parallel;
      implicit_parallel->belonging_task = main_ti;
      task_data->ptr = main_ti;
      insert_leaf(initial, main_ti);
    } else if (endpoint == ompt_scope_end) {
      task_t *ti = (task_t *)task_data->ptr;
      delete ti;
      __tsan_reset_step_in_tls();
    }
  } else if (flags & ompt_task_implicit) {
    if (endpoint == ompt_scope_begin) {
      assert(parallel_data->ptr != nullptr);
      parallel_t *parallel = (parallel_t *) parallel_data->ptr;
      task_t* parent_task = parallel->encounter_task;
      TreeNode *parent = find_parent(parent_task);
      TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
          task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_I,
          parent, parent_task->current_taskwait->corresponding_id);
      finish_t *enclosed_finish = parent_task->current_finish;
      task_t *ti = new task_t{new_task_node, enclosed_finish, true};
      task_data->ptr = ti;
      insert_leaf(new_task_node, ti);
    } else if (endpoint == ompt_scope_end) {
      task_t *ti = (task_t *)task_data->ptr;
      delete ti;
      __tsan_reset_step_in_tls();
    }
  }
}


static void ompt_ta_sync_region(
  ompt_sync_region_t kind,
  ompt_scope_endpoint_t endpoint,
  ompt_data_t *parallel_data,
  ompt_data_t *task_data,
  const void *codeptr_ra)
{
  if(kind == ompt_sync_region_taskgroup){
    assert(task_data->ptr != nullptr);
    task_t *current_task = (task_t *)task_data->ptr;
    if (endpoint == ompt_scope_begin) {
      TreeNode *parent = find_parent(current_task);
      TreeNode *new_finish_node = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), FINISH,
          parent, current_task->current_taskwait->corresponding_id);
      finish_t *new_finish =
          new finish_t{new_finish_node, current_task->current_finish, current_task};
      current_task->current_finish = new_finish;
      insert_leaf(new_finish_node, current_task);
    } else if (endpoint == ompt_scope_end) {
      finish_t *finish = current_task->current_finish;
      TreeNode *parent = find_parent(finish);
      current_task->current_finish = finish->parent;
      delete finish;
      insert_leaf(parent, current_task);
    }
  } else if(kind == ompt_sync_region_taskwait){
    assert(task_data->ptr != nullptr);
    task_t *current_task = (task_t *)task_data->ptr;
    if (endpoint == ompt_scope_begin) {
      // insert a special node for taskwait, mark this as a taskwait step
      TreeNode *parent = find_parent(current_task);
      TreeNode *new_taskwait_node = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), TASKWAIT,
          parent, current_task->current_taskwait->corresponding_id);
      current_task->current_taskwait = new_taskwait_node;
    } else if (endpoint == ompt_scope_end) {
      TreeNode *parent = current_task->current_taskwait->parent;
      insert_leaf(parent, current_task);
    }
  } else if (kind == ompt_sync_region_reduction) {
    return;
  } else {
    // For all kinds of barriers, split parallel region
    // Assume barriers are only used in implicit tasks
    if (endpoint == ompt_scope_begin) {
      task_t *current_task = (task_t *)task_data->ptr;
      TreeNode *current_task_node = current_task->node_in_dpst;
      if (current_task_node->node_type != ASYNC_I) {
        printf("WARNING: barrier in explicit task\n");
        return;
      }
      finish_t *enclosed_finish = current_task->enclosed_finish;
      TreeNode *finish_parent = find_parent(enclosed_finish);

      TreeNode *new_parallel;
      parallel_t *parallel = (parallel_t *)parallel_data->ptr;
      int remaining_task = parallel->count_down_on_barrier();
      if (remaining_task == 1) {
        new_parallel = __tsan_alloc_insert_internal_node(
            sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
            finish_parent, enclosed_finish->node_in_dpst.load(std::memory_order_acquire)->preceeding_taskwait);
        enclosed_finish->node_in_dpst.store(new_parallel, std::memory_order_release);
        parallel->reset_remaining_task();
      } else {
        do {
          new_parallel = enclosed_finish->node_in_dpst.load(std::memory_order_acquire);
        } while (new_parallel == current_task_node->parent);
      }

      TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
             task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_I,
             new_parallel, current_task_node->preceeding_taskwait);
      task_t* ti = new task_t{new_task_node, enclosed_finish, true};
      task_data->ptr = ti;
      delete current_task;
      insert_leaf(new_task_node, ti);
    }
  }
  
}


static void ompt_ta_task_create(ompt_data_t *encountering_task_data,
                                const ompt_frame_t *encountering_task_frame,
                                ompt_data_t *new_task_data, int flags,
                                int has_dependences, const void *codeptr_ra) 
{
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode* parent = find_parent(current_task);
  TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
      task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_E, parent,
      current_task->current_taskwait->corresponding_id);
  finish_t *enclosed_finish = current_task->current_finish
                                  ? current_task->current_finish
                                  : current_task->enclosed_finish;
  task_t *ti = new task_t{new_task_node, enclosed_finish, false};
  new_task_data->ptr = ti;
  insert_leaf(parent, current_task);
}



static void ompt_ta_task_schedule(
  ompt_data_t *prior_task_data,
  ompt_task_status_t prior_task_status,
  ompt_data_t *next_task_data
){
  // printf("OMPT! task_schedule, put task node in current thread \n");
  // printf("task %d, %p scheduled\n", next_task_node->corresponding_task_id, next_task_node);
  // printf("thread %lu, task %lu\n", ompt_get_thread_data()->value, (uintptr_t)next_task_node & 0xFFFFUL);
  // char *stack = static_cast<char *>(__builtin_frame_address(0));
  // TreeNode *prior_task_node = ((task_t *)prior_task_data->ptr)->node_in_dpst;
  //printf("task %lu, stack range [%p, %p]\n", (uintptr_t)next_task_node & 0xFFFFUL, stack - kDefaultStackSize, stack);
  // printf("tid = %lu, prior task %lu, next task %lu, %u\n",
  //        ompt_get_thread_data()->value, (uintptr_t)prior_task_node & 0xFFFFUL,
  //        (uintptr_t)next_task_node & 0xFFFFUL, prior_task_status);
  TsanNewMemory(static_cast<char *>(__builtin_frame_address(0)) -
                    kDefaultStackSize,
                kDefaultStackSize);
  if (prior_task_status == ompt_task_complete ||
      prior_task_status == ompt_task_late_fulfill) {
    if (ompt_get_task_memory) {
      void *addr;
      size_t size;
      int ret_task_memory = 1, block = 0;
      while (ret_task_memory) {
        ret_task_memory = ompt_get_task_memory(&addr, &size, block);
        if (size > 0) {
          TsanNewMemory(((void**)addr), size+8);
        }
        block++;
      }
    }
    if (prior_task_status == ompt_task_complete && prior_task_data) {
      task_t *prior_ti = (task_t *)prior_task_data->ptr;
      delete prior_ti;
    }
  }
  
  assert(next_task_data->ptr);
  task_t* next_task = (task_t*) next_task_data->ptr;
  TreeNode* next_task_node = next_task->node_in_dpst;
  if (!next_task->initialized) {
    insert_leaf(next_task_node, next_task);
    next_task->initialized = true;
  } else {
    __tsan_set_step_in_tls(next_task->current_step_id);
  }
}

static void ompt_ta_thread_begin(ompt_thread_t thread_type, ompt_data_t *thread_data) {
  thread_data->value = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
}


static void ompt_ta_target(ompt_target_t kind, ompt_scope_endpoint_t endpoint,
                             int device_num, ompt_data_t *task_data,
                             ompt_id_t target_id, const void *codeptr_ra) 
{
  printf("[ompt_ta_target] callback: ");
  if (endpoint == ompt_scope_begin)
  {
    printf("scope begin \n");
  }
  else if(endpoint == ompt_scope_end){
    printf("scope end \n");
  }
  else{
    printf("\n");
  }
  

}




static void ompt_ta_device_mem(ompt_data_t *target_task_data,
                                ompt_data_t *target_data,
                                unsigned int device_mem_flag,
                                void *orig_base_addr, void *orig_addr,
                                int orig_device_num, void *dest_addr,
                                int dest_device_num, size_t bytes,
                                const void *codeptr_ra)
{
  printf("[ompt_ta_device_mem] callback: original addr %p ", orig_addr);
  if (device_mem_flag & ompt_device_mem_flag_to){
    printf("to \n");
  }
  else if (device_mem_flag & ompt_device_mem_flag_from){
    printf("from \n");
  }
  else if (device_mem_flag & ompt_device_mem_flag_alloc){
    printf("alloc \n");
  }
  else if (device_mem_flag & ompt_device_mem_flag_release){
    printf("release \n");
  }
  else if (device_mem_flag & ompt_device_mem_flag_associate){
    printf("associate \n");
  }
  else if (device_mem_flag & ompt_device_mem_flag_disassociate){
    printf("associate \n");
  }
  
} 

static int ompt_tsan_initialize(ompt_function_lookup_t lookup, int device_num,
                                ompt_data_t *tool_data) {
  // __tsan_print();
  // ompt_set_callback = (ompt_set_callback_t) lookup("ompt_set_callback");
  // if (ompt_set_callback == NULL) {
  //   std::cerr << "Could not set callback, exiting..." << std::endl;
  //   std::exit(1);
  // }

  // ompt_get_task_info = (ompt_get_task_info_t) lookup("ompt_get_task_info");
  // if(ompt_get_task_info == NULL){
  //   std::cerr << "Could not get task info, exiting..." << std::endl;
  //   std::exit(1);
  // }
  GET_ENTRY_POINT(set_callback);
  GET_ENTRY_POINT(get_task_info);
  GET_ENTRY_POINT(get_thread_data);
  GET_ENTRY_POINT(get_task_memory);

  SET_CALLBACK(task_create);
  SET_CALLBACK(parallel_begin);
  SET_CALLBACK(implicit_task);
  SET_CALLBACK(sync_region);
  SET_CALLBACK(parallel_end);
  SET_CALLBACK(task_schedule);
  SET_CALLBACK(thread_begin);
  
  SET_CALLBACK(target);
  SET_CALLBACK(device_mem);

  return 1; // success
}

static void ompt_tsan_finalize(ompt_data_t *tool_data) {
  //__tsan_print_DPST_info(true);
}

static bool scan_tsan_runtime() {
  void *func_ptr = dlsym(RTLD_DEFAULT, "__tsan_read1");
  return func_ptr != nullptr;
}

extern "C" ompt_start_tool_result_t *
ompt_start_tool(unsigned int omp_version, const char *runtime_version) {
  static ompt_start_tool_result_t ompt_start_tool_result = {
      &ompt_tsan_initialize, &ompt_tsan_finalize, {0}};
  if (!scan_tsan_runtime()) {
    std::cout << "Taskracer does not detect TSAN runtime, stopping operation" << std::endl;
    return nullptr;
  }
  std::cout << "Taskracer detects TSAN runtime, carrying out race detection using DPST" << std::endl;
  return &ompt_start_tool_result;
}