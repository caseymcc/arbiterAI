#include "autoUpdate.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>

namespace arbiterAI
{
namespace server
{

namespace
{

std::string formatUtc(SystemTime time)
{
    std::time_t seconds=std::chrono::system_clock::to_time_t(time);
    std::tm utc{};
    gmtime_r(&seconds, &utc);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

int localMinuteOfDay(SystemTime time)
{
    std::time_t seconds=std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_r(&seconds, &local);
    return local.tm_hour*60+local.tm_min;
}

bool inScope(const std::string &scope, const SemVer &current, const SemVer &latest)
{
    if(scope == "major")
        return true;
    if(scope == "minor")
        return latest.major == current.major;
    return latest.major == current.major && latest.minor == current.minor;
}

} // anonymous namespace

std::optional<SystemTime> parseUtcTime(const std::string &text)
{
    std::tm utc{};
    const char *end=strptime(text.c_str(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    if(!end || *end != '\0')
        return std::nullopt;
    return std::chrono::system_clock::from_time_t(timegm(&utc));
}

AutoDecision decideAutoUpdate(const AutoInputs &inputs)
{
    AutoDecision decision;
    const UpdatePolicy &policy=inputs.policy;

    if(!policy.autoApply)
    {
        decision.status="off";
        return decision;
    }
    if(!policy.allowApply)
    {
        decision.status="off: updates.allow_apply is false in the config file";
        return decision;
    }
    if(inputs.supervisor == Supervisor::None)
    {
        decision.status="off: no supervisor declared (see README)";
        return decision;
    }
    decision.enabled=true;

    std::optional<SemVer> current=SemVer::parse(inputs.currentVersion, false);
    if(!inputs.latest || !current || !(*current<inputs.latest->version))
    {
        decision.status="up to date";
        return decision;
    }

    const SemVer &latest=inputs.latest->version;
    std::string label="v"+latest.toString();
    if(!inScope(policy.autoApplyScope, *current, latest))
    {
        std::string kind=latest.major != current->major?"major":"minor";
        decision.status=label+" is a "+kind+" update, outside the '"+policy.autoApplyScope
            +"' scope: install it from the dashboard";
        return decision;
    }
    if(std::find(inputs.failed.begin(), inputs.failed.end(), latest.toString()) != inputs.failed.end())
    {
        decision.status="skipping "+label+": it failed here before (install it from the dashboard to try again)";
        return decision;
    }
    if(inputs.applierBusy)
    {
        decision.status="an update is running";
        return decision;
    }
    if(!inputs.check.canApply)
    {
        decision.status="blocked: "+inputs.check.blockedReason;
        return decision;
    }
    if(inputs.retryAfter && inputs.now<*inputs.retryAfter)
    {
        decision.status="the last attempt at "+label+" failed; trying again later";
        decision.nextAttempt=inputs.retryAfter;
        return decision;
    }

    std::optional<SystemTime> published=parseUtcTime(inputs.latest->publishedAt);
    if(!published)
    {
        decision.status="waiting: "+label+" has no publish time";
        return decision;
    }
    SystemTime due=*published+std::chrono::hours(policy.autoApplyDelayHours);
    if(inputs.now<due)
    {
        decision.status="waiting: "+label+" installs "+std::to_string(policy.autoApplyDelayHours)
            +" h after it was published";
        decision.nextAttempt=due;
        return decision;
    }

    int windowStart=0;
    int windowEnd=0;
    if(!policy.autoApplyWindow.empty() && parseTimeWindow(policy.autoApplyWindow, windowStart, windowEnd))
    {
        int minute=inputs.localMinuteOfDay;
        bool inside=windowStart<windowEnd
            ?minute>=windowStart && minute<windowEnd
            :minute>=windowStart || minute<windowEnd;
        if(!inside)
        {
            decision.status="waiting for the window "+policy.autoApplyWindow+" (local time)";
            int untilStart=(windowStart-minute+24*60)%(24*60);
            decision.nextAttempt=inputs.now+std::chrono::minutes(untilStart);
            return decision;
        }
    }

    if(!inputs.busy.empty())
    {
        decision.status="waiting for the server to be idle ("+inputs.busy+" running)";
        return decision;
    }
    SystemTime idleSince=inputs.idleSince.value_or(inputs.now);
    SystemTime idleDue=idleSince+std::chrono::minutes(policy.idleMinutes);
    if(inputs.now<idleDue)
    {
        decision.status="waiting for "+std::to_string(policy.idleMinutes)+" idle minutes";
        decision.nextAttempt=idleDue;
        return decision;
    }

    decision.apply=true;
    decision.status="installing "+label;
    return decision;
}

AutoUpdater::AutoUpdater(Sources sources, UpdateApplier &applier, std::string currentVersion):
    m_sources(std::move(sources)),
    m_applier(applier),
    m_currentVersion(std::move(currentVersion))
{
    if(!m_sources.now)
        m_sources.now=[]() { return std::chrono::system_clock::now(); };
    if(!m_sources.localMinuteOfDay)
        m_sources.localMinuteOfDay=localMinuteOfDay;
    m_last.status="off";
}

AutoUpdater::~AutoUpdater()
{
    stop();
}

void AutoUpdater::setRetryDelay(std::chrono::seconds delay)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_retryDelay=delay;
}

void AutoUpdater::start(std::chrono::milliseconds interval)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_thread.joinable())
        return;
    m_stopping=false;
    m_thread=std::thread(&AutoUpdater::run, this, interval);
}

void AutoUpdater::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping=true;
    }
    m_wake.notify_all();
    if(m_thread.joinable())
        m_thread.join();
}

