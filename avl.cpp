#include <assert.h>
#include "avl.h"

// comparator function for searching node in tree
static uint32_t max(uint32_t lhs, uint32_t rhs){
    return lhs < rhs ? rhs : lhs;
}

// updates the height and size field of an AVL tree
// changes are bottom-up so a change at the bottom propagates up
static void avl_update(AVLNode *node){
    node->height = 1 + max(avl_height(node->left), avl_height(node->right));
    node->size = 1 + avl_size(node->left) + avl_size(node->right);
}

static AVLNode* avl_rot_left(AVLNode *node){
    AVLNode* from = node->parent;
    AVLNode* new_node = node->right;
    AVLNode* inner = new_node->left;

    node->right = inner;
    if (inner) {
        inner->parent = node;
    }
    new_node->parent = from;    // we don't use AVLNode** anymore as the parent can be NULL
                                // so the parent to child link is not updated, it is left
                                // to the caller to decide whether to update the link or not
    new_node->left = node;
    node->parent = new_node;

    // update auxiliary data
    avl_update(node);
    avl_update(new_node);
    return new_node;
}


static AVLNode* avl_rot_right(AVLNode *node){
    AVLNode* from = node->parent;
    AVLNode* new_node = node->left;
    AVLNode* inner = new_node->right;

    node->left = inner;
    if (inner){
        inner->parent = node;
    }
    new_node->parent = from;

    new_node->right = node;
    node->parent = new_node;

    avl_update(node);
    avl_update(new_node);
    return new_node;
}

// when the left subtree of node is taller by 2
static AVLNode* avl_balance_left(AVLNode* node){
    if (avl_height(node->left->left) < avl_height(node->left->right)) {
        node->left = avl_rot_left(node->left);
    }
    return avl_rot_right(node);
}

// when the right subtree of node is taller by 2
static AVLNode* avl_balance_right(AVLNode* node){
    if (avl_height(node->right->left) > avl_height(node->right->right)) {
        node->left = avl_rot_right(node->left);
    }
    return avl_rot_left(node);
}


/*
    called on an updated node
    - propagate auxiliary data to the root
    - fix imbalances
    - return the new root node (as the root pointer is not stored)
*/
AVLNode* avl_balance(AVLNode* node){
    while(true){
        // save the fixed subtree
        AVLNode** from = &node;
        AVLNode* parent = node->parent;
        if (parent) {
            // attach fixed subtree to the parent
            from = parent->left == node ? &parent->left : &parent->right;
        }   // else save to local variable `node`
        avl_update(node);
        uint32_t hl = avl_height(node->left);
        uint32_t hr = avl_height(node->right);
        // result assigned to parent as height may be changed
        if (hl == hr+2){
            *from = avl_balance_left(node);
        } else {
            *from = avl_balance_right(node);
        }
        
        if (!parent){
            return *from;
        }

        // continue to the parent as the height may be changed
        node = parent;
    }
}


// detach the node has only one children
static AVLNode *avl_del_easy(AVLNode *node){
    assert(!node->left || !node->right);
    AVLNode *child = node->left ? node->left : node->right;
    AVLNode *parent = node->parent;

    // update the childs' parent pointer
    // and attach the child to grandparent
    if (child) {
        child->parent = parent;     // can be null
    }
    if (!parent) {
        return child;   // node being detached is root
    }

    // change where the parent is pointing at
    AVLNode **from = parent->left == node ? &parent->left : &parent->right;
    *from = child;
    // rebalance the tree after deleting a node
    return avl_balance(parent);
}

// detach a node and return the new root of the tree
AVLNode* avl_del(AVLNode *node){
    if (!node->left || !node->right){
        return avl_del_easy(node);
    }

    // find successor
    AVLNode *victim = node->right;
    while(victim->left){
        victim = victim->left;
    }
    // detach the successor
    AVLNode* root = avl_del_easy(victim);
    // swap with the successor

    *victim = *node;
    if (victim->left){
        victim->left->parent = victim;
    }
    if (victim->right){
        victim->right->parent = victim;
    }

    AVLNode **from = &root;
    AVLNode *parent = node->parent;
    if (parent){
        from = parent->left == node ? &parent->left : &parent->right;
    }
    *from = victim;
    return root;
}

// offset into the succeeding or preceding node
// worst case is O(log N) regardless of offset length
// the rank difference refers to the rank in terms of an in-order traversal
AVLNode* avl_offset(AVLNode* node, int64_t offset) {
    // rank difference from starting node
    int64_t pos = 0;
    while(offset != pos){
        if (pos < offset && pos + avl_size(node->right) >= offset) {
            node = node->right;
            pos += avl_size(node->left)+1;
        } else if (pos > offset && pos-avl_size(node->left) <= offset) {
            node = node->left;
            pos -= avl_size(node->right)+1;
        } else {    // current node is not a direct ancestor of the node we are looking for
            AVLNode* parent = node->parent;
            if (!parent){
                return nullptr;
            }
            if (parent->right == node){
                pos -= avl_size(node->left)+1;
            } else {
                pos += avl_size(node->right)+1;
            }
            node = parent;
        }
    }
    return node;
}



/*
    not used
*/
// the height difference can be stored in 2 bits as only 3 valid height
// differences and this is stored in the 2 LSBs of the node pointer
static uint8_t avl_h_diff(AVLNode *node){
    uintptr_t p = (uintptr_t)node->parent;
    return p & 0b11;    // get data stored in 2 LSBs
}
// clears the height difference info and gets the actual pointer to node
static AVLNode *avl_get_parent(AVLNode *node){
    uintptr_t p = (uintptr_t)node->parent;
    return (AVLNode *)(p&(~0b11));  // clear the LSBs
}