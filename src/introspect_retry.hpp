#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>

/** @brief Returns the delay before retrying an Introspect call
 *
 * The ObjectMapper retries Introspect calls that time out for as long as the
 * D-Bus service still owns its bus name. Back off exponentially to avoid
 * overloading the system bus further while the BMC is busy.
 *
 * @param[in] timeouts - Number of times the Introspect call timed out (>= 1)
 *
 * @return The delay before the next attempt: 1, 2, 4, ... seconds, capped at
 *         60 seconds.
 */
inline std::chrono::seconds introspectRetryDelay(unsigned int timeouts)
{
    constexpr unsigned int maxShift = 6;
    constexpr std::chrono::seconds maxDelay{60};
    const unsigned int shift =
        std::min(timeouts > 0 ? timeouts - 1 : 0, maxShift);

    return std::min(std::chrono::seconds{1U << shift}, maxDelay);
}

/** @brief Returns delay plus a random amount of up to a quarter of delay
 *
 * Spreading retries avoids that many requests that timed out at the same
 * time are retried at the same time, which would recreate the load burst
 * that caused the timeouts.
 *
 * @param[in] delay - Delay returned by introspectRetryDelay()
 * @param[in] rng   - Random number generator
 */
template <typename Rng>
std::chrono::milliseconds addRetryJitter(std::chrono::seconds delay, Rng& rng)
{
    const std::chrono::milliseconds base = delay;
    std::uniform_int_distribution<std::chrono::milliseconds::rep> jitter(
        0, base.count() / 4);
    return base + std::chrono::milliseconds{jitter(rng)};
}

/** @brief Whether or not to log that a request timed out
 *
 * Log the fourth timeout and every tenth timeout (10, 20, ...) to keep the
 * log size bounded if a service never answers.
 *
 * @param[in] timeouts - Number of times the request timed out (>= 1)
 */
inline bool shouldLogRetry(unsigned int timeouts)
{
    return timeouts == 4 || (timeouts > 4 && timeouts % 10 == 0);
}

/** @brief Tracks the owner changes of well-known names
 *
 * Each owner of a well-known name is identified by a generation number. The
 * generation number of an introspection is recorded when the introspection
 * starts. Retrying stops once the generation number of the name differs from
 * the recorded generation number, i.e. once the name has been released or
 * has a new owner. The entry for a name is removed when the name is
 * released such that the number of entries is bounded by the number of
 * names that have an owner.
 */
class IntrospectGenerations
{
  public:
    /** @brief Record that the owner of name changed
     *
     * @param[in] name     - Well-known name
     * @param[in] hasOwner - Whether or not name has a new owner
     */
    void ownerChanged(const std::string& name, bool hasOwner)
    {
        if (!hasOwner)
        {
            generations.erase(name);
            return;
        }
        generations[name] = ++lastGeneration;
    }

    /** @brief Returns the generation number for a new introspection of name
     */
    uint64_t start(const std::string& name)
    {
        uint64_t& generation = generations[name];
        if (generation == 0)
        {
            generation = ++lastGeneration;
        }
        return generation;
    }

    /** @brief Whether or not the owner of name has not changed since the
     *         introspection with the specified generation number started
     */
    bool isCurrent(const std::string& name, uint64_t generation) const
    {
        auto it = generations.find(name);
        return it != generations.end() && it->second == generation;
    }

    /** @brief Returns the number of names that are tracked */
    size_t size() const
    {
        return generations.size();
    }

  private:
    std::unordered_map<std::string, uint64_t> generations;
    uint64_t lastGeneration = 0;
};
