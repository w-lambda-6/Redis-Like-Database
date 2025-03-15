#pragma once

#include <stddef.h>
#include <stdint.h>

// intrusive data structure
#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) );})


// the FNV hash function we use for our hash table
inline uint64_t str_hash(const uint8_t* data, size_t len){
    uint64_t h=0x811C9DC5;
    for (size_t i = 0; i < len; i++){
        h = (h+data[i])*0x01000193;
    }
    return h;
}