/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace rtspcam {

template<typename T>
class Queue {
public:
    // Pushes a new item to the end of the queue.
    // Returns `true` if the queue was empty, returns `false` otherwise.
    bool push(T&& item)
    {
        bool was_empty;

        {
            std::scoped_lock lock(mutex_);
            was_empty = queue_.empty();
            queue_.push_back(std::forward<T>(item));
        }

        if (was_empty) {
            condvar_.notify_one();
        }

        return was_empty;
    }

    // Pops an element from the front of the queue.
    // Blocks if the queue is empty.
    T pop()
    {
        std::unique_lock lock(mutex_);
        condvar_.wait(lock, [this]() { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    // Tries to pop an element from the front of the queue with a timeout.
    std::optional<T> try_pop(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (condvar_.wait_for(lock, timeout,
                [this] { return !queue_.empty(); })) {
            T item = std::move(queue_.front());
            queue_.pop_front();
            return item;
        }
        return {};
    }

    // Returns all items from the queue. The order of items is preserved (fifo).
    // The queue is left empty on return.
    std::vector<T> drain()
    {
        std::vector<T> items;

        std::scoped_lock lock(mutex_);
        items.reserve(queue_.size());
        std::move(queue_.begin(), queue_.end(), std::back_inserter(items));
        queue_.clear();

        return items;
    }

    // Returns current size of the queue.
    size_t size() const
    {
        std::scoped_lock lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condvar_;
    std::deque<T> queue_;
};

} // namespace rtspcam
