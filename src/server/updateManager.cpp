#include "updateManager.h"

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

} // namespace server
} // namespace arbiterAI
