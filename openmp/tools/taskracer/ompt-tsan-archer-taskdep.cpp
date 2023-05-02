#include <iostream>
#include <cstdint>
#include <cassert>
#include "omp-tools.h"
#include "dlfcn.h"

#define GRAPH_MACRO

#ifdef GRAPH_MACRO
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphml.hpp>

using namespace boost;

void print_graph();

struct Vertex {
  unsigned int id;
  std::string end_event;
  unsigned int has_race;
  std::string race_stack;
};

struct Edge{
  std::string type;
};

const std::string contedge = "CONT";
const std::string iforkedge = "FORK_I";
const std::string eforkedge = "FORK_E";
const std::string joinedge = "JOIN";
const std::string ejoinedge = "JOIN_E";
const std::string barrieredge = "barrier";

const std::string event_parallel_begin = "parallel_begin";
const std::string event_parallel_end = "parallel_end";
const std::string event_implicit_task = "implicit_task";
const std::string event_sync_region_begin = "sync_region_begin";
const std::string event_sync_region_end = "sync_region_end";
const std::string event_task_create = "task_create";

//Define the graph using those classes
typedef adjacency_list<vecS, vecS, directedS, Vertex, Edge > Graph;
//Some typedefs for simplicity
typedef graph_traits<Graph>::vertex_descriptor vertex_t; // vertex_descriptor, in this case, is an index for the listS used to save all vertex. 
typedef graph_traits<Graph>::edge_descriptor edge_t;

//Instanciate a graph
unsigned int vertex_size = 35;
Graph g(vertex_size);
dynamic_properties dp;

#endif

// ---------------------------------
// Task Depend related part begin
// ---------------------------------
static int pagesize{0};
typedef char ompt_tsan_clockid;

// Data structure to provide a threadsafe pool of reusable objects.
// DataPool<Type of objects>
template <typename T> struct DataPool final {
  static __thread DataPool<T> *ThreadDataPool;
  std::mutex DPMutex{};

  // store unused objects
  std::vector<T *> DataPointer{};
  std::vector<T *> RemoteDataPointer{};

  // store all allocated memory to finally release
  std::list<void *> memory;

  // count remotely returned data (RemoteDataPointer.size())
  std::atomic<int> remote{0};

  // totally allocated data objects in pool
  int total{0};
  int getTotal() { return total; }
  int getMissing() {
    return total - DataPointer.size() - RemoteDataPointer.size();
  }

  // fill the pool by allocating a page of memory
  void newDatas() {
    if (remote > 0) {
      const std::lock_guard<std::mutex> lock(DPMutex);
      // DataPointer is empty, so just swap the vectors
      DataPointer.swap(RemoteDataPointer);
      remote = 0;
      return;
    }
    // calculate size of an object including padding to cacheline size
    size_t elemSize = sizeof(T);
    size_t paddedSize = (((elemSize - 1) / 64) + 1) * 64;
    // number of padded elements to allocate
    int ndatas = pagesize / paddedSize;
    char *datas = (char *)malloc(ndatas * paddedSize);
    memory.push_back(datas);
    for (int i = 0; i < ndatas; i++) {
      DataPointer.push_back(new (datas + i * paddedSize) T(this));
    }
    total += ndatas;
  }

  // get data from the pool
  T *getData() {
    T *ret;
    if (DataPointer.empty()){
      newDatas();
    }
      
    ret = DataPointer.back();
    DataPointer.pop_back();
    return ret;
  }

  // accesses to the thread-local datapool don't need locks
  void returnOwnData(T *data) {
    DataPointer.emplace_back(data);
  }

  // returning to a remote datapool using lock
  void returnData(T *data) {
    const std::lock_guard<std::mutex> lock(DPMutex);
    RemoteDataPointer.emplace_back(data);
    remote++;
  }

  ~DataPool() {
    // we assume all memory is returned when the thread finished / destructor is
    // called
    // if (archer_flags->report_data_leak && getMissing() != 0) {
    //   printf("ERROR: While freeing DataPool (%s) we are missing %i data "
    //          "objects.\n",
    //          __PRETTY_FUNCTION__, getMissing());
    //   exit(-3);
    // }
    for (auto i : DataPointer)
      if (i)
        i->~T();
    for (auto i : RemoteDataPointer)
      if (i)
        i->~T();
    for (auto i : memory)
      if (i)
        free(i);
  }
};

