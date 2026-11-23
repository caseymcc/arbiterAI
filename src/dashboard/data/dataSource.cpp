#include "dataSource.h"

namespace arbiterAI
{
namespace dashboard
{

Response makeResponse(int status, const std::string &body)
{
    Response response;
    response.status=status;

    if(!body.empty())
        response.json=nlohmann::json::parse(body, nullptr, false);

    const bool parsed=!response.json.is_discarded();
    if(!parsed)
        response.json=nlohmann::json();

    if(status>=200 && status<300)
    {
        response.ok=parsed;
        if(!parsed)
            response.error="the server sent a reply that is not JSON";
        return response;
    }

    if(status==0)
    {
        response.error="the server cannot be reached";
        return response;
    }

    if(response.json.is_object() && response.json.contains("error"))
    {
        const nlohmann::json &error=response.json["error"];
        if(error.is_string())
            response.error=error.get<std::string>();
        else if(error.is_object() && error.contains("message") && error["message"].is_string())
            response.error=error["message"].get<std::string>();
    }
    if(response.error.empty())
        response.error="HTTP "+std::to_string(status);
    return response;
}

}//namespace dashboard
}//namespace arbiterAI
