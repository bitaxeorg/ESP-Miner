/**
 * @file mock_nvs.c
 * @brief Mock NVS implementation for host-based testing
 */

#include "mock_nvs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Maximum entries in mock storage */
#define MAX_NVS_ENTRIES 256
#define MAX_KEY_LEN 32
#define MAX_NAMESPACE_LEN 16
#define MAX_STRING_LEN 256

typedef enum {
    NVS_TYPE_U8,
    NVS_TYPE_I8,
    NVS_TYPE_U16,
    NVS_TYPE_I16,
    NVS_TYPE_U32,
    NVS_TYPE_I32,
    NVS_TYPE_U64,
    NVS_TYPE_STRING,
    NVS_TYPE_BLOB
} nvs_value_type_t;

typedef struct {
    char namespace[MAX_NAMESPACE_LEN];
    char key[MAX_KEY_LEN];
    nvs_value_type_t type;
    union {
        uint8_t u8;
        int8_t i8;
        uint16_t u16;
        int16_t i16;
        uint32_t u32;
        int32_t i32;
        uint64_t u64;
        char str[MAX_STRING_LEN];
    } value;
    int write_count;
    bool valid;
} nvs_entry_t;

typedef struct {
    char namespace[MAX_NAMESPACE_LEN];
    nvs_open_mode_t mode;
    bool valid;
} nvs_handle_info_t;

/* Mock storage */
static nvs_entry_t g_entries[MAX_NVS_ENTRIES];
static int g_entry_count = 0;
static nvs_handle_info_t g_handles[32];
static int g_handle_count = 0;
static int g_commit_count = 0;
static bool g_initialized = false;

/* ============================================
 * Internal helpers
 * ============================================ */

static nvs_entry_t* find_entry(const char* namespace, const char* key)
{
    for (int i = 0; i < g_entry_count; i++) {
        if (g_entries[i].valid &&
            strcmp(g_entries[i].namespace, namespace) == 0 &&
            strcmp(g_entries[i].key, key) == 0) {
            return &g_entries[i];
        }
    }
    return NULL;
}

static nvs_entry_t* create_entry(const char* namespace, const char* key)
{
    /* First try to find existing */
    nvs_entry_t* entry = find_entry(namespace, key);
    if (entry) return entry;

    /* Create new */
    if (g_entry_count >= MAX_NVS_ENTRIES) return NULL;

    entry = &g_entries[g_entry_count++];
    strncpy(entry->namespace, namespace, MAX_NAMESPACE_LEN - 1);
    strncpy(entry->key, key, MAX_KEY_LEN - 1);
    entry->write_count = 0;
    entry->valid = true;
    return entry;
}

static nvs_handle_info_t* get_handle_info(nvs_handle_t handle)
{
    if (handle == 0 || handle > (uint32_t)g_handle_count) return NULL;
    nvs_handle_info_t* info = &g_handles[handle - 1];
    if (!info->valid) return NULL;
    return info;
}

/* ============================================
 * Mock Control Functions
 * ============================================ */

void mock_nvs_init(void)
{
    memset(g_entries, 0, sizeof(g_entries));
    memset(g_handles, 0, sizeof(g_handles));
    g_entry_count = 0;
    g_handle_count = 0;
    g_commit_count = 0;
    g_initialized = true;
}

void mock_nvs_clear(void)
{
    mock_nvs_init();
}

void mock_nvs_set_u16(const char* namespace, const char* key, uint16_t value)
{
    nvs_entry_t* entry = create_entry(namespace, key);
    if (entry) {
        entry->type = NVS_TYPE_U16;
        entry->value.u16 = value;
    }
}

void mock_nvs_set_string(const char* namespace, const char* key, const char* value)
{
    nvs_entry_t* entry = create_entry(namespace, key);
    if (entry) {
        entry->type = NVS_TYPE_STRING;
        strncpy(entry->value.str, value, MAX_STRING_LEN - 1);
    }
}

bool mock_nvs_was_written(const char* namespace, const char* key)
{
    nvs_entry_t* entry = find_entry(namespace, key);
    return entry && entry->write_count > 0;
}

int mock_nvs_get_write_count(const char* namespace, const char* key)
{
    nvs_entry_t* entry = find_entry(namespace, key);
    return entry ? entry->write_count : 0;
}

int mock_nvs_get_commit_count(void)
{
    return g_commit_count;
}

/* ============================================
 * NVS API Implementation
 * ============================================ */

int nvs_flash_init(void)
{
    if (!g_initialized) {
        mock_nvs_init();
    }
    return 0; /* ESP_OK */
}

int nvs_flash_erase(void)
{
    mock_nvs_clear();
    return 0;
}

