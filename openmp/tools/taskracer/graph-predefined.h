#ifndef GRAPH_DEF_H
#define GRAPH_DEF_H

#include <unistd.h>
#include <mutex>


void print_graph_json();
std::string extractInfo(const std::string& input, unsigned int& lineNumber, bool has_race);
std::string get_file_string(std::string filePath);

typedef unsigned int vertex_t;

enum event_type{
    parallel_begin,
    parallel_end,
    implicit_task,
    sync_region_begin,
    sync_region_end,
    task_create,
    target_begin,
    target_end,
    taskwait_end,
    taskgroup_begin,
    taskgroup_end,
    task_schedule
};

enum edge_type{
    CONT,
    FORK_I,
    FORK_E,
    JOIN,
    JOIN_E,
    BARRIER,
    TARGET
};

struct Edge_new{
    vertex_t target;
    edge_type type;

    Edge_new()
    : target(0), type(CONT)
    {}

    Edge_new(vertex_t t, edge_type et)
    : target(t), type(et)
    {}
};

struct Vertex_new{
    vertex_t id;
    bool has_race;
    bool ontarget;
    event_type end_event;
    std::string stack;
    Edge_new out_edges[10];
    unsigned int index;
    
    Vertex_new()
    : id(0), has_race(false),ontarget(false), end_event(implicit_task), stack(""), index(0)
    {}
};

struct race_info{
    vertex_t prev_step;
    vertex_t current_step;
    vertex_t lca;
    std::string prev_stack;
    std::string current_stack;
};

//Instanciate a graph
const int vertex_size = 130000000;

struct EdgeFull{
  edge_type type;
  vertex_t source;
  vertex_t target;
};

template <typename T>
class ConcurrentVector {
public:

    ConcurrentVector(size_t capacity=10){
        this->data_ = std::vector<T>();
        this->data_.reserve(capacity);
    }

    void push_back(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.push_back(value);
    }

    bool pop_back(T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!data_.empty()) {
            value = data_.back();
            data_.pop_back();
            return true;
        }
        return false;
    }

    // Iterator for begin
    typename std::vector<T>::iterator begin() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.begin();
    }

    // Iterator for end
    typename std::vector<T>::iterator end() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.end();
    }

    // Clear the vector
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.clear();
    }

    void reserve(size_t capacity){
        std::lock_guard<std::mutex> lock(mutex_);
        data_.reserve(capacity);
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.size();
    }

    // Getter: Get element at a specific index
    bool get(size_t index, T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < data_.size()) {
            value = data_[index];
            return true;
        }
        return false;
    }

    // Setter: Set element at a specific index
    bool set(size_t index, const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < data_.size()) {
            data_[index] = value;
            return true;
        }
        return false;
    }

    // Accessor: Get element by index
    T operator[](size_t index) {
        // std::lock_guard<std::mutex> lock(mutex_);
        if (index < data_.size()) {
            return data_[index];
        }
        // You might want to handle out-of-bounds access differently, e.g., throw an exception.
        // For simplicity, we return a default-constructed T in this example.
        return T();
    }

    ~ConcurrentVector(){
        
    }

private:
    std::vector<T> data_;
    std::mutex mutex_;
};

// Vector that saves all edges to be add to g, before output the graph
ConcurrentVector<EdgeFull> *savedEdges;
std::vector<Vertex_new> *savedVertex;

struct DataMove{
    void *orig_addr;
    void *dest_addr;
    size_t bytes;
    unsigned int device_mem_flag;
    std::string stack;

    DataMove(void *oa, void *da, size_t bytes, unsigned int flag, std::string stack) :
    orig_addr(oa), dest_addr(da), bytes(bytes), device_mem_flag(flag), stack(stack) {}
};

struct targetRegion{
    unsigned int begin_node;
    unsigned int end_node;
    std::vector<DataMove> dmv;
    targetRegion(){
        begin_node = 0;
        end_node = 0;
        dmv = std::vector<DataMove>();
        dmv.reserve(10);
    }
};

void addVertex(vertex_t id){
    (*savedVertex)[id].id = id;
}

void setRace(vertex_t id){
    (*savedVertex)[id].has_race = true;
}

bool hasRace(vertex_t id){
    return (*savedVertex)[id].has_race;
}

void setOnTarget(vertex_t id){
    (*savedVertex)[id].ontarget = true;
}

void unsetOnTarget(vertex_t id){
    (*savedVertex)[id].ontarget = false;
}

void addEvent(vertex_t id, event_type event){
    (*savedVertex)[id].end_event = event;  
}

void addStack(vertex_t id, std::string stack){
    (*savedVertex)[id].stack = stack;
}

void addEdge(vertex_t source, vertex_t target, edge_type type){
    Edge_new e = {target, type};
    (*savedVertex)[source].out_edges[(*savedVertex)[source].index] = e;
    (*savedVertex)[source].index++;
}

#endif