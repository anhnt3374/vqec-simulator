#include "common/bounded_queue.hpp"

#include <optional>
#include <thread>

#include <gtest/gtest.h>

namespace vqec {
namespace {

TEST(BoundedQueue, PopsItemsInFifoOrder) {
    BoundedQueue<int> queue(3, OverflowPolicy::Block);
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));

    EXPECT_EQ(queue.pop(), std::optional<int>(1));
    EXPECT_EQ(queue.pop(), std::optional<int>(2));
}

TEST(BoundedQueue, DropOldestKeepsNewestItemsWhenFull) {
    BoundedQueue<int> queue(2, OverflowPolicy::DropOldest);
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));

    EXPECT_EQ(queue.dropped_count(), 1U);
    EXPECT_EQ(queue.try_pop(), std::optional<int>(2));
    EXPECT_EQ(queue.try_pop(), std::optional<int>(3));
}

TEST(BoundedQueue, DropNewestRejectsIncomingItemWhenFull) {
    BoundedQueue<int> queue(1, OverflowPolicy::DropNewest);
    EXPECT_TRUE(queue.push(1));
    EXPECT_FALSE(queue.push(2));

    EXPECT_EQ(queue.dropped_count(), 1U);
    EXPECT_EQ(queue.try_pop(), std::optional<int>(1));
}

TEST(BoundedQueue, DrainsPendingItemsAfterCloseThenReturnsNullopt) {
    BoundedQueue<int> queue(2, OverflowPolicy::Block);
    EXPECT_TRUE(queue.push(1));
    queue.close();

    EXPECT_FALSE(queue.push(2));
    EXPECT_EQ(queue.pop(), std::optional<int>(1));
    EXPECT_EQ(queue.pop(), std::nullopt);
}

TEST(BoundedQueue, CloseWakesBlockedConsumer) {
    BoundedQueue<int> queue(1, OverflowPolicy::Block);
    std::optional<int> received = 0;
    std::thread consumer([&] { received = queue.pop(); });

    queue.close();
    consumer.join();

    EXPECT_EQ(received, std::nullopt);
}

TEST(BoundedQueue, BlockedProducerResumesWhenConsumerMakesSpace) {
    BoundedQueue<int> queue(1, OverflowPolicy::Block);
    EXPECT_TRUE(queue.push(1));
    bool pushed = false;
    std::thread producer([&] { pushed = queue.push(2); });

    EXPECT_EQ(queue.pop(), std::optional<int>(1));
    producer.join();

    EXPECT_TRUE(pushed);
    EXPECT_EQ(queue.try_pop(), std::optional<int>(2));
}

}  // namespace
}  // namespace vqec