template <typename T> struct DataPoolEntry {
  DataPool<T> *owner;

  static T *New() { 
    return DataPool<T>::ThreadDataPool->getData();
  }

  void Delete() {
    static_cast<T *>(this)->Reset();
    if (owner == DataPool<T>::ThreadDataPool)
      owner->returnOwnData(static_cast<T *>(this));
    else
      owner->returnData(static_cast<T *>(this));
  }

  DataPoolEntry(DataPool<T> *dp) : owner(dp) {}
};

struct DependencyData;
typedef DataPool<DependencyData> DependencyDataPool;
template <>
__thread DependencyDataPool *DependencyDataPool::ThreadDataPool = nullptr;

// Data structure to store additional information for task dependency.
// each variable has a dependencyData
// For example, depend(out:i) depend(in:j)
// We will have one DependencyData for i, one for j
struct DependencyData final : DataPoolEntry<DependencyData> {
  std::vector<unsigned int> in = std::vector<unsigned int>();
  unsigned int out = 0;
  unsigned int inoutset = 0;
  
  std::vector<unsigned int> *GetInPtr() { return &in; }
  unsigned int *GetOutPtr() { return &out; }
  unsigned int *GetInoutsetPtr() { return &inoutset; }

  void Reset() {}

  static DependencyData *New() { 
    return DataPoolEntry<DependencyData>::New();
  }

  DependencyData(DataPool<DependencyData> *dp)
      : DataPoolEntry<DependencyData>(dp) {}
};

struct TaskDependency {
  std::vector<unsigned int> *inPtr;
  unsigned int *outPtr;
  unsigned int *inoutsetPtr;
  ompt_dependence_type_t type;
  TaskDependency(DependencyData *depData, ompt_dependence_type_t type)
      : inPtr(depData->GetInPtr()), outPtr(depData->GetOutPtr()),
        inoutsetPtr(depData->GetInoutsetPtr()), type(type) {}
        
  void AnnotateBegin(unsigned int current_step) {
    std::vector<unsigned int> depending_steps = std::vector<unsigned int>();

    if (type == ompt_dependence_type_out ||
        type == ompt_dependence_type_inout ||
        type == ompt_dependence_type_mutexinoutset) 
    {
      for(unsigned int steps: *inPtr){
        depending_steps.push_back(steps);
      }
      depending_steps.push_back(*outPtr);
      depending_steps.push_back(*inoutsetPtr);
    } else if (type == ompt_dependence_type_in) 
    {
      depending_steps.push_back(*outPtr);
      depending_steps.push_back(*inoutsetPtr);
    } else if (type == ompt_dependence_type_inoutset) 
    {
      for(unsigned int steps: *inPtr){
        depending_steps.push_back(steps);
      }
      depending_steps.push_back(*outPtr);
    }

  #ifdef GRAPH_MACRO
    printf("[AnnotateBegin] depending steps: ");
    for(unsigned int previous_step : depending_steps){
      if(previous_step != 0){
          printf("%d ", previous_step);
          edge_t e; bool b;
          boost::tie(e,b) = boost::edge(previous_step,current_step,g);
          if(!b){
            boost::tie(e,b) = boost::add_edge(previous_step,current_step,g);
            g[e].type = ejoinedge;
          }
      }
    }
    printf("\n");
  #endif
  }

