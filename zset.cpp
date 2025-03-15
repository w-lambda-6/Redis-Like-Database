#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "zset.h"
#include "commonops.h"

static ZNode* znode_new(const char * name, size_t len, double score){
    ZNode* node = (ZNode*) malloc(sizeof(ZNode)+len);
    avl_init(&node->tree);
    node->hmap.next = nullptr;
    node->hmap.hval = str_hash((uint8_t*)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name[0], name, len);
    return node;
}

static void znode_del(ZNode* node){
    free(node);
}

static size_t min(size_t lhs, size_t rhs){
    return lhs < rhs ? lhs : rhs;
}

// compare by the (score, name) tuple, 
// returns whether lhs is less than rhs
static bool zless(AVLNode* lhs, AVLNode* rhs){
    ZNode* zl = container_of(lhs, ZNode, tree);
    ZNode* zr = container_of(rhs, ZNode, tree);
    if (zl->score!=zr->score){
        return zl->score < zr->score;
    }
    int ret = memcmp(zl->name, zr->name, min(zr->len, zl->len));
    return (ret != 0) ? (ret < 0) : (zl->len < zr->len);
}

static bool zless(AVLNode* node, double score, const char *name, size_t len){
    ZNode* zn = container_of(node, ZNode, tree);
    if (zn->score != score) {
        return zn->score < score;
    }
    int ret = memcmp(zn->name, name, min(zn->len, len));
    if (ret != 0){
        return ret < 0;
    }
    return zn->len < len;
}



//===============================name lookup in zset===============================//

struct HKey {
    HNode node;
    const char* name = nullptr;
    size_t len = 0;
};

static bool hcmp(HNode* node, HNode* key){
    ZNode* znode = container_of(node, ZNode, hmap);
    HKey* hkey = container_of(key, HKey, node);
    if (znode->len != hkey->len){
        return false;
    }
    return 0 == memcmp(znode->name, hkey->name, znode->len);
}

// lookup by name is just a hashtable lookup
ZNode* zset_lookup(ZSet* zset, const char* name, size_t len){
    if(!zset->root){
        return nullptr;
    }

    HKey key;
    key.node.hval = str_hash((uint8_t*)name, len);
    key.name = name;
    key.len = len;
    HNode* found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : nullptr;
}


//=============================== insertion into underlying AVL tree ===============================//

static void tree_insert(ZSet* zset, ZNode* node){
    AVLNode* parent = nullptr;      // insert under this node
    AVLNode** from = &zset->root;
    while(*from){
        parent = *from;
        from = zless(&node->tree, parent) ? &parent->left : &parent->right;
    }
    *from = &node->tree;    // attach the new node
    node->tree.parent = parent;
    zset->root = avl_balance(&node->tree);
}

// update the score of an existing node
static void zset_update(ZSet* zset, ZNode* node, double score){
    if (node->score == score){
        return;
    }
    // detach the tree node
    zset->root = avl_del(&node->tree);
    avl_init(&node->tree);
    // reinsert the tree node
    node->score = score;
    tree_insert(zset, node);
}

// add a new (score, name) tuple, returns whether `insertion` is successful
bool zset_insert(ZSet* zset, const char * name, size_t len, double score){
    ZNode* node = zset_lookup(zset, name, len);
    if (node) {
        zset_update(zset, node, score);
        return false;   // only updated, no insertion happened
    } else {
        node = znode_new(name, len, score);
        hm_insert(&zset->hmap, &node->hmap);
        tree_insert(zset, node);
        return true;
    }
}


//=============================== deletion from the zset ===============================//

void zset_delete(ZSet* zset, ZNode* node){      // no search and delete 
    HKey key;
    key.node.hval = node->hmap.hval;
    key.name = node->name;
    key.len = node->len;
    HNode* found = hm_delete(&zset->hmap, &key.node, &hcmp);
    assert(found);
    // remove from the tree
    zset->root = avl_del(&node->tree);
    znode_del(node);
}


/*
    below is related to sorted set range query
    +--------+-----+-------+------+--------+-------+-----+------+
    | ZQUERY | key | score | name | offset | limit | len | strn |
    +--------+-----+-------+------+--------+-------+-----+------+
*/

// seek to the first pair where pair >= (score, name)
ZNode* zset_seekge(ZSet *zset, double score, const char *name, size_t len){
    AVLNode *found = nullptr;
    for (AVLNode *node = zset->root; node;) {
        if (zless(node, score, name, len)){
            node = node->right;
        } else {
            found = node;       // potential candidate
            node = node->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : nullptr;
}

// walk to the n-th successor/predecessor(offset)
// offset and iterate (just walking the AVL tree)
ZNode* znode_offset(ZNode* node, int64_t offset){
    AVLNode* tnode = node ? avl_offset(&node->tree, offset) : nullptr;
    return tnode ? container_of(tnode, ZNode, tree) : nullptr;
}

// frees the avl tree and the znodes that contain it
static void tree_dispose(AVLNode *node){
    if (!node){
        return;
    }
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_del(container_of(node, ZNode, tree));
}

// destroy the zset
void zset_clear(ZSet* zset) {
    hm_clear(&zset->hmap);
    tree_dispose(zset->root);
    zset->root = nullptr;
}