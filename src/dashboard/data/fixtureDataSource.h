#ifndef _ARBITERAI_DASHBOARD_FIXTUREDATASOURCE_H_
#define _ARBITERAI_DASHBOARD_FIXTUREDATASOURCE_H_

#include "data/dataSource.h"

#include <map>
#include <string>
#include <vector>

namespace arbiterAI
{
namespace dashboard
{

/// Canned replies for the tests: a path answers with the JSON or the failure
/// given for it (a path with no reply is a 404), before the call returns. Every
/// request is recorded.
class FixtureDataSource: public DataSource {
public:
    struct Request {
        std::string method;
        std::string path;
        nlohmann::json body;
    };

    /// \p method "GET", "POST", "PUT" or "DELETE"
    void setJson(const std::string &path, const nlohmann::json &json, const std::string &method="GET");
    /// a reply of \p status (0: unreachable) with {"error": message}
    void setFailure(const std::string &path, int status, const std::string &message, const std::string &method="GET");
    void clear(const std::string &path, const std::string &method="GET");

    const std::vector<Request> &requests() const { return m_requests; }
    /// how many requests were made for \p path with \p method
    size_t count(const std::string &path, const std::string &method="GET") const;

    /// while held, the replies (as they are when asked) wait until answerHeld()
    void hold(bool held) { m_held=held; }
    size_t heldCount() const { return m_waiting.size(); }
    /// delivers the waiting reply \p index (in the order asked) and drops it
    void answerHeld(size_t index);

    void getJson(const std::string &path, ResponseHandler handler) override;
    void postJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler) override;
    void putJson(const std::string &path, const nlohmann::json &body, ResponseHandler handler) override;
    void del(const std::string &path, ResponseHandler handler) override;

private:
    struct Reply {
        int status;
        std::string body;
    };

    void answer(const std::string &method, const std::string &path, const nlohmann::json &body, const ResponseHandler &handler);

    std::map<std::string, Reply> m_replies;
    std::vector<Request> m_requests;
    bool m_held=false;
    std::vector<std::pair<ResponseHandler, Response>> m_waiting;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_FIXTUREDATASOURCE_H_
