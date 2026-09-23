#pragma once
#include "function.hpp"
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace sengine {
class job_system {
  public:
    explicit job_system(unsigned workers = std::thread::hardware_concurrency());
    ~job_system();
    job_system(const job_system&) = delete;
    job_system& operator=(const job_system&) = delete;
    template <class function> auto submit(function&& work) -> std::future<std::invoke_result_t<function>> {
        std::packaged_task<std::invoke_result_t<function>()> task(std::forward<function>(work));
        auto result = task.get_future();
        enqueue(std::move(task));
        return result;
    }
    template <class function> void parallel_for(std::size_t count, std::size_t grain, function&& visit) {
        if (!grain)
            throw std::invalid_argument("Job grain must be positive");
        if (executing_here() || count <= grain) {
            for (std::size_t i = 0; i < count; ++i)
                std::invoke(visit, i);
            return;
        }
        std::vector<std::future<void>> batches;
        batches.reserve(1 + (count - 1) / grain);
        std::exception_ptr failure;
        try {
            for (std::size_t begin = 0; begin < count;) {
                const auto end = begin + std::min(grain, count - begin);
                batches.push_back(submit([&visit, begin, end] {
                    for (auto i = begin; i < end; ++i)
                        std::invoke(visit, i);
                }));
                begin = end;
            }
        } catch (...) {
            failure = std::current_exception();
        }
        for (auto& batch : batches) {
            try {
                batch.get();
            } catch (...) {
                if (!failure)
                    failure = std::current_exception();
            }
        }
        if (failure)
            std::rethrow_exception(failure);
    }
    void wait_idle();
    std::size_t worker_count() const noexcept { return workers_.size(); }

  private:
    void enqueue(unique_function<void()>);
    void work();
    bool executing_here() const noexcept;

  private:
    std::mutex mutex_;
    std::condition_variable ready_, idle_;
    std::deque<unique_function<void()>> queue_;
    std::size_t active_{};
    bool closing_{};
    std::vector<std::jthread> workers_;
};
}
