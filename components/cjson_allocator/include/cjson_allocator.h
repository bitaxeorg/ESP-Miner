#ifndef CJSON_ALLOCATOR_H
#define CJSON_ALLOCATOR_H

// Call once at startup, before any cJSON use or tasks that use cJSON.
void cjson_allocator_init(void);

#endif
