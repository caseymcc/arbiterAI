#ifndef _ARBITERAI_SERVER_DRAINCONTROLLER_H_
#define _ARBITERAI_SERVER_DRAINCONTROLLER_H_

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace arbiterAI
{
namespace server
{

/// Tracks in-flight HTTP requests and other work (scheduler jobs, downloads)
/// so the server can stop accepting requests and wait for the rest to finish.
///
/// An HTTP request is counted from the pre-routing handler (gate()) until
/// httplib has written its response (the logger installed by attach()), so a
/// streamed response stays counted until its last chunk is sent.
class DrainController {
public:
    /// Returns how many units of non-HTTP work are still running.
    using BusyProbe=std::function<size_t()>;

    struct BusyCount {
        std::string name;
        size_t count=0;
    };

    DrainController();

    /// Add a source of work drain() waits for besides HTTP requests.
    void addBusyProbe(const std::string &name, BusyProbe probe);

    /// Install the logger that ends a request's count once its response is written.
    void attach(httplib::Server &server);

    /// Call first in the server's pre-routing handler. While draining it
    /// answers 503 and returns Handled; otherwise it counts the request.
    httplib::Server::HandlerResponse gate(const httplib::Request &req, httplib::Response &res);

    /// End the count of the request handled on this thread, if it was counted.
    void responseWritten();

    /// Refuse new requests from now on.
    void beginDrain();
    bool isDraining() const;

    size_t inFlightRequests() const;

    /// Non-HTTP work still running, per probe.
    std::vector<BusyCount> busyWork() const;

    /// True when no request is in flight and every probe reports zero.
    bool isIdle() const;

    /// beginDrain(), then wait until isIdle() or the timeout passes.
    /// Returns true if everything finished in time.
    bool drain(std::chrono::milliseconds timeout);

    /// Seconds sent in Retry-After with the 503.
    void setRetryAfterSeconds(int seconds);

private:
    std::atomic<bool> m_draining{false};
    std::atomic<int> m_retryAfterSeconds{5};

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    size_t m_inFlight=0;
    std::vector<std::pair<std::string, BusyProbe>> m_probes;
};

} // namespace server
} // namespace arbiterAI

#endif//_ARBITERAI_SERVER_DRAINCONTROLLER_H_