int nvs_open(const char* namespace, nvs_open_mode_t mode, nvs_handle_t* handle)
{
    if (!g_initialized) return ESP_ERR_NVS_NOT_INITIALIZED;
    if (!namespace || !handle) return 0x102; /* ESP_ERR_INVALID_ARG */
    if (g_handle_count >= 32) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    nvs_handle_info_t* info = &g_handles[g_handle_count];
    strncpy(info->namespace, namespace, MAX_NAMESPACE_LEN - 1);
    info->mode = mode;
    info->valid = true;
    g_handle_count++;

    *handle = (nvs_handle_t)g_handle_count;
    return 0;
}

void nvs_close(nvs_handle_t handle)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (info) {
        info->valid = false;
    }
}

int nvs_get_u8(nvs_handle_t handle, const char* key, uint8_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_U8) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.u8;
    return 0;
}

int nvs_set_u8(nvs_handle_t handle, const char* key, uint8_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_U8;
    entry->value.u8 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_i8(nvs_handle_t handle, const char* key, int8_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_I8) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.i8;
    return 0;
}

int nvs_set_i8(nvs_handle_t handle, const char* key, int8_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_I8;
    entry->value.i8 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_u16(nvs_handle_t handle, const char* key, uint16_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_U16) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.u16;
    return 0;
}

int nvs_set_u16(nvs_handle_t handle, const char* key, uint16_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_U16;
    entry->value.u16 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_i16(nvs_handle_t handle, const char* key, int16_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_I16) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.i16;
    return 0;
}

int nvs_set_i16(nvs_handle_t handle, const char* key, int16_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_I16;
    entry->value.i16 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_u32(nvs_handle_t handle, const char* key, uint32_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_U32) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.u32;
    return 0;
}

int nvs_set_u32(nvs_handle_t handle, const char* key, uint32_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_U32;
    entry->value.u32 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_i32(nvs_handle_t handle, const char* key, int32_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_I32) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.i32;
    return 0;
}

int nvs_set_i32(nvs_handle_t handle, const char* key, int32_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_I32;
    entry->value.i32 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_u64(nvs_handle_t handle, const char* key, uint64_t* out_value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_U64) return ESP_ERR_NVS_TYPE_MISMATCH;

    *out_value = entry->value.u64;
    return 0;
}

int nvs_set_u64(nvs_handle_t handle, const char* key, uint64_t value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_U64;
    entry->value.u64 = value;
    entry->write_count++;
    return 0;
}

int nvs_get_str(nvs_handle_t handle, const char* key, char* out_value, size_t* length)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;
    if (entry->type != NVS_TYPE_STRING) return ESP_ERR_NVS_TYPE_MISMATCH;

    size_t str_len = strlen(entry->value.str) + 1;

    if (out_value == NULL) {
        /* Just return required length */
        *length = str_len;
        return 0;
    }

    if (*length < str_len) {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }

    strcpy(out_value, entry->value.str);
    *length = str_len;
    return 0;
}

int nvs_set_str(nvs_handle_t handle, const char* key, const char* value)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    if (strlen(value) >= MAX_STRING_LEN) return ESP_ERR_NVS_VALUE_TOO_LONG;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_STRING;
    strncpy(entry->value.str, value, MAX_STRING_LEN - 1);
    entry->write_count++;
    return 0;
}

int nvs_get_blob(nvs_handle_t handle, const char* key, void* out_value, size_t* length)
{
    /* For simplicity, treat blob same as string in mock */
    return nvs_get_str(handle, key, (char*)out_value, length);
}

int nvs_set_blob(nvs_handle_t handle, const char* key, const void* value, size_t length)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    if (length >= MAX_STRING_LEN) return ESP_ERR_NVS_VALUE_TOO_LONG;

    nvs_entry_t* entry = create_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

    entry->type = NVS_TYPE_BLOB;
    memcpy(entry->value.str, value, length);
    entry->write_count++;
    return 0;
}

int nvs_erase_key(nvs_handle_t handle, const char* key)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    nvs_entry_t* entry = find_entry(info->namespace, key);
    if (!entry) return ESP_ERR_NVS_NOT_FOUND;

    entry->valid = false;
    return 0;
}

int nvs_erase_all(nvs_handle_t handle)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;
    if (info->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;

    for (int i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].namespace, info->namespace) == 0) {
            g_entries[i].valid = false;
        }
    }
    return 0;
}

int nvs_commit(nvs_handle_t handle)
{
    nvs_handle_info_t* info = get_handle_info(handle);
    if (!info) return ESP_ERR_NVS_INVALID_HANDLE;

    g_commit_count++;
    return 0;
}