  void AnnotateEnd(unsigned int current_step) {
    if (type == ompt_dependence_type_out ||
        type == ompt_dependence_type_inout ||
        type == ompt_dependence_type_mutexinoutset) 
    {
      *outPtr = current_step;
      inPtr->clear();
      // printf("[AnnotateEnd] Setting outPtr %u \n", current_step);
    } else if (type == ompt_dependence_type_in) 
    {
      inPtr->push_back(current_step);
      // printf("[AnnotateEnd] Setting inPtr %u \n", current_step);
    } else if (type == ompt_dependence_type_inoutset) 
    {
      *inoutsetPtr = current_step;
      inPtr->clear();
      // printf("[AnnotateEnd] Setting inoutPtr %u \n", current_step);
    }
  }
};

// ---------------------------------
// Task Depend related part end
// ---------------------------------

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
  unsigned int index;
  void *parallel_region;
  bool added_to_others_join;

  task_t* parent_task;
  unsigned int execution;
  // Dependency information for this task.
  TaskDependency *Dependencies{nullptr};

  // Number of dependency entries.
  unsigned DependencyCount{0};

  // We expect a rare need for the dependency-map, so alloc on demand
  std::unordered_map<void *, DependencyData *> *DependencyMap{nullptr};
public:
  task_t(TreeNode *node, finish_t *enclosed_finish, bool initialized, unsigned int index, void *pr, task_t* parent)
    : node_in_dpst(node), current_finish(nullptr),
      enclosed_finish(enclosed_finish), current_taskwait(&kNullTaskWait),
      current_step_id(kNullStepId), initialized(initialized),
      index(index), parallel_region(pr), added_to_others_join(false), 
      parent_task(parent), execution(0) {}
};

static void releaseDependencies(task_t *task) {
  unsigned int cs = task->current_step_id;
  for (unsigned i = 0; i < task->DependencyCount; i++) {
    task->Dependencies[i].AnnotateEnd(cs);
  }
}

static void acquireDependencies(task_t *task) {
  unsigned int cs = task->current_step_id;
  for (unsigned i = 0; i < task->DependencyCount; i++) {
    task->Dependencies[i].AnnotateBegin(cs);
  }
}


class parallel_t {
public:
  unsigned int parallelism;
  std::atomic<int> remaining_task;
  task_t *encounter_task;
  vertex_t start_cg_node;
  task_t *current_join_master;
  std::vector<vertex_t> end_nodes_to_join;
public:
  parallel_t(int parallelism, task_t *task)
      : parallelism(parallelism), remaining_task(parallelism),
        encounter_task(task), start_cg_node(-1) {
        }

  parallel_t(int parallelism, task_t *task, vertex_t node)
    : parallelism(parallelism), remaining_task(parallelism),
      encounter_task(task), start_cg_node(node), current_join_master(nullptr) {
        this->end_nodes_to_join = std::vector<vertex_t>(parallelism, 0);
      }

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

// TreeNode *__attribute__((weak))
// __tsan_alloc_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait);

TreeNode *__attribute__((weak))
__tsan_insert_internal_node(TreeNode *node, TreeNode *parent);

TreeNode *__attribute__((weak))
__tsan_alloc_insert_internal_node(int internal_node_id, NodeType node_type, TreeNode *parent, int preceeding_taskwait);

TreeNode *__attribute__((weak)) __tsan_insert_leaf(TreeNode *parent, int preceeding_taskwait);

void __attribute__((weak)) __tsan_print_DPST_info(bool print_dpst);

void __attribute__((weak)) __tsan_reset_step_in_tls();

void __attribute__((weak)) __tsan_set_step_in_tls(int step_id);

void __attribute__((weak)) AnnotateNewMemory(const char *f, int l, const volatile void *mem, size_t size);

TreeNode *__attribute__((weak)) __tsan_get_step_in_tls();

void __attribute__((weak)) __tsan_set_ompt_print_function(void (*f)());

void __attribute__((weak)) __tsan_set_report_race_steps_function(void (*f)(int,int));

void __attribute__((weak)) __tsan_set_report_race_stack_function(void (*f)(char*, bool, unsigned int, unsigned int));
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

void boost_test();
static std::atomic<int> task_id_counter(1);
static std::atomic<int> sync_id_counter(1);
static std::atomic<int> thread_id_counter(0);

void ompt_print_func(){
  printf("print in ompt \n");
}

void ompt_report_race_steps_func(int cur, int prev){
  // printf("race between current step %d and previous step %d \n", cur, prev);
  g[cur].has_race = 1;
  g[prev].has_race = 1;
}

void ompt_report_race_stack_func(char* s, bool first, unsigned int current_step, unsigned int prev_step){
  printf("race stack: %s \n", s);
  printf("first? %d, current_step %d, previous_step %d \n", first, current_step, prev_step);
  if(first){
    g[current_step].race_stack = s;
  }
  else{
    g[prev_step].race_stack = s;
  }
}

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
  // printf("[PARALLEL_BEGIN] \n");
  // insert a FINISH node because of the implicit barrier
  assert(encountering_task_data->ptr != nullptr);
  task_t* current_task = (task_t*) encountering_task_data->ptr;
  TreeNode *parent = find_parent(current_task);
  // 1. Update DPST
  TreeNode *new_finish_node = __tsan_alloc_insert_internal_node(
      sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
      parent, current_task->current_taskwait->corresponding_id);

#ifdef GRAPH_MACRO
  vertex_t cg_parent = current_task->current_step_id;
  g[cg_parent].end_event = event_parallel_begin;
#endif

