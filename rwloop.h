#include <stdint.h>
#include <stddef.h>

#ifndef RWLOOP_H
#define RWLOOP_H

int32_t read_full(int fd, char* buf, size_t n);
int32_t write_all(int fd, const char* buf, size_t n);

#endif