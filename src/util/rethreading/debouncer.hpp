#ifndef DEBOUNCER_H
#define DEBOUNCER_H

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

template<class T>
class DebounceTime
{
  public:
    using Clock = std::chrono::steady_clock;

    DebounceTime(int debounceMs, std::function<void(const T &)> emit) :
        debounceMs_(debounceMs), emit_(std::move(emit))
    {}

    ~DebounceTime()
    {
        stop();
    }

    // Call this whenever a new value arrives
    void next(const T & value)
    {
        ensureWorker();
        {
            std::lock_guard<std::mutex> lock(m_);
            latest_    = value;
            lastEvent_ = Clock::now();
            running_   = true;
        }
        cv_.notify_one();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_one();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

  private:
    void ensureWorker()
    {
        std::lock_guard<std::mutex> lock(m_);
        if (workerStarted_)
        {
            return;
        }

        workerStarted_ = true;

        worker_ = std::thread([this]
        {
            std::unique_lock<std::mutex> lock(m_);
            while (!stop_)
            {
                cv_.wait(lock, [this] { return stop_ || running_; });
                if (stop_)
                {
                    break;
                }

                if (!latest_.has_value())
                {
                    running_ = false;
                    continue;
                }

                auto target = lastEvent_;
                auto debounceDur = std::chrono::milliseconds(debounceMs_);

                cv_.wait_for(lock, debounceDur,
                    [this, target] { return stop_ || lastEvent_ != target; });

                if (stop_)
                {
                    break;
                }

                // No newer value arrived during debounce window => emit latest
                if (latest_.has_value() && (lastEvent_ == target))
                {
                    auto v = *latest_;
                    latest_.reset();
                    running_ = false;
                    lock.unlock();
                    emit_(v);
                    lock.lock();
                } else
                {
                    running_ = true; // more values came in; wait again
                }
            }
        });
    }

    int debounceMs_;
    std::function<void(const T &)> emit_;

    std::mutex m_;
    std::condition_variable cv_;
    std::thread worker_;

    bool stop_ = false;
    bool workerStarted_ = false;
    bool running_ = false;

    std::optional<T> latest_;
    Clock::time_point lastEvent_{};
};

#if 0
std::unique_ptr<DebounceTime<std::string>> d;

int main()
{
    DebounceTime<std::string> deb(300, [] (const std::string& s)
    {
        std::cout << "Debounced emit: " << s << "\n";
    });

    d = std::make_unique<DebounceTime<std::string>>(300, [] (const std::string& s)
    {
        std::cout << "Debounced emit: " << s << "\n";
    });

    d->next("h");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    d->next("he");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    d->next("hel");
    std::this_thread::sleep_for(std::chrono::milliseconds(400)); // quiet for 300ms -> emits "hel"

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

#endif

#endif
