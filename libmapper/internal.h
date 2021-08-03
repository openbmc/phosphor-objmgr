#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int sarraylen(char* array[]);
void sarrayfree(char* array[]);
char** sarraydup(char* array[]);
#ifdef __cplusplus
}
#endif
