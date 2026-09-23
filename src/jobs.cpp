#include "sengine/jobs.hpp"
#include <stdexcept>
namespace sengine {
namespace {
thread_local const job_system* current_pool{};
}
job_system::job_system(unsigned workers) {
    workers = std::max(1u, workers);
    workers_.reserve(workers);
    try {
        for (unsigned i = 0; i < workers; ++i)
            workers_.emplace_back(&job_system::work, this);
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            closing_ = true;
        }
        ready_.notify_all();
        workers_.clear();
        throw;
    }
}
job_system::~job_system() {
    {
        std::lock_guard lock(mutex_);
        closing_ = true;
    }
    ready_.notify_all();
    workers_.clear();
}
bool job_system::executing_here() const noexcept {
    return current_pool == this;
}
void job_system::enqueue(unique_function<void()> task) {
    {
        std::lock_guard lock(mutex_);
        if (closing_)
            throw std::logic_error("Job system is shutting down");
        queue_.push_back(std::move(task));
    }
    ready_.notify_one();
}
void job_system::work() {
    current_pool = this;
    for (;;) {
        unique_function<void()> task;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return closing_ || !queue_.empty(); });
            if (queue_.empty())
                break;
            task = std::move(queue_.front());
            queue_.pop_front();
            ++active_;
        }
        task();
        {
            std::lock_guard lock(mutex_);
            --active_;
            if (!active_ && queue_.empty())
                idle_.notify_all();
        }
    }
    current_pool = nullptr;
}
void job_system::wait_idle() {
    if (executing_here())
        throw std::logic_error("A worker cannot wait for its own pool");
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return !active_ && queue_.empty(); });
}
}
