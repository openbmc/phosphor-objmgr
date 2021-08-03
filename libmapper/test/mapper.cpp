extern "C" {
#include "../internal.h"
}

#include <gtest/gtest.h>

auto generateTestSarray(size_t len)
{
    static const std::string testString = "hello";
    size_t i;
    auto ret = new char*[len + 1];
    ret[len] = nullptr;
    for (i = 0; i < len; ++i)
    {
        ret[i] = new char[testString.size() + 1];
        testString.copy(ret[i], testString.size());
        ret[i][testString.size()] = '\0';
    }

    return ret;
}

TEST(TestSarray, Length)
{
    auto a = generateTestSarray(3);
    auto size = sarraylen(a);
    EXPECT_EQ(size, 3);
}

TEST(TestSarray, Dup)
{
    auto a = generateTestSarray(3);
    auto b = sarraydup(a);
    size_t i;

    for (i = 0; i < 4; i++)
    {
        EXPECT_STREQ(a[i], b[i]);
    }
}

TEST(TestSarray, Free)
{
    auto a = generateTestSarray(3);
    auto b = sarraydup(a);
    sarrayfree(a);
    sarrayfree(b);
}
