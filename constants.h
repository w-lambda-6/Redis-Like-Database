#pragma once
#include <stdlib.h>
#include "zset.h"


const size_t k_max_msg = 32<<20;                // likely larger than the kernel buffer
const size_t k_max_args = 200 * 1000;           // limit for number of queries/requests
const uint64_t k_idle_timeout_ms = 5*1000;      // timeout value for idle connections
const size_t k_max_works = 2000;                // limit for expired timer processing
static const ZSet k_empty_zset;                 // dummy empty zset used to tell if a zset exists or not