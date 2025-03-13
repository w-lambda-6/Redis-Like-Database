#include <stdlib.h>

#ifndef CONSTANTS_H
#define CONSTANTS_H
const size_t k_max_msg = 32<<20;    // likely larger than the kernel buffer
const size_t k_max_args = 200 * 1000;
#endif