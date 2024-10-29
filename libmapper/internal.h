#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t sarraylen(char *array[]);
void sarrayfree(char *array[]);
char **sarraydup(char *array[]);
#ifdef __cplusplus
}
#endif