  // 2. Set current task's current_finish to this finish
  finish_t *finish = new finish_t{
      new_finish_node, current_task->current_finish, current_task};
  current_task->current_finish = finish;
  
  parallel_data->ptr = new parallel_t(requested_parallelism, current_task, cg_parent);
  int step_id = insert_leaf(new_finish_node, current_task);

#ifdef GRAPH_MACRO
  // FJ: insert a node in G as continuation node, and a continuation edge
  g[step_id].id = step_id;

  edge_t e; bool b;
  boost::tie(e,b) = boost::add_edge(cg_parent,step_id,g);
  g[e].type = contedge;
#endif
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

#ifdef GRAPH_MACRO
  vertex_t cg_parent = current_task->current_step_id;
  g[cg_parent].end_event = event_parallel_end;
#endif

  int step_id = insert_leaf(parent, current_task);
  
#ifdef GRAPH_MACRO
  // 1. insert a node in G as continuation node, and a continuation edge
  g[step_id].id = step_id;

  edge_t e; bool b;
  boost::tie(e,b) = boost::add_edge(cg_parent,step_id,g);
  g[e].type = contedge;

  // 2. insert join edges from last step nodes in implicit tasks to the continuation node
  for(int joining_node : parallel->end_nodes_to_join){
    edge_t ee; bool bb;
    boost::tie(ee,bb) = boost::add_edge(joining_node,step_id,g);
    g[ee].type = joinedge;
  }

  if(current_task->node_in_dpst->corresponding_id == 0){
    print_graph();
  }
#endif

  delete parallel;
  parallel_data->ptr = nullptr;

  printf("[PARALLEL_END] \n");
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
      __tsan_set_ompt_print_function(&ompt_print_func);
      __tsan_set_report_race_steps_function(&ompt_report_race_steps_func);
      __tsan_set_report_race_stack_function(&ompt_report_race_stack_func);

      if (!root) {
        root = __tsan_init_DPST();
      }
      TreeNode *initial = __tsan_alloc_insert_internal_node(
          sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
          root, kNullStepId);
      finish_t *implicit_parallel = new finish_t{initial, nullptr, nullptr};
      task_t* main_ti = new task_t{root, nullptr, true, index, nullptr, nullptr};
      main_ti->current_finish = implicit_parallel;
      implicit_parallel->belonging_task = main_ti;
      task_data->ptr = main_ti;
      int step_id = insert_leaf(initial, main_ti);

    #ifdef GRAPH_MACRO
      // FJ: this should be the first node in G, with step_id = 1
      g[step_id].id = step_id;
    #endif
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
      task_t *ti = new task_t{new_task_node, enclosed_finish, true, index, (void*) parallel, parent_task};
      task_data->ptr = ti;
      int step_id = insert_leaf(new_task_node, ti);

