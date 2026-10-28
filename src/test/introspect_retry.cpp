#include "src/introspect_retry.hpp"

#include <chrono>
#include <random>

#include <gtest/gtest.h>

using std::chrono::seconds;

TEST(IntrospectRetryDelay, BacksOffExponentially)
{
    EXPECT_EQ(introspectRetryDelay(1), seconds{1});
    EXPECT_EQ(introspectRetryDelay(2), seconds{2});
    EXPECT_EQ(introspectRetryDelay(3), seconds{4});
    EXPECT_EQ(introspectRetryDelay(4), seconds{8});
    EXPECT_EQ(introspectRetryDelay(5), seconds{16});
    EXPECT_EQ(introspectRetryDelay(6), seconds{32});
}

TEST(IntrospectRetryDelay, IsCappedAtOneMinute)
{
    EXPECT_EQ(introspectRetryDelay(7), seconds{60});
    EXPECT_EQ(introspectRetryDelay(8), seconds{60});
    EXPECT_EQ(introspectRetryDelay(1000), seconds{60});
}

TEST(IntrospectRetryDelay, HandlesZeroTimeouts)
{
    EXPECT_EQ(introspectRetryDelay(0), seconds{1});
}

TEST(AddRetryJitter, StaysWithinBounds)
{
    std::minstd_rand rng(1);
    for (int i = 0; i < 1000; ++i)
    {
        const std::chrono::milliseconds delay = addRetryJitter(seconds{8}, rng);
        EXPECT_GE(delay, std::chrono::milliseconds{8000});
        EXPECT_LE(delay, std::chrono::milliseconds{10000});
    }
}

TEST(ShouldLogRetry, IsThrottled)
{
    EXPECT_FALSE(shouldLogRetry(1));
    EXPECT_FALSE(shouldLogRetry(3));
    EXPECT_TRUE(shouldLogRetry(4));
    EXPECT_FALSE(shouldLogRetry(5));
    EXPECT_TRUE(shouldLogRetry(10));
    EXPECT_FALSE(shouldLogRetry(11));
    EXPECT_TRUE(shouldLogRetry(20));
}

TEST(IntrospectGenerations, StartReturnsCurrentGeneration)
{
    IntrospectGenerations generations;
    const uint64_t g = generations.start("a");
    EXPECT_TRUE(generations.isCurrent("a", g));
    EXPECT_EQ(generations.start("a"), g);
    EXPECT_FALSE(generations.isCurrent("b", g));
}

TEST(IntrospectGenerations, OwnerChangeStopsRetrying)
{
    IntrospectGenerations generations;
    const uint64_t g1 = generations.start("a");
    generations.ownerChanged("a", true);
    EXPECT_FALSE(generations.isCurrent("a", g1));
    const uint64_t g2 = generations.start("a");
    EXPECT_NE(g1, g2);
    EXPECT_TRUE(generations.isCurrent("a", g2));
}

TEST(IntrospectGenerations, ReleaseRemovesEntry)
{
    IntrospectGenerations generations;
    const uint64_t g = generations.start("a");
    generations.ownerChanged("a", false);
    EXPECT_FALSE(generations.isCurrent("a", g));
    EXPECT_EQ(generations.size(), 0);

    // A new owner gets a generation that differs from all previous ones.
    generations.ownerChanged("a", true);
    EXPECT_FALSE(generations.isCurrent("a", g));
    EXPECT_EQ(generations.size(), 1);
}
