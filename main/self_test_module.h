#ifndef SELFT_TEST_MODULE_H_
#define SELFT_TEST_MODULE_H_

typedef struct
{
    bool is_active;
    bool is_finished;
    char * message;
    char *result;
    char *finished;
} SelfTestModule;

extern SelfTestModule SELF_TEST_MODULE;

#endif