    #ifdef GRAPH_MACRO
      // FJ: insert first step node for the implicit task
      vertex_t cg_parent = parallel->start_cg_node;

      g[step_id].id = step_id;

      edge_t e; bool b;
      boost::tie(e,b) = boost::add_edge(cg_parent,step_id,g);
      if(b){
        g[e].type = iforkedge;
      }
    #endif
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

      // TODO: taskgroup step node in computation graph
    } else if (endpoint == ompt_scope_end) {
      finish_t *finish = current_task->current_finish;
      TreeNode *parent = find_parent(finish);
      current_task->current_finish = finish->parent;
      delete finish;
      insert_leaf(parent, current_task);

      // TODO: taskgroup end 
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

      // TODO: taskwait
    }
  } else if (kind == ompt_sync_region_reduction) {
    task_t *current_task = (task_t *)task_data->ptr;
    printf("[SYNC] task current step id is %d \n", current_task->current_step_id);
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

      TreeNode *new_parallel_treeNode;
      parallel_t *parallel = (parallel_t *)parallel_data->ptr;
      int remaining_task = parallel->count_down_on_barrier();
      if (remaining_task == 1) {
        new_parallel_treeNode = __tsan_alloc_insert_internal_node(
            sync_id_counter.fetch_add(1, std::memory_order_relaxed), PARALLEL,
            finish_parent, enclosed_finish->node_in_dpst.load(std::memory_order_acquire)->preceeding_taskwait);
        enclosed_finish->node_in_dpst.store(new_parallel_treeNode, std::memory_order_release);
        parallel->reset_remaining_task();
      } else {
        do {
          new_parallel_treeNode = enclosed_finish->node_in_dpst.load(std::memory_order_acquire);
        } while (new_parallel_treeNode == current_task_node->parent);
      }

      TreeNode *new_task_node = __tsan_alloc_insert_internal_node(
             task_id_counter.fetch_add(1, std::memory_order_relaxed), ASYNC_I,
             new_parallel_treeNode, current_task_node->preceeding_taskwait);
      task_t* ti = new task_t{new_task_node, enclosed_finish, true, current_task->index, current_task->parallel_region, current_task};
      task_data->ptr = ti;
      
    #ifdef GRAPH_MACRO
      g[current_task->current_step_id].end_event = event_sync_region_begin;
    #endif

      int step_id = insert_leaf(new_task_node, ti);
      if(current_task->added_to_others_join == false){
        unsigned int team_index = ti->index;
        if(team_index > parallel->parallelism){
          parallel->end_nodes_to_join.push_back(step_id);
        }
        else{
          parallel->end_nodes_to_join[team_index] = step_id;
        }
        current_task->added_to_others_join = true;
      }

    #ifdef GRAPH_MACRO
      // FJ: continuation edge and continuation node
      g[step_id].id = step_id;
      g[step_id].end_event = event_sync_region_end;
      edge_t e;
      bool b;
      boost::tie(e,b) = boost::add_edge(current_task->current_step_id, step_id, g);
      g[e].type = contedge;
    #endif

      delete current_task;
    }
    else if(endpoint == ompt_scope_end){
      // printf("[SYNC scope_end] \n");

      if(parallel_data != nullptr){
        parallel_t* parallel = (parallel_t*) parallel_data->ptr;

        if(parallel != nullptr){
          task_t *current_task = (task_t *)task_data->ptr;
          unsigned int team_index = current_task->index;
          int current_step = current_task->current_step_id;

          if(team_index == 0){
            for(vertex_t node : parallel->end_nodes_to_join){
              // if node == 0, we can directly break, because
              // we assume all threads hit the barrier or not
              if(node == 0){
                break;
              }

              if(node == current_step){
                continue;
              }

            #ifdef GRAPH_MACRO
              edge_t e;
              bool b;
              boost::tie(e,b) = boost::add_edge(node, current_task->current_step_id, g);
              g[e].type = joinedge;
            #endif
              // boost::tie(e,b) = boost::add_edge(current_task->current_step_id, node, g);
              // g[e].type = barrieredge;
            }

            parallel->end_nodes_to_join.resize(parallel->parallelism);
          }
        }
      }
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
                                  
  parallel_t* parallel = (parallel_t*) current_task->parallel_region;
  unsigned int parallelism = parallel->parallelism;
  task_t *ti = new task_t{new_task_node, enclosed_finish, true, parallelism+1, current_task->parallel_region, current_task};
  new_task_data->ptr = ti;

#ifdef GRAPH_MACRO
  // 1. add a new node for new task and a fork edge
  vertex_t cg_parent = current_task->current_step_id;
  g[cg_parent].end_event = event_task_create;
#endif

  vertex_t fork_node = insert_leaf(new_task_node,ti);

#ifdef GRAPH_MACRO
  g[fork_node].id = fork_node;
  edge_t e; bool b;
  boost::tie(e,b) = boost::add_edge(cg_parent,fork_node,g);
  g[e].type = eforkedge;
#endif

  vertex_t continuation_node = insert_leaf(parent, current_task);

#ifdef GRAPH_MACRO
  // 2. add a continuation node and continuation edge, notice insert_leaf will set the current_step_id of current_task to be the continuation node
  g[continuation_node].id = continuation_node;

  edge_t ee; bool bb;
  boost::tie(ee,bb) = boost::add_edge(cg_parent,continuation_node,g);
  g[ee].type = contedge;
#endif
}