void AutoUpdater::run(std::chrono::milliseconds interval)
{
    while(true)
    {
        tick();

        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_wake.wait_for(lock, interval, [this]() { return m_stopping; }))
            return;
    }
}

AutoDecision AutoUpdater::tick()
{
    AutoInputs inputs;
    inputs.policy=m_sources.policy();
    inputs.currentVersion=m_currentVersion;
    inputs.latest=m_sources.status().latest;
    inputs.failed=m_sources.failed();
    inputs.check=m_sources.check();
    inputs.supervisor=m_sources.supervisor();
    inputs.applierBusy=m_applier.busy();
    inputs.now=m_sources.now();
    inputs.localMinuteOfDay=m_sources.localMinuteOfDay(inputs.now);
    inputs.busy=m_sources.busy();

    std::lock_guard<std::mutex> lock(m_mutex);
    // Idle is sampled: any work seen at a tick starts the idle time again.
    if(inputs.busy.empty())
    {
        if(!m_idleSince)
            m_idleSince=inputs.now;
    }
    else
    {
        m_idleSince.reset();
    }

    // An attempt that ended without a restart failed; wait before the next.
    if(!m_attempted.empty() && !inputs.applierBusy)
    {
        spdlog::warn("Auto-update to v{} did not complete, trying again in {} min", m_attempted, m_retryDelay.count()/60);
        m_retryAfter=inputs.now+m_retryDelay;
        m_attempted.clear();
    }

    inputs.idleSince=m_idleSince;
    inputs.retryAfter=m_retryAfter;
    AutoDecision decision=decideAutoUpdate(inputs);

    if(decision.apply)
    {
        std::string message;
        std::string version=inputs.latest->version.toString();
        UpdateApplier::Start start=m_applier.apply(version, inputs.latest, inputs.check, message, m_sources.busy);
        if(start == UpdateApplier::Start::Started)
        {
            spdlog::info("Auto-update: installing v{}", version);
            m_attempted=version;
            decision.status="installing v"+version;
        }
        else
        {
            decision.status="could not start v"+version+": "+message;
            m_retryAfter=inputs.now+m_retryDelay;
            decision.nextAttempt=m_retryAfter;
        }
    }

    m_last=decision;
    return decision;
}

nlohmann::json AutoUpdater::statusJson() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return {
        {"enabled", m_last.enabled},
        {"status", m_last.status},
        {"next_attempt", m_last.nextAttempt?nlohmann::json(formatUtc(*m_last.nextAttempt)):nlohmann::json(nullptr)}
    };
}

} // namespace server
} // namespace arbiterAI
