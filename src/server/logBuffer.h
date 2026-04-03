#ifndef _ARBITERAI_SERVER_LOGBUFFER_H_
#define _ARBITERAI_SERVER_LOGBUFFER_H_

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>

#include <mutex>
#include <deque>
#include <string>
#include <chrono>

namespace arbiterAI
{
namespace server
{

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string level;
    std::string message;
};

/// Thread-safe ring-buffer spdlog sink that retains the most recent N log
/// entries in memory so the dashboard / REST API can serve them.
class LogBufferSink : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit LogBufferSink(size_t maxEntries=500)
        :m_maxEntries(maxEntries) {}

    /// Return a snapshot of the buffered entries (newest last).
    std::deque<LogEntry> getEntries() const
    {
        std::lock_guard<std::mutex> lock(m_readMutex);
        return m_entries;
    }

    /// Return only the last `count` entries.
    std::deque<LogEntry> getEntries(size_t count) const
    {
        std::lock_guard<std::mutex> lock(m_readMutex);
        if(count>=m_entries.size()) return m_entries;

        return std::deque<LogEntry>(m_entries.end()-static_cast<std::ptrdiff_t>(count), m_entries.end());
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_readMutex);
        m_entries.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        LogEntry entry;
        entry.timestamp=msg.time;
        entry.level=std::string(spdlog::level::to_string_view(msg.level).data(),
            spdlog::level::to_string_view(msg.level).size());
        entry.message=std::string(msg.payload.data(), msg.payload.size());

        std::lock_guard<std::mutex> lock(m_readMutex);
        m_entries.push_back(std::move(entry));
        while(m_entries.size()>m_maxEntries)
        {
            m_entries.pop_front();
        }
    }

    void flush_() override {}

private:
    size_t m_maxEntries;
    mutable std::mutex m_readMutex;
    std::deque<LogEntry> m_entries;
};

/// Global accessor so routes.cpp can read the buffer without coupling to main.
inline std::shared_ptr<LogBufferSink> &logBufferSinkInstance()
{
    static std::shared_ptr<LogBufferSink> instance;
    return instance;
}

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_LOGBUFFER_H_
