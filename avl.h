#pragma once

#include <stddef.h>
#include <stdint.h>


struct AVLNode {
    AVLNode *parent = nullptr;
    AVLNode *left = nullptr;
    AVLNode *right = nullptr;
    // subtree height and count
    uint32_t height = 0;
    uint32_t size = 0;          // useful for speeding up rank queries
};

inline void avl_init(AVLNode *node){
    node->left = node->right = node->parent = nullptr;
    node->height = 1;
    node->size = 1;
}

// helper functions 
inline uint32_t avl_height(AVLNode *node){
    return !node ? 0 : node->height;
}

inline uint32_t avl_size(AVLNode *node){
    return !node ? 0 : node->size;
}

// APIs
AVLNode* avl_balance(AVLNode *node);
AVLNode* avl_del(AVLNode *node);
AVLNode* avl_offset(AVLNode* node, int64_t offset);