// 1. For TTL cache we use a heap as the timeout value is not fixed
// 2. We use an array-encoded heap for this one, meaning no dynamic allocations

# pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct HeapNode {
    uint64_t ttl_val;
    size_t* ref;        // points to Entry::heap_idx
};

void heap_update_pos(HeapNode* arr, size_t pos, size_t len);
void heap_delete(std::vector<HeapNode>& arr, size_t pos);
void heap_upsert(std::vector<HeapNode>& arr, size_t pos, HeapNode t);