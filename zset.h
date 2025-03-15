#pragma once

#include "avl.h"
#include "hashtable.h"

struct ZSet {
    AVLNode *root = nullptr;    // index by (score, name)
    HMap hmap;                  // index by name
};

struct ZNode {
    // data struture nodes
    AVLNode tree;
    HNode hmap;
    // data 
    double score = 0;
    size_t len = 0;
    char name[0];       // flexible array, used to embed the string into 
                        // the node to reduce memory allocations
};

bool    zset_insert(ZSet* zset, const char *name, size_t len, double score);
ZNode*  zset_lookup(ZSet* zset, const char *name, size_t len);
void    zset_delete(ZSet* zset, ZNode* node);
ZNode*  zset_seekge(ZSet* zset, double score, const char *name, size_t len);
void    zset_clear(ZSet* zset);
ZNode*  znode_offset(ZNode* node, int64_t offset);