static void ompt_ta_dependences(ompt_data_t *task_data,
                                const ompt_dependence_t *deps,
                                int ndeps)
{
  task_t* ct = (task_t*) task_data->ptr;
  int step_id = ct->current_step_id;
  printf("[DEPENDENCES] ndeps = %d, current step id %d \n", ndeps, step_id);

  // for(int i=0; i < ndeps; i++){
  //   ompt_dependence_t d = deps[i];
  //   printf("  dependence variable address %p, type %d \n", d.variable.ptr, d.dependence_type);
  // }

  // typedef enum ompt_dependence_type_t
  // ompt_dependence_type_in              = 1,
  // ompt_dependence_type_out             = 2,
  // ompt_dependence_type_inout           = 3,
  // ompt_dependence_type_mutexinoutset   = 4,

  if (ndeps > 0) {
    // Copy the data to use it in task_switch and task_end.
    task_t *Data = (task_t*) task_data->ptr;
    if (!Data->parent_task) {
      // Return since doacross dependences are not supported yet.
      return;
    }

    if (!Data->parent_task->DependencyMap)
      Data->parent_task->DependencyMap = new std::unordered_map<void *, DependencyData *>();

    Data->Dependencies = (TaskDependency *) malloc(sizeof(TaskDependency) * ndeps);
    Data->DependencyCount = ndeps;

    for (int i = 0; i < ndeps; i++) {
      auto ret = Data->parent_task->DependencyMap->insert(std::make_pair(deps[i].variable.ptr, nullptr));

      if (ret.second) {
        ret.first->second = DependencyData::New();
        ret.first->second->in.reserve(5);
      }

      new ((void *)(Data->Dependencies + i)) TaskDependency(ret.first->second, deps[i].dependence_type);

      // printf("[DEPENDENCES] variable address: %p, in %p, out %p, inout %p \n", ret.first->first, 
      //   Data->Dependencies[i].inPtr, Data->Dependencies[i].outPtr, Data->Dependencies[i].inoutsetPtr);
    }

    // This callback is executed before this task is first started.
    // TsanHappensBefore(Data->GetTaskPtr());
  }
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
      if(prior_ti->added_to_others_join == false){
        parallel_t* previous_task_parallel_region = (parallel_t*) prior_ti->parallel_region;
        int team_index = prior_ti->index;

        if(team_index > previous_task_parallel_region->parallelism){
          previous_task_parallel_region->end_nodes_to_join.push_back(prior_ti->node_in_dpst->children_list_tail->corresponding_id);
        }
        else{
          previous_task_parallel_region->end_nodes_to_join[team_index] = prior_ti->node_in_dpst->children_list_tail->corresponding_id;
        }
        
        prior_ti->added_to_others_join = true;
        // printf("[TASK SCHEDULE] step id %d \n", prior_ti->node_in_dpst->children_list_tail->corresponding_id);
      }

      releaseDependencies(prior_ti);

      delete prior_ti;
    }
  }
  
  assert(next_task_data->ptr);
  task_t* next_task = (task_t*) next_task_data->ptr;

  __tsan_set_step_in_tls(next_task->current_step_id);
  if (next_task->execution == 0) {
    acquireDependencies(next_task);
  }
  next_task->execution++;

  // TreeNode* next_task_node = next_task->node_in_dpst;
  // if (!next_task->initialized) {
  //   insert_leaf(next_task_node, next_task);
  //   next_task->initialized = true;
  // } else {
  // __tsan_set_step_in_tls(next_task->current_step_id);
  // }

  // TODO: check if next_task has dependency. If it has, add join edge from finished tasks to a new step node.
  // if(next_task->depending_task_nodes.size() > 0){
    // for(TreeNode* join_task : next_task->depending_task_nodes){
    //   vertex_t join_parent = (unsigned int) join_task->children_list_tail->corresponding_id;
    //   edge_t e; bool b;
    //   boost::tie(e,b) = boost::add_edge(join_parent,next_task->current_step_id,g);
    //   g[e].type = ejoinedge;
    // }
}

