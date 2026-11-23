#include "drainController.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace arbiterAI
{
namespace server
{

namespace
{

// httplib reads, routes and writes a request on one worker thread, so the
// pre-routing handler and the logger of the same request run on the same thread.
thread_local DrainController *t_countedBy=nullptr;

} // anonymous namespace

DrainController::DrainController()
{
}

void DrainController::addBusyProbe(const std::string &name, BusyProbe probe)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_probes.emplace_back(name, std::move(probe));
}

void DrainController::attach(httplib::Server &server)
{
    server.set_logger([this](const httplib::Request &, const httplib::Response &)
    {
        responseWritten();
    });
}

httplib::Server::HandlerResponse DrainController::gate(const httplib::Request &, httplib::Response &res)
{
    // A count left on this thread by a request whose response was never
    // written would otherwise hold the drain until its timeout.
    if(t_countedBy==this)
        responseWritten();

    if(m_draining.load())
    {
        nlohmann::json err={{"error", {
            {"message", "Server is shutting down"},
            {"type", "server_error"},
            {"param", nullptr},
            {"code", "server_shutting_down"}
        }}};
        res.status=503;
        res.set_header("Retry-After", std::to_string(m_retryAfterSeconds.load()));
        res.set_header("Connection", "close");
        res.set_content(err.dump(), "application/json");
        return httplib::Server::HandlerResponse::Handled;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_inFlight;
    }
    t_countedBy=this;
    return httplib::Server::HandlerResponse::Unhandled;
}

void DrainController::responseWritten()
{
    if(t_countedBy!=this)
        return;
    t_countedBy=nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if(m_inFlight>0)
            --m_inFlight;
    }
    m_cv.notify_all();
}

void DrainController::beginDrain()
{
    m_draining.store(true);
    m_cv.notify_all();
}

bool DrainController::isDraining() const
{
    return m_draining.load();
}

size_t DrainController::inFlightRequests() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_inFlight;
}

std::vector<DrainController::BusyCount> DrainController::busyWork() const
{
    std::vector<std::pair<std::string, BusyProbe>> probes;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        probes=m_probes;
    }

    std::vector<BusyCount> result;
    for(const std::pair<std::string, BusyProbe> &probe:probes)
    {
        result.push_back({probe.first, probe.second()});
    }
    return result;
}

bool DrainController::isIdle() const
{
    if(inFlightRequests()>0)
        return false;

    for(const BusyCount &busy:busyWork())
    {
        if(busy.count>0)
            return false;
    }
    return true;
}

bool DrainController::drain(std::chrono::milliseconds timeout)
{
    beginDrain();

    std::chrono::steady_clock::time_point deadline=std::chrono::steady_clock::now()+timeout;

    // Probes have no notification, so poll them; a finished request wakes the wait early.
    const std::chrono::milliseconds pollInterval(100);

    while(!isIdle())
    {
        std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now();
        if(now>=deadline)
        {
            std::string left="requests="+std::to_string(inFlightRequests());
            for(const BusyCount &busy:busyWork())
            {
                left+=", "+busy.name+"="+std::to_string(busy.count);
            }
            spdlog::warn("Drain timed out after {} ms with work left: {}", timeout.count(), left);
            return false;
        }

        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::min<std::chrono::steady_clock::duration>(pollInterval, deadline-now));
    }
    return true;
}

void DrainController::setRetryAfterSeconds(int seconds)
{
    m_retryAfterSeconds.store(seconds);
}

} // namespace server
} // namespace arbiterAI
