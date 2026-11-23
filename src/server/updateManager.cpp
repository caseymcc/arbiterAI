#include "updateManager.h"

#include <cpr/cpr.h>
#include <spdlog/spdlog.h>

#include <ctime>

namespace arbiterAI
{
namespace server
{

namespace
{

bool parseNumber(const std::string &text, size_t &pos, int &value)
{
    size_t start=pos;
    while(pos<text.size() && text[pos]>='0' && text[pos]<='9')
        pos++;

    size_t length=pos-start;
    if(length == 0 || length>9)
        return false;
    if(length>1 && text[start] == '0')
        return false;

    value=std::stoi(text.substr(start, length));
    return true;
}

std::string stringField(const nlohmann::json &object, const char *key)
{
    nlohmann::json::const_iterator it=object.find(key);
    if(it == object.end() || !it->is_string())
        return std::string();
    return it->get<std::string>();
}

bool boolField(const nlohmann::json &object, const char *key)
{
    nlohmann::json::const_iterator it=object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

std::optional<ReleaseAsset> findServerAsset(const nlohmann::json &release, const std::string &tag)
{
    nlohmann::json::const_iterator assets=release.find("assets");
    if(assets == release.end() || !assets->is_array())
        return std::nullopt;

    std::string wanted=serverAssetName(tag);
    for(const nlohmann::json &asset:*assets)
    {
        if(!asset.is_object() || stringField(asset, "name") != wanted)
            continue;

        ReleaseAsset result;
        result.name=wanted;
        result.url=stringField(asset, "browser_download_url");
        result.digest=stringField(asset, "digest");
        nlohmann::json::const_iterator size=asset.find("size");
        if(size != asset.end() && size->is_number_integer())
            result.size=size->get<int64_t>();
        return result;
    }
    return std::nullopt;
}

std::string formatTime(std::chrono::system_clock::time_point time)
{
    std::time_t seconds=std::chrono::system_clock::to_time_t(time);
    std::tm utc{};
    gmtime_r(&seconds, &utc);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace

std::optional<SemVer> SemVer::parse(const std::string &text, bool requireV)
{
    size_t pos=0;
    if(requireV)
    {
        if(text.empty() || text[0] != 'v')
            return std::nullopt;
        pos=1;
    }

    SemVer version;
    if(!parseNumber(text, pos, version.major) || pos>=text.size() || text[pos++] != '.')
        return std::nullopt;
    if(!parseNumber(text, pos, version.minor) || pos>=text.size() || text[pos++] != '.')
        return std::nullopt;
    if(!parseNumber(text, pos, version.patch) || pos != text.size())
        return std::nullopt;
    return version;
}

std::string SemVer::toString() const
{
    return std::to_string(major)+"."+std::to_string(minor)+"."+std::to_string(patch);
}

bool SemVer::operator<(const SemVer &other) const
{
    if(major != other.major)
        return major<other.major;
    if(minor != other.minor)
        return minor<other.minor;
    return patch<other.patch;
}

bool SemVer::operator==(const SemVer &other) const
{
    return major == other.major && minor == other.minor && patch == other.patch;
}

std::string serverAssetName(const std::string &tag)
{
    return "arbiterAI-server-"+tag+"-linux-x64.tar.gz";
}

std::optional<ReleaseInfo> selectLatestRelease(const nlohmann::json &releases, std::string &error)
{
    if(!releases.is_array())
    {
        error="release list is not a JSON array";
        return std::nullopt;
    }

    std::optional<ReleaseInfo> latest;
    for(const nlohmann::json &release:releases)
    {
        if(!release.is_object() || boolField(release, "draft") || boolField(release, "prerelease"))
            continue;

        std::string tag=stringField(release, "tag_name");
        std::optional<SemVer> version=SemVer::parse(tag, true);
        if(!version)
            continue;
        if(latest && !(latest->version<*version))
            continue;

        ReleaseInfo info;
        info.tag=tag;
        info.version=*version;
        info.htmlUrl=stringField(release, "html_url");
        info.notes=stringField(release, "body");
        info.publishedAt=stringField(release, "published_at");
        info.serverAsset=findServerAsset(release, tag);
        latest=std::move(info);
    }
    return latest;
}

UpdateChecker::UpdateChecker(std::unique_ptr<ReleaseSource> source, std::string currentVersion):
    m_source(std::move(source)),
    m_currentVersion(SemVer::parse(currentVersion, false))
{
    m_status.current=std::move(currentVersion);
}

UpdateCheckResult UpdateChecker::check()
{
    std::lock_guard<std::mutex> checkLock(m_checkMutex);

    ReleaseFetch fetch=m_source->fetch(m_etag);
    if(!fetch.error.empty())
        return fail(fetch.error);

    if(fetch.status == 304)
    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_status.checkedAt=std::chrono::system_clock::now();
        m_status.lastError.clear();
        return UpdateCheckResult::NotModified;
    }
    if(fetch.status != 200)
        return fail("GitHub answered HTTP "+std::to_string(fetch.status));

    nlohmann::json releases=nlohmann::json::parse(fetch.body, nullptr, false);
    if(releases.is_discarded())
        return fail("release list is not valid JSON");

    std::string error;
    std::optional<ReleaseInfo> latest=selectLatestRelease(releases, error);
    if(!error.empty())
        return fail(error);

    m_etag=fetch.etag;

    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_status.latest=std::move(latest);
    m_status.updateAvailable=m_status.latest && m_currentVersion && *m_currentVersion<m_status.latest->version;
    m_status.checkedAt=std::chrono::system_clock::now();
    m_status.lastError.clear();
    return UpdateCheckResult::Updated;
}

UpdateCheckResult UpdateChecker::fail(const std::string &error)
{
    spdlog::warn("Update check failed: {}", error);

    std::lock_guard<std::mutex> lock(m_statusMutex);
    m_status.lastError=error;
    return UpdateCheckResult::Failed;
}

UpdateStatus UpdateChecker::status() const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_status;
}

nlohmann::json UpdateChecker::statusJson() const
{
    UpdateStatus current=status();

    nlohmann::json result;
    result["current"]=current.current;
    result["update_available"]=current.updateAvailable;
    if(current.latest)
    {
        result["latest"]=current.latest->version.toString();
        result["release_url"]=current.latest->htmlUrl;
        result["release_notes"]=current.latest->notes;
        result["published_at"]=current.latest->publishedAt;
    }
    else
    {
        result["latest"]=nullptr;
        result["release_url"]=nullptr;
        result["release_notes"]=nullptr;
        result["published_at"]=nullptr;
    }
    result["checked_at"]=current.checkedAt?nlohmann::json(formatTime(*current.checkedAt)):nlohmann::json(nullptr);
    result["last_error"]=current.lastError.empty()?nlohmann::json(nullptr):nlohmann::json(current.lastError);
    return result;
}

GitHubReleaseSource::GitHubReleaseSource(std::string repo, std::string apiBase):
    m_url(apiBase+"/repos/"+repo+"/releases?per_page=100")
{
}

ReleaseFetch GitHubReleaseSource::fetch(const std::string &etag)
{
    cpr::Header headers{
        {"Accept", "application/vnd.github+json"},
        {"X-GitHub-Api-Version", "2022-11-28"},
        {"User-Agent", "arbiterAI-server"}
    };
    if(!etag.empty())
        headers["If-None-Match"]=etag;

    cpr::Response response=cpr::Get(cpr::Url{m_url}, headers, cpr::ConnectTimeout{5000}, cpr::Timeout{15000});

    ReleaseFetch result;
    if(response.error.code != cpr::ErrorCode::OK)
    {
        result.error=response.error.message.empty()?"request failed":response.error.message;
        return result;
    }

    result.status=static_cast<int>(response.status_code);
    result.body=std::move(response.text);
    cpr::Header::const_iterator etagHeader=response.header.find("ETag");
    if(etagHeader != response.header.end())
        result.etag=etagHeader->second;
    return result;
}

bool readUpdatePolicy(const nlohmann::json &updates, UpdatePolicy &policy, std::string &error)
{
    if(!updates.is_object())
    {
        error="'updates' must be an object";
        return false;
    }

    UpdatePolicy result=policy;
    nlohmann::json::const_iterator check=updates.find("check");
    if(check != updates.end())
    {
        if(!check->is_boolean())
        {
            error="'updates.check' must be a boolean";
            return false;
        }
        result.check=check->get<bool>();
    }

    nlohmann::json::const_iterator interval=updates.find("interval_hours");
    if(interval != updates.end())
    {
        if(!interval->is_number_integer())
        {
            error="'updates.interval_hours' must be an integer";
            return false;
        }
        int64_t hours=interval->get<int64_t>();
        if(hours<1 || hours>8760)
        {
            error="'updates.interval_hours' must be between 1 and 8760";
            return false;
        }
        result.intervalHours=static_cast<int>(hours);
    }

    nlohmann::json::const_iterator allowApply=updates.find("allow_apply");
    if(allowApply != updates.end())
    {
        if(!allowApply->is_boolean())
        {
            error="'updates.allow_apply' must be a boolean";
            return false;
        }
        result.allowApply=allowApply->get<bool>();
    }

    policy=result;
    return true;
}

bool mergeUpdatesConfig(nlohmann::json &cfg, const nlohmann::json &body, UpdatePolicy &policy, std::string &error)
{
    if(body.is_object() && body.contains("allow_apply"))
    {
        error="'updates.allow_apply' can only be set in the config file";
        return false;
    }

    UpdatePolicy requested;
    if(!readUpdatePolicy(body, requested, error))
        return false;

    nlohmann::json updates=cfg.value("updates", nlohmann::json::object());
    if(!updates.is_object())
        updates=nlohmann::json::object();
    if(body.contains("check"))
        updates["check"]=requested.check;
    if(body.contains("interval_hours"))
        updates["interval_hours"]=requested.intervalHours;

    UpdatePolicy merged;
    if(!readUpdatePolicy(updates, merged, error))
        return false;

    cfg["updates"]=updates;
    policy=merged;
    return true;
}

nlohmann::json updatePolicyJson(const UpdatePolicy &policy)
{
    return {
        {"check", policy.check},
        {"interval_hours", policy.intervalHours},
        {"allow_apply", policy.allowApply}
    };
}

UpdateManager::UpdateManager(std::unique_ptr<ReleaseSource> source, std::string currentVersion):
    m_checker(std::move(source), std::move(currentVersion))
{
}

UpdateManager::~UpdateManager()
{
    stop();
}

void UpdateManager::setIntervalUnit(std::chrono::milliseconds unit)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_intervalUnit=unit;
}

void UpdateManager::start(const UpdatePolicy &policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_thread.joinable())
        return;

