// Copyright (C) 2018-2019 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <dhcpsrv/dhcpsrv_log.h>

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <queue>
#include <thread>

namespace isc {
namespace dhcp {

/// @brief Defines a thread pool which uses a thread pool queue for managing
/// work items. Each work item is a 'function' object.
template <typename WorkItem>
struct ThreadPool {
    /// @brief Constructor
    ThreadPool() : running_(false) {
    }

    /// @brief Destructor
    ~ThreadPool() {
        reset();
    }

    /// @brief reset the thread pool stopping threads and clearing the internal
    /// queue
    void reset() {
        LOG_INFO(dhcpsrv_logger, "Thread pool shutting down");

        stop();
        queue_.clear();

        LOG_INFO(dhcpsrv_logger, "Thread pool shut down");
    }

    /// @brief start all the threads
    ///
    /// @param thread_count specifies the number of threads to be created and
    /// started
    void start(uint32_t thread_count) {
        LOG_INFO(dhcpsrv_logger, "Thread pool starting with %1 worker threads")
            .arg(thread_count);
        if (!thread_count || running_) {
            return;
        }
        queue_.enable();
        running_ = true;
        for (uint32_t i = 0; i < thread_count; ++i) {
            threads_.push_back(std::make_shared<std::thread>(&ThreadPool::run, this));
        }

        LOG_INFO(dhcpsrv_logger, "Thread pool started");
    }

    /// @brief stop all the threads
    void stop() {
        LOG_INFO(dhcpsrv_logger, "Thread pool stopping");
        running_ = false;
        queue_.disable();
        for (auto thread : threads_) {
            thread->join();
        }
        threads_.clear();

        LOG_INFO(dhcpsrv_logger, "Thread pool stopped");
    }

    /// @brief add a working item to the thread pool
    ///
    /// @param call_back the 'function' object to be added to the queue
    void add(WorkItem& item) {
        queue_.push(item);
    }

    /// @brief count number of work items in the queue
    ///
    /// @return the number of work items in the queue
    size_t count() {
        return queue_.count();
    }

    /// @brief size number of thread pool threads
    ///
    /// @return the number of threads
    size_t size() {
        return threads_.size();
    }
private:
    /// @brief Defines a generic thread pool queue.
    ///
    /// The main purpose is to safely manage thread pool tasks.
    /// The thread pool queue can be 'disabled', which means that no items can be
    /// removed from the queue, or 'enabled', which guarantees that inserting or
    /// removing items are thread safe.
    /// In 'disabled' state, all threads waiting on the queue are unlocked and all
    /// operations are non blocking.
    template <typename Item>
    struct ThreadPoolQueue {
        /// @brief Constructor
        ///
        /// Creates the thread pool queue in 'disabled' state
        ThreadPoolQueue() : enabled_(false) {
        }

        /// @brief Destructor
        ///
        /// Destroys the thread pool queue
        ~ThreadPoolQueue() {
            disable();
            clear();
        }

        /// @brief push work item to the queue
        ///
        /// Used to add work items in the queue.
        /// This function adds an item to the queue and wakes up at least one thread
        /// waiting on the queue.
        ///
        /// @param item the new item to be added to the queue
        void push(Item& item) {
            if (!item) {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(item);
            // Notify pop function so that it can effectively remove a work item.
            cv_.notify_all();
        }

        /// @brief pop work item from the queue or block waiting
        ///
        /// Used to retrieve and remove a work item from the queue
        /// If the queue is 'disabled', this function returns immediately
        /// (no element).
        /// If the queue is 'enabled', this function returns the first element in
        /// the queue or blocks the calling thread if there are no work items
        /// available.
        ///
        /// @param item the reference of the item removed from the queue, if any
        ///
        /// @return true if there was a work item removed from the queue, false
        /// otherwise
        Item pop() {
            std::unique_lock<std::mutex> lock(mutex_);
            while (enabled_) {
                if (queue_.empty()) {
                    // Wait for push or stop functions.
                    cv_.wait(lock);
                    continue;
                }

                Item item = queue_.front();
                queue_.pop();
                return item;
            }

            return Item();
        }

        /// @brief count number of work items in the queue
        ///
        /// Returns the number of work items in the queue
        ///
        /// @return the number of work items
        size_t count() {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        /// @brief clear remove all work items
        ///
        /// Removes all queued work items
        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_ = std::queue<Item>();
        }

        /// @brief start and enable the queue
        ///
        /// Sets the queue state to 'enabled'
        void enable() {
            std::lock_guard<std::mutex> lock(mutex_);
            enabled_ = true;
        }

        /// brief stop and disable the queue
        ///
        /// Sets the queue state to 'disabled' and optionally removes all work items
        void disable() {
            std::lock_guard<std::mutex> lock(mutex_);
            enabled_ = false;
            // Notify pop so that it can exit.
            cv_.notify_all();
        }

    private:
        /// @brief underlying queue container
        std::queue<Item> queue_;

        /// @brief mutex used for critical sections
        std::mutex mutex_;

        /// @brief condition variable used to signal waiting threads
        std::condition_variable cv_;

        /// @brief the sate of the queue
        /// The 'enabled' state corresponds to false value
        /// The 'disabled' state corresponds to true value
        std::atomic_bool enabled_;
    };

    /// @brief run function of each thread
    void run() {
        std::thread::id th_id = std::this_thread::get_id();
        LOG_INFO(dhcpsrv_logger, "Thread pool thread started. id: %1")
            .arg(th_id);

        while (running_) {
            WorkItem item = queue_.pop();
            if (item) {
                item();
            }
        }

        LOG_INFO(dhcpsrv_logger, "Thread pool thread ended. id: %1")
            .arg(th_id);
    }

    /// @brief list of worker threads
    std::list<std::shared_ptr<std::thread>> threads_;

    /// @brief underlying work items queue
    ThreadPoolQueue<WorkItem> queue_;

    /// @brief state of the thread pool
    /// The 'run' state corresponds to false value
    /// The 'stop' state corresponds to true value
    std::atomic_bool running_;
};

}  // namespace dhcp
}  // namespace isc

#endif  // THREAD_POOL_H
