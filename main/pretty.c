#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>    

#include "pretty.h"

void prettyHex(unsigned char * buf, int len) {
    int i;
    for (i = 0; i < len; i++) {
        if ((i > 0) && (buf[i] == 0xAA) && (buf[i+1] == 0x55))
            printf("\n");
        printf("%02X ", buf[i]);
    }
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
