#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

// n must be a power of 2
static void h_init(HTab *htab, size_t n){
    assert(n>0 && ((n-1)&n)==0);
    htab->tab = (HNode**)calloc(n, sizeof(HNode *));
    htab->mask = n-1;
    htab->size = 0;
}

// insertion into hashtable linked lists, insert into the front
// this way the insertion operation is always o(1)
static void h_insert(HTab *htab, HNode *node){
    size_t pos = node->hval & htab->mask;
    HNode *next = htab->tab[pos];
    node->next = next;
    htab->tab[pos] = node;
    htab->size++;
}

// lookup subroutine
// returns the parent pointer that owns the target node
// and it can be used to delete the target node
// also uses hash value comparisons to rule out 
// candidates early
static HNode **h_lookup(HTab *htab, HNode *key, bool (*eq)(HNode*, HNode*)) {
    if (!htab->tab){
        return nullptr;
    }
    size_t pos = key->hval & htab->mask;
    HNode **from = &htab->tab[pos];
    for (HNode *cur; (cur=*from) != nullptr; from = &cur->next){
        if (cur->hval==key->hval && eq(cur, key)){
            return from;
        }
    }
    return nullptr;
}

// deleting nodes in the hash table
// no need to care about whether whether it is the first node or not
// h_lookup returns the address of the to be updated pointer, 
// doesn't matter if it is from a node or a slot
static HNode *h_detach(HTab *htab, HNode** from){
    HNode* node = *from;    // the target node
    *from = node->next;     // update the incoming pointer to the target
    htab->size--;
    return node;
}



//========================code for resizable hash table========================//

const size_t k_rehashing_work = 128;    // constant work

// moves some keys to the newer table
// can also be triggered from lookup and deletes
// it only does o(1) work
static void hm_rehash(HMap *hmap){
    size_t nwork = 0;
    while( nwork < k_rehashing_work && hmap->older.size>0){
        // find a non-empty slot
        HNode **from = &hmap->older.tab[hmap->migrate_pos];
        if(!*from){
            hmap->migrate_pos++;
            continue;   // empty slot
        }
        // move the first list item to the newer table
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }
    // discard old table if it becomes empty
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTab{};
    }
}

static void hm_trigger_rehash(HMap* hmap){
    assert(hmap->older.tab==nullptr);
    hmap->older = hmap->newer;
    h_init(&hmap->newer, (hmap->newer.mask+1)*2);
    hmap->migrate_pos = 0;
}

// during reshashing we may need to lookup both tables
HNode* hm_lookup(HMap *hmap, HNode *key, bool (*eq)(HNode*, HNode*)){
    hm_rehash(hmap);
    HNode **from = h_lookup(&hmap->newer, key, eq);
    if (!from){
        from = h_lookup(&hmap->older, key, eq);
    }
    return from ? *from : nullptr;
}

// during rehashing we might need to delete from both tables
HNode* hm_delete(HMap* hmap, HNode* key, bool(*eq)(HNode* , HNode*)){
    hm_rehash(hmap);
    if (HNode** from = h_lookup(&hmap->newer, key, eq)){
        return h_detach(&hmap->newer, from);
    }
    if (HNode** from = h_lookup(&hmap->older, key, eq)){
        return h_detach(&hmap->older, from);
    }
    return nullptr;
}

// maximum load factor
const size_t k_max_load_factor = 8;

// insertion triggers rehashing when the load factor is high
void hm_insert(HMap *hmap, HNode *node){
    if (!hmap->newer.tab){
        h_init(&hmap->newer, 4);    // initialise if empty
    }
    h_insert(&hmap->newer, node);
    if (!hmap->older.tab){
        size_t threshold = (hmap->newer.mask+1)*k_max_load_factor;
        if (hmap->newer.size>=threshold){
            hm_trigger_rehash(hmap);
        }
    }
    hm_rehash(hmap);    // migrate some keys
}

void hm_clear(HMap *hmap){
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HMap{};
}

size_t hm_size(HMap *hmap){
    return hmap->newer.size+hmap->older.size;
}

static bool h_foreach(HTab *htab, bool (*f)(HNode *, void *), void *arg){
    for (size_t i = 0; htab->mask != 0 && i <= htab->mask; i++){
        for (HNode *node = htab->tab[i]; node != nullptr; node = node->next){
            if (!f(node, arg)){
                return false;
            }
        }
    }
    return true;
}

void hm_foreach(HMap *hmap, bool (*f)(HNode*, void *), void *arg){
    h_foreach(&hmap->newer, f, arg)&&h_foreach(&hmap->older, f, arg);
}