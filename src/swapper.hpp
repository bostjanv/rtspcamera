/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

template<typename T>
class Swapper {
public:
    Swapper(T&& item)
        : push_counter_(std::numeric_limits<uint64_t>::max())
        , pop_counter_(std::numeric_limits<uint64_t>::max())
        , item_(std::forward<T>(item))
        , is_waiting_(false)
    {
    }

    T push(T&& item)
    {
        bool should_signal = false;

        {
            std::scoped_lock lock(mutex_);
            item = std::exchange(item_, std::forward<T>(item));
            push_counter_ += 1;
            should_signal = is_waiting_;
        }

        if (should_signal) {
            condvar_.notify_one();
        }

        return std::move(item);
    }

    std::pair<T, uint64_t> pop(T&& item)
    {
        std::unique_lock lock(mutex_);
        is_waiting_ = true;
        condvar_.wait(lock, [this]() { return pop_counter_ != push_counter_; });
        item = std::exchange(item_, std::forward<T>(item));
        pop_counter_ = push_counter_;
        is_waiting_ = false;
        return { std::forward<T>(item), pop_counter_ };
    }

    std::optional<std::pair<T, uint64_t>> try_pop(T&& item, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        is_waiting_ = true;
        if (condvar_.wait_for(lock, timeout,
                [this] { return pop_counter_ != push_counter_; })) {
            item = std::exchange(item_, std::forward<T>(item));
            pop_counter_ = push_counter_;
            is_waiting_ = false;
            return std::make_pair(std::forward<T>(item), pop_counter_);
        }

        is_waiting_ = false;
        return {};
    }

private:
    uint64_t push_counter_;
    uint64_t pop_counter_;
    T item_;
    std::mutex mutex_;
    std::condition_variable condvar_;
    bool is_waiting_;
};
