#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "errhelp.h"  

// Helper function for returning messages
void msg(const char* msg) {
    fprintf(stderr, "%s\n", msg);
}

// Helper function for returning error messages
void msg_err(const char* msg){
    fprintf(stderr, "[errno:%d]%s", errno, msg);
}

// Kill the program if an error occurs
void die(const char* msg) {
    int err = errno;
    fprintf(stderr, "[%d]%s\n", err, msg);
    abort();
}