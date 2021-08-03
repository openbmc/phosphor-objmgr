extern "C" {
#include "../internal.h"
#include "utils.h"
}

#include <gtest/gtest.h>

TEST(TestSarray, Length)
{
    auto a = generate_test_sarray(3);
    auto size = sarraylen(a);
    EXPECT_EQ(size, 3);
    sarrayfree(a);
}

TEST(TestSarray, Dup)
{
    auto a = generate_test_sarray(3);
    auto b = sarraydup(a);
    size_t i;

    for (i = 0; i < 4; i++)
    {
        EXPECT_STREQ(a[i], b[i]);
    }
    sarrayfree(a);
    sarrayfree(b);
}

TEST(TestSarray, Free)
{
    auto a = generate_test_sarray(3);
    auto b = sarraydup(a);
    sarrayfree(a);
    sarrayfree(b);
}
