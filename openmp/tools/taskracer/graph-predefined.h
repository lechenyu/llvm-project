#ifndef GRAPH_DEF_H
#define GRAPH_DEF_H

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphml.hpp>
#include <mutex>

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
const std::string barrieredge = "BARRIER";
const std::string targetedge = "TARGET";

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
unsigned int vertex_size = 100;
Graph g(vertex_size);

struct EdgeFull{
  std::string type;
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
ConcurrentVector<EdgeFull> savedEdges(10);

#endif

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
ConcurrentVector<EdgeFull> savedEdges(10);

#endif