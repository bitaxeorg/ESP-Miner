#ifndef BZM_STATUS_H
#define BZM_STATUS_H
typedef int bzm_status_t;
enum {
    BZM_OK = 0,
    BZM_ERR_IO = -1,
    BZM_ERR_INVALID_ARG = -2,
    BZM_ERR_INVALID_STATE = -3,
    BZM_ERR_NOT_SUPPORTED = -4,
    BZM_ERR_NO_MEM = -5,
    BZM_ERR_TIMEOUT = -6,
};
#endif
