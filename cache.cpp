#include "cache.h"
#include <vector>

static size_t heap_left(size_t i){
    return 2*i+1;
}

static size_t heap_right(size_t i){
    return 2*i+2;
}

static size_t heap_parent(size_t i){
    return (i+1)/2 - 1;
}

// update the node's position when it becomes smaller than its parents
static void heap_float(HeapNode* arr, size_t pos){
    // pointer arithemtic disguised as array access
    HeapNode t = arr[pos];
    while(pos > 0 && arr[heap_parent(pos)].ttl_val > t.ttl_val){
        // swap with the parent
        arr[pos] = arr[heap_parent(pos)];
        pos = heap_parent(pos);
    }
    arr[pos] = t;
    *arr[pos].ref = pos;
}

// update the node's position when it becomes greater than its parents
static void heap_sink(HeapNode* arr, size_t pos, size_t len){
    HeapNode t = arr[pos];
    while(true){
        size_t l = heap_left(pos);
        size_t r = heap_right(pos);
        size_t min_pos = pos;
        uint64_t min_val = t.ttl_val;
        if (l < len && arr[l].ttl_val < min_val){
            min_pos = l;
            min_val = arr[l].ttl_val;
        }
        if (r < len && arr[r].ttl_val < min_val){
            min_pos = r;
            min_val = arr[r].ttl_val;
        }
        if (min_pos == pos){
            break;
        }
        // swap with the smaller child
        arr[pos] = arr[min_pos];
        pos = min_pos;
    }
    arr[pos] = t;
    *arr[pos].ref = pos;
}

// doesn't actually update the value, that is left to the application code
// what it does is update the node position after its value is updated
void heap_update_pos(HeapNode* arr, size_t pos, size_t len){
    if (pos > 0 && arr[heap_parent(pos)].ttl_val > arr[pos].ttl_val){
        heap_float(arr, pos);
    } else {
        heap_sink(arr, pos, len);
    }
}

void heap_delete(std::vector<HeapNode>& arr, size_t pos){
    // swap erased item with the last item
    arr[pos] = arr.back();
    arr.pop_back();
    if (pos < arr.size()){
        heap_update_pos(arr.data(), pos, arr.size());
    }
}

// update or insert an entry
void heap_upsert(std::vector<HeapNode>& arr, size_t pos, HeapNode t){
    if (pos < arr.size()){
        arr[pos] = t;
    } else {
        pos = arr.size();
        arr.push_back(t);
    }
    heap_update_pos(arr.data(), pos, arr.size());
}