static void ompt_ta_work(
  ompt_work_t wstype, 
  ompt_scope_endpoint_t endpoint, 
  ompt_data_t *parallel_data, 
  ompt_data_t *task_data, 
  uint64_t count, 
  const void *codeptr_ra)
{
  // parallel_t *parallel = (parallel_t*) parallel_data->ptr;
  // if(wstype == ompt_work_single_executor){
  //   if(endpoint == ompt_scope_begin){
  //     // if(parallel->end_nodes_to_join.size() > parallel->parallelism){
  //     //   parallel->end_nodes_to_join.resize(parallel->parallelism);
  //     // }
  //   }
  // }
}

static void ompt_ta_thread_begin(ompt_thread_t thread_type, ompt_data_t *thread_data) {
  thread_data->value = thread_id_counter.fetch_add(1, std::memory_order_relaxed);
  DependencyDataPool::ThreadDataPool = new DependencyDataPool;
}

static void ompt_ta_thread_end(ompt_data_t *thread_data){
  delete DependencyDataPool::ThreadDataPool;
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
  SET_CALLBACK(thread_end);
  SET_CALLBACK(work);
  SET_CALLBACK(dependences);

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

  std::cout << "Computation Graph recording enabled" << std::endl;

  // auto vertex_id_map = boost::get(&Vertex::id, g);
  // auto edge_type_map = boost::get(&Edge::type, g);
  // dp.property("vertex_id", vertex_id_map);
  // dp.property("edge_type", edge_type_map);

  // Create two vertices in that graph
  // vertex_t u = boost::add_vertex(g);
  // vertex_t v = boost::add_vertex(g);

  // // Create an edge conecting those two vertices
  // edge_t e; bool b;
  // boost::tie(e,b) = boost::add_edge(u,v,g);

  // // Set the properties of a vertex and the edge
  // g[u].id = 666;
  // g[v].id = 888;
  // g[e].info = "Hello world";
  // g[e].type = "cont";

  // // how to use vertex_t and edge_t
  // std::cout << "u is " << u << std::endl;
  // std::cout << "v is " << v << std::endl;
  // std::cout << "e source: " << e.m_source << ", vertex id is: " << g[e.m_source].id << std::endl;
  // std::cout << "e target: " << e.m_target << ", vertex id is: " << g[e.m_target].id << std::endl;

  // property_map<Graph, vertex_index_t>::type index_map = get(vertex_index, g);
  // auto vertex_id_map = boost::get(&Vertex::id, g);
  // auto edge_type_map = boost::get(&Edge::type, g);

  // dynamic_properties dp;
  // dp.property("vertex_id", vertex_id_map);
  // dp.property("edge_type", edge_type_map);

  // write_graphml(std::cout, g, dp, true);
  // boost_test();

  pagesize = getpagesize();

  return &ompt_start_tool_result;
}

