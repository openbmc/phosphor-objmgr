#include "utils.h"

#include <stdlib.h>
#include <string.h>

char** generate_test_sarray(size_t len)
{
    static const char testString[] = "test";
    size_t i;
    char** ret = calloc(len + 1, sizeof(*ret));
    if (!ret)
    {
        return NULL;
    }

    for (i = 0; i < len; ++i)
    {
        ret[i] = strdup(testString);
        if (!ret[i])
        {
            goto error;
        }
    }

    return ret;

error:
    for (i = 0; i < len; ++i)
    {
        free(ret[i]);
    }
    free(ret);

    return NULL;
}
