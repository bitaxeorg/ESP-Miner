#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>    

#include "pretty.h"

void prettyHex(unsigned char * buf, int len) {
    int i;
    printf("[");
    for (i = 0; i < len-1; i++) {
        printf("%02X ", buf[i]);
    }
    printf("%02X]\n", buf[len-1]);
}

//flip byte order of a 32 bit integer
uint32_t flip32(uint32_t val) {
    uint32_t ret = 0;
    ret |= (val & 0xFF) << 24;
    ret |= (val & 0xFF00) << 8;
    ret |= (val & 0xFF0000) >> 8;
    ret |= (val & 0xFF000000) >> 24;
    return ret;
}
