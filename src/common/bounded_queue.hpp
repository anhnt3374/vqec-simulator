#pragma once

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace vqec {

// What push() does when the queue is full.
enum class OverflowPolicy {
    Block,       // Wait for space. Use for finite inputs (files) where every frame matters.
    DropOldest,  // Discard the oldest item. Use for live inputs where latency matters.
    DropNewest,  // Discard the incoming item.
};

// The only sanctioned channel between threads. Thread-safe; any number of producers/consumers.
template <typename T>
class BoundedQueue {
public:
    BoundedQueue(std::size_t capacity, OverflowPolicy policy)
        : capacity_(capacity), policy_(policy) {
        assert(capacity_ > 0);
    }

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;
    ~BoundedQueue() = default;

    // Returns true when `item` was enqueued. DropOldest always enqueues (evicting the oldest
    // item); DropNewest returns false instead of enqueueing. Always false after close().
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (policy_ == OverflowPolicy::Block) {
            not_full_.wait(lock, [this] { return closed_ || items_.size() < capacity_; });
        }
        if (closed_) {
            return false;
        }
        if (items_.size() >= capacity_) {
            ++dropped_count_;
            if (policy_ == OverflowPolicy::DropNewest) {
                return false;
            }
            items_.pop_front();
        }
        items_.push_back(std::move(item));
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    // Blocks until an item is available. Returns nullopt once the queue is closed and drained.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_.wait(lock, [this] { return closed_ || !items_.empty(); });
        return take_front(lock);
    }

    // Non-blocking variant of pop().
    std::optional<T> try_pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        return take_front(lock);
    }

    // Wakes all waiters. Pending items can still be popped; further pushes are rejected.
    void close() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    std::uint64_t dropped_count() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return dropped_count_;
    }

private:
    std::optional<T> take_front(std::unique_lock<std::mutex>& lock) {
        if (items_.empty()) {
            return std::nullopt;
        }
        std::optional<T> item(std::move(items_.front()));
        items_.pop_front();
        lock.unlock();
        not_full_.notify_one();
        return item;
    }

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::deque<T> items_;
    const std::size_t capacity_;
    const OverflowPolicy policy_;
    bool closed_ = false;
    std::uint64_t dropped_count_ = 0;
};

}  // namespace vqec