void print_graph(){
  // typedef property_map<Graph, vertex_index_t>::type IndexMap;
  // IndexMap index = get(vertex_index, g);

  // printf("vertices(g) = ");

  // graph_traits<Graph>::vertex_iterator vi, vi_end, next;
  // boost::tie(vi, vi_end) = boost::vertices(g);
  // for (next = vi; vi != vi_end; vi = next) {
  //   ++next;
  //   if(g[*vi].id != 0){
  //     std::cout << g[*vi].id << " ";
  //   }
  // }

  // printf("\n");

  // printf("edges(g) = ");

  // graph_traits<Graph>::edge_iterator ei, ei_end;
  // for (tie(ei, ei_end) = edges(g); ei != ei_end; ++ei){
  //     std::cout << "(" 
  //               << index[source(*ei, g)] 
  //               << "->" << index[target(*ei, g)]
  //               << ","  << g[*ei].type  
  //               << ") ";
      
  // }

  // printf("\n");

  const char* env_p = std::getenv("PRINT_GRAPHML");
  if(env_p && (strcmp(env_p, "TRUE") == 0)){
    auto vertex_id_map = boost::get(&Vertex::id, g);
    auto edge_type_map = boost::get(&Edge::type, g);
    auto vertex_event_map = boost::get(&Vertex::end_event, g);
    auto vertex_has_race_map = boost::get(&Vertex::has_race, g);
    auto vertex_race_stack_map = boost::get(&Vertex::race_stack,g);

    dynamic_properties dp;
    dp.property("vertex_id", vertex_id_map);
    dp.property("edge_type", edge_type_map);
    dp.property("end_event", vertex_event_map);
    dp.property("has_race", vertex_has_race_map);
    dp.property("race_stack", vertex_race_stack_map);

    std::ofstream output("output.txt");
    write_graphml(output, g, dp, true);
  }
  
}

/* Test code

void boost_test(){
  // Initialize boost graph
  // create a typedef for the Graph type
  typedef adjacency_list<vecS, vecS, bidirectionalS> Graph;

  // Make convenient labels for the vertices
  enum { A, B, C, D, E, N };
  const int num_vertices = N;
  const char* name = "ABCDE";

  // writing out the edges in the graph
  typedef std::pair<int, int> Edge;
  Edge edge_array[] = 
  { Edge(A,B), Edge(A,D), Edge(C,A), Edge(D,C),
    Edge(C,E), Edge(B,D), Edge(D,E) };
  const int num_edges = sizeof(edge_array)/sizeof(edge_array[0]);

  // declare a graph object
  Graph g(num_vertices);

  // add the edges to the graph object
  for (int i = 0; i < num_edges; ++i){
    add_edge(edge_array[i].first, edge_array[i].second, g);
  }

  // get the property map for vertex indices
  typedef property_map<Graph, vertex_index_t>::type IndexMap;
  IndexMap index = get(vertex_index, g);

  printf("vertices(g) = ");

  typedef graph_traits<Graph>::vertex_iterator vertex_iter;
  std::pair<vertex_iter, vertex_iter> vp;
  for (vp = vertices(g); vp.first != vp.second; ++vp.first){
    std::cout << index[*vp.first] <<  " ";
  }
    
  printf("\n");

  // get the edge list
  printf("edges(g) = ");

  graph_traits<Graph>::edge_iterator ei, ei_end;
  for (tie(ei, ei_end) = edges(g); ei != ei_end; ++ei)
      std::cout << "(" << index[source(*ei, g)] 
                << "," << index[target(*ei, g)] << ") ";

  printf("\n");
}

*/