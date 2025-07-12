#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include <cxxopts.hpp>

#include "arbiterAI/arbiterAI.h"

int main(int argc, char *argv[])
{
    cxxopts::Options options("arbiterAI-cli", "A simple CLI for the ArbiterAI library");

    options.add_options()
        ("p,prompt", "The text prompt", cxxopts::value<std::string>()->default_value(""))
        ("M,messages", "The messages in json format", cxxopts::value<std::string>()->default_value(""));
    options.add_options()
        ("m,model", "The model to use", cxxopts::value<std::string>());
    options.add_options()
        ("s,stream", "Enable streaming completion", cxxopts::value<bool>()->default_value("false"));
    options.add_options()
        ("h,help", "Print usage");

    auto result=options.parse(argc, argv);

    if(result.count("help"))
    {
        std::cout<<options.help()<<std::endl;
        return 0;
    }

    if(result["prompt"].as<std::string>().empty()&&result["messages"].as<std::string>().empty())
    {
        std::cerr<<"Error: either --prompt or --messages is required."<<std::endl;
        std::cerr<<options.help()<<std::endl;
        return 1;
    }

    try
    {
        arbiterAI::arbiterAI arbiterAI;

        arbiterAI::CompletionRequest request;
        if(!result["prompt"].as<std::string>().empty())
        {
            request.messages.push_back({ "user", result["prompt"].as<std::string>() });
        }

        if(!result["messages"].as<std::string>().empty())
        {
            nlohmann::json messages_json=nlohmann::json::parse(result["messages"].as<std::string>());
            for(const auto &msg:messages_json)
            {
                request.messages.push_back({ msg["role"], msg["content"] });
            }
        }

        if(result.count("model"))
        {
            request.model=result["model"].as<std::string>();
        }

        if(result["stream"].as<bool>())
        {
            arbiterAI.streamingCompletion(request, [](const std::string &chunk)
                {
                    std::cout<<chunk<<std::flush;
                });
            std::cout<<std::endl;
        }
        else
        {
            arbiterAI::CompletionResponse response;
            arbiterAI::ErrorCode err=arbiterAI.completion(request, response);
            if(err!=arbiterAI::ErrorCode::Success)
            {
                std::cerr<<"Error: "<<static_cast<int>(err)<<std::endl;
                return 1;
            }
            std::cout<<response.text<<std::endl;
        }
    }
    catch(const std::exception &e)
    {
        std::cerr<<"An unexpected error occurred: "<<e.what()<<std::endl;
        return 1;
    }

    return 0;
}