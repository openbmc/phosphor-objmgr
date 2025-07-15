#include "../internal.h"
#include "utils.h"

#include <gtest/gtest.h>

TEST(TestSarray, Dup)
{
    auto a = generate_test_sarray(3);

    auto size = sarraylen(a);
    EXPECT_EQ(size, 3);

    auto b = sarraydup(a);
    size_t i;

    for (i = 0; i < 4; i++)
    {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
        EXPECT_STREQ(a[i], b[i]);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }
    sarrayfree(a);
    sarrayfree(b);
}
