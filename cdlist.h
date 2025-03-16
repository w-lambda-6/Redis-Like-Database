// circular doubly linked list for timers

#include <stddef.h>

struct CDNode {
    CDNode* prev = nullptr;
    CDNode* next = nullptr;
};

// dummy node with a circular reference to itself
inline void cdlist_init(CDNode* node) {
    node->prev = node->next = node;
}

// a list that only contains the dummy node
inline bool cdlist_empty(CDNode* node){
    return node->prev==node;
}

inline void cdlist_detach(CDNode* node){
    CDNode* prev = node->prev;
    CDNode* next = node->next;
    prev->next = next;
    next->prev = prev;
}

inline void cdlist_insert_before(CDNode* target, CDNode* rookie){
    CDNode* prev = target->prev;
    prev->next = rookie;
    rookie->prev = prev;
    rookie->next = target;
    target->prev = rookie;
}