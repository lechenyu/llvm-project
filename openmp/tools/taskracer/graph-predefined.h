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