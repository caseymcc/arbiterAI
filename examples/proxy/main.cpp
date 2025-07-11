#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "arbiterAI/arbiterAI.h"

// Helper to create a JSON error response
nlohmann::json createErrorResponse(const std::string &message)
{
    return {
        {"error", {
            {"message", message},
            {"type", "invalid_request_error"}
        }}
    };
}

// Helper to create an OpenAI-compatible choice from a CompletionResponse
nlohmann::json createChoice(const arbiterAI::CompletionResponse &resp, int index=0)
{
    return {
        {"index", index},
        {"message", {
            {"role", "assistant"},
            {"content", resp.text}
        }},
        {"finish_reason", "stop"}
    };
}

// Helper to create an OpenAI-compatible streaming choice
nlohmann::json createStreamingChoice(const std::string &content, int index=0)
{
    return {
        {"index", index},
        {"delta", {
            {"content", content}
        }},
        {"finish_reason", nullptr}
    };
}

int main()
{
    httplib::Server svr;

    // Initialize ArbiterAI
    arbiterAI::arbiterAI arbiterInstance;
    std::vector<std::filesystem::path> configPaths={ "./examples/model_config_v2.json" };
    arbiterAI::ErrorCode err=arbiterInstance.initialize(configPaths);
    if(err!=arbiterAI::ErrorCode::Success)
    {
        std::cerr<<"Failed to initialize ArbiterAI, error code: "<<static_cast<int>(err)<<std::endl;
        return 1;
    }

    svr.Post("/v1/chat/completions", [&](const httplib::Request &req, httplib::Response &res)
        {
            nlohmann::json requestJson;
            try
            {
                requestJson=nlohmann::json::parse(req.body);
            }
            catch(const nlohmann::json::parse_error &e)
            {
                res.status=400;
                res.set_content(createErrorResponse("Failed to parse JSON body").dump(), "application/json");
                return;
            }

            arbiterAI::CompletionRequest arbiterRequest;
            try
            {
                arbiterRequest.model=requestJson.at("model");

                for(const auto &msg:requestJson.at("messages"))
                {
                    arbiterRequest.messages.push_back({ msg.at("role"), msg.at("content") });
                }

                if(requestJson.contains("temperature"))
                {
                    arbiterRequest.temperature=requestJson.at("temperature");
                }
                if(requestJson.contains("max_tokens"))
                {
                    arbiterRequest.max_tokens=requestJson.at("max_tokens");
                }
                if(requestJson.contains("top_p"))
                {
                    arbiterRequest.top_p=requestJson.at("top_p");
                }
                if(requestJson.contains("presence_penalty"))
                {
                    arbiterRequest.presence_penalty=requestJson.at("presence_penalty");
                }
                if(requestJson.contains("frequency_penalty"))
                {
                    arbiterRequest.frequency_penalty=requestJson.at("frequency_penalty");
                }
                if(requestJson.contains("stop"))
                {
                    arbiterRequest.stop=requestJson.at("stop").get<std::vector<std::string>>();
                }

            }
            catch(const nlohmann::json::exception &e)
            {
                res.status=400;
                res.set_content(createErrorResponse(std::string("JSON validation error: ")+e.what()).dump(), "application/json");
                return;
            }

            bool stream=requestJson.value("stream", false);

            if(stream)
            {
                res.set_chunked_content_provider(
                    "text/event-stream",
                    [&](size_t offset, httplib::DataSink &sink)
                    {
                        auto callback=[&](const std::string &chunk)
                            {
                                if(chunk.empty()) return;
                                nlohmann::json sse_chunk={
                                    {"id", "chatcmpl-123"}, // A dummy ID
                                    {"object", "chat.completion.chunk"},
                                    {"created", std::time(nullptr)},
                                    {"model", arbiterRequest.model},
                                    {"choices", {createStreamingChoice(chunk)}}
                                };
                                std::string data_line="data: "+sse_chunk.dump()+"\n\n";
                                sink.write(data_line.c_str(), data_line.length());
                            };

                        arbiterAI::ErrorCode stream_err=arbiterInstance.streamingCompletion(arbiterRequest, callback);

                        if(stream_err!=arbiterAI::ErrorCode::Success)
                        {
                            // This part is tricky because headers are already sent.
                            // We can't change status code. Best we can do is log.
                            std::cerr<<"Streaming completion failed with code: "<<static_cast<int>(stream_err)<<std::endl;
                        }

                        std::string done_line="data: [DONE]\n\n";
                        sink.write(done_line.c_str(), done_line.length());
                        sink.done();
                        return true;
                    }
                );
            }
            else
            {
                arbiterAI::CompletionResponse arbiterResponse;
                arbiterAI::ErrorCode completion_err=arbiterInstance.completion(arbiterRequest, arbiterResponse);

                if(completion_err!=arbiterAI::ErrorCode::Success)
                {
                    res.status=500;
                    res.set_content(createErrorResponse("Failed to get completion from ArbiterAI").dump(), "application/json");
                    return;
                }

                nlohmann::json responseJson={
                    {"id", "chatcmpl-123"}, // A dummy ID
                    {"object", "chat.completion"},
                    {"created", std::time(nullptr)},
                    {"model", arbiterResponse.model},
                    {"choices", {createChoice(arbiterResponse)}},
                    {"usage", {
                        {"prompt_tokens", arbiterResponse.usage.prompt_tokens},
                        {"completion_tokens", arbiterResponse.usage.completion_tokens},
                        {"total_tokens", arbiterResponse.usage.total_tokens}
                    }}
                };

                res.set_content(responseJson.dump(), "application/json");
            }
        });

    int port=8080;
    std::cout<<"Starting proxy server on port "<<port<<std::endl;
    svr.listen("0.0.0.0", port);

    return 0;
}