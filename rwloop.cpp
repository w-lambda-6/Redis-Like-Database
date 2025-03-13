#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include "rwloop.h"

int32_t read_full(int fd, char* buf, size_t n){
    while(n > 0){
        ssize_t ret = read(fd, buf, n);
        if (ret <= 0){
            return -1;      // error or unexpected EOF
        }
        assert((size_t)ret <= n);
        n -= (size_t)ret;
        buf += ret;
    }
    return 0;
}

int32_t write_all(int fd, const char* buf, size_t n){
    while(n>0){
        ssize_t ret = write(fd, buf, n);
        if (ret <= 0){
            return -1;      // error
        }
        assert((size_t)ret<=n);
        n -= (size_t)ret;
        buf += ret;
    }
    return 0;
}