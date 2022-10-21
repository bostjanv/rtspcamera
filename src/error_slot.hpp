/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <atomic>
#include <optional>
#include <string>

class ErrorSlot {
public:
    ErrorSlot()
        : errored_(false)
    {
    }

    void set(std::string const& error)
    {
        error_ = error;
        errored_.store(true, std::memory_order_release);
    }

    std::optional<std::string> check()
    {
        if (errored_.load(std::memory_order_acquire)) {
            return { error_ };
        }
        return {};
    }

private:
    std::atomic<bool> errored_;
    std::string error_;
};
