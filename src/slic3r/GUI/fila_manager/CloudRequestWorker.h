#ifndef slic3r_CloudRequestWorker_h_
#define slic3r_CloudRequestWorker_h_

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Slic3r { namespace GUI {

// One sleeping worker for filament requests, rather than one detached thread
// per request. Cancellation drains the active plugin call before agent reuse.
class CloudRequestWorker {
public:
    CloudRequestWorker() = default;
    CloudRequestWorker(const CloudRequestWorker&) = delete;
    CloudRequestWorker& operator=(const CloudRequestWorker&) = delete;
    ~CloudRequestWorker() { shutdown(); }

    void submit(std::function<void()> task)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) return;
        if (!m_thread.joinable()) m_thread = std::thread([this] { run(); });
        m_queue.push_back(std::move(task));
        m_ready.notify_one();
    }

    void cancel_pending_and_wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_idle.wait(lock, [this] { return !m_active; });
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
            m_queue.clear();
        }
        m_ready.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            m_ready.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_stopping) return;
            auto task = std::move(m_queue.front());
            m_queue.pop_front();
            m_active = true;
            lock.unlock();
            // The client translates request failures into UI callbacks.
            try { task(); } catch (...) {}
            lock.lock();
            m_active = false;
            m_idle.notify_all();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_ready, m_idle;
    std::deque<std::function<void()>> m_queue;
    std::thread m_thread;
    bool m_active = false;
    bool m_stopping = false;
};

}} // namespace Slic3r::GUI
#endif