    m_policy=policy;
    m_stopping=false;
    m_thread=std::thread(&UpdateManager::run, this);
}

void UpdateManager::setPolicy(const UpdatePolicy &policy)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_policy=policy;
        m_policyChanged=true;
    }
    m_wake.notify_all();
}

UpdatePolicy UpdateManager::policy() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_policy;
}

nlohmann::json UpdateManager::checkNow()
{
    m_checker.check();
    return m_checker.statusJson();
}

nlohmann::json UpdateManager::statusJson() const
{
    return m_checker.statusJson();
}

UpdateStatus UpdateManager::status() const
{
    return m_checker.status();
}

void UpdateManager::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping=true;
    }
    m_wake.notify_all();

    if(m_thread.joinable())
        m_thread.join();
}

void UpdateManager::run()
{
    std::optional<std::chrono::steady_clock::time_point> lastAttempt;

    std::unique_lock<std::mutex> lock(m_mutex);
    while(!m_stopping)
    {
        m_policyChanged=false;
        if(!m_policy.check)
        {
            m_wake.wait(lock, [this]() { return m_stopping || m_policyChanged; });
            continue;
        }

        std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now();
        std::chrono::milliseconds interval=m_intervalUnit*m_policy.intervalHours;
        if(lastAttempt && now<*lastAttempt+interval)
        {
            m_wake.wait_until(lock, *lastAttempt+interval, [this]() { return m_stopping || m_policyChanged; });
            continue;
        }

        lastAttempt=now;
        lock.unlock();
        m_checker.check();
        lock.lock();
    }
}

} // namespace server
} // namespace arbiterAI
