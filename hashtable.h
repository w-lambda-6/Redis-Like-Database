#pragma once

#include <stdlib.h>
#include <stdint.h>




struct HNode {
    HNode *next = nullptr;
    uint64_t hval = 0;  // the hash value
};

struct HTab {
    HNode **tab = nullptr;  // an array of slots
    // 2^n-1, used to get index quickly via mod
    size_t mask = 0;        
    size_t size = 0;        // # of keys
};

// the resizable hash table interface based on the fixed size hash table
// normally the newer table is the one being used and the older one is not
// when the load gets heavy, the new one is moved to the older one's spot
// and the new one is replace with an empty table double the size  
struct HMap{
    HTab newer;
    HTab older;
    size_t migrate_pos = 0;
};

// the set, get, del interfaces
HNode* hm_lookup(HMap* hmap, HNode* key, bool (*eq)(HNode* , HNode*));
void   hm_insert(HMap* hmap, HNode* node);
HNode* hm_delete(HMap* hmap, HNode* key, bool (*eq)(HNode* , HNode*));
void   hm_clear(HMap* hmap);
size_t hm_size(HMap* hmap);
// invoke the callback on each node until it returns false
void hm_foreach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);
// used to check if 2 hnode pointers are pointing to the same one or not
bool hnode_same(HNode* node, HNode* key);