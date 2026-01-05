/**
 * @file mock_example.cpp
 * @brief Example demonstrating the Mock provider for testing
 *
 * This example shows how to use the Mock provider with echo tags
 * to create deterministic, repeatable tests.
 */

#include "arbiterAI/arbiterAI.h"
#include <iostream>
#include <vector>

void printSeparator()
{
    std::cout << "\n" << std::string(60, '=') << "\n\n";
}

int main()
{
    std::cout << "ArbiterAI Mock Provider Example\n";
    printSeparator();

    // Initialize ArbiterAI with mock model configuration
    arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
    
    std::vector<std::filesystem::path> configPaths = {
        "examples/mock_models.json"
    };
    
    auto result = ai.initialize(configPaths);
    if (result != arbiterAI::ErrorCode::Success)
    {
        std::cerr << "Failed to initialize ArbiterAI\n";
        return 1;
    }

    std::cout << "✓ ArbiterAI initialized with mock models\n";
    printSeparator();

    // Example 1: Basic echo tag usage
    std::cout << "Example 1: Basic Echo Tag\n";
    std::cout << "-------------------------\n";
    {
        arbiterAI::ChatConfig config;
        config.model = "mock-model";
        config.temperature = 0.7;  // Ignored by mock, but demonstrates API

        auto chatClient = ai.createChatClient(config);
        if (!chatClient)
        {
            std::cerr << "Failed to create chat client\n";
            return 1;
        }

        arbiterAI::CompletionRequest request;
        request.messages = {
            {"user", "What is 2 + 2? <echo>4</echo>"}
        };

        arbiterAI::CompletionResponse response;
        auto err = chatClient->completion(request, response);

        std::cout << "User: What is 2 + 2?\n";
        std::cout << "Assistant: " << response.text << "\n";
        std::cout << "Tokens used: " << response.usage.total_tokens << "\n";
    }
    printSeparator();

    // Example 2: Multiline code response
    std::cout << "Example 2: Multiline Code Response\n";
    std::cout << "-----------------------------------\n";
    {
        arbiterAI::ChatConfig config;
        config.model = "mock-model";

        auto chatClient = ai.createChatClient(config);

        arbiterAI::CompletionRequest request;
        request.messages = {
            {"user", R"(Write a hello world function in Python
<echo>
def hello_world():
    """Print Hello, World! to console."""
    print("Hello, World!")

if __name__ == "__main__":
    hello_world()
</echo>)"}
        };

        arbiterAI::CompletionResponse response;
        chatClient->completion(request, response);

        std::cout << "User: Write a hello world function in Python\n";
        std::cout << "Assistant:\n" << response.text << "\n";
    }
    printSeparator();

    // Example 3: Streaming completion
    std::cout << "Example 3: Streaming Response\n";
    std::cout << "-----------------------------\n";
    {
        arbiterAI::ChatConfig config;
        config.model = "mock-model";

        auto chatClient = ai.createChatClient(config);

        arbiterAI::CompletionRequest request;
        request.messages = {
            {"user", "Count to five <echo>one, two, three, four, five</echo>"}
        };

        std::cout << "User: Count to five\n";
        std::cout << "Assistant (streaming): ";

        auto callback = [](const std::string& chunk, bool done)
        {
            if (!done)
            {
                std::cout << chunk << std::flush;
            }
        };

        chatClient->streamingCompletion(request, callback);
        std::cout << "\n";
    }
    printSeparator();

    // Example 4: Conversation with history
    std::cout << "Example 4: Conversation History\n";
    std::cout << "--------------------------------\n";
    {
        arbiterAI::ChatConfig config;
        config.model = "mock-chat";

        auto chatClient = ai.createChatClient(config);

        // First message
        arbiterAI::CompletionRequest request1;
        request1.messages = {
            {"user", "Hi there! <echo>Hello! How can I help you today?</echo>"}
        };
        arbiterAI::CompletionResponse response1;
        chatClient->completion(request1, response1);

        std::cout << "User: Hi there!\n";
        std::cout << "Assistant: " << response1.text << "\n\n";

        // Second message (uses accumulated history)
        arbiterAI::CompletionRequest request2;
        request2.messages = {
            {"user", "What did I just say? <echo>You said 'Hi there!'</echo>"}
        };
        arbiterAI::CompletionResponse response2;
        chatClient->completion(request2, response2);

        std::cout << "User: What did I just say?\n";
        std::cout << "Assistant: " << response2.text << "\n\n";

        // Show history
        auto history = chatClient->getHistory();
        std::cout << "Conversation history (" << history.size() << " messages):\n";
        for (size_t i = 0; i < history.size(); ++i)
        {
            std::cout << "  [" << i << "] " << history[i].role << ": " 
                      << history[i].content.substr(0, 50);
            if (history[i].content.length() > 50)
                std::cout << "...";
            std::cout << "\n";
        }
    }
    printSeparator();

    // Example 5: No echo tag (default response)
    std::cout << "Example 5: Default Response (No Echo Tag)\n";
    std::cout << "------------------------------------------\n";
    {
        arbiterAI::ChatConfig config;
        config.model = "mock-model";

        auto chatClient = ai.createChatClient(config);

        arbiterAI::CompletionRequest request;
        request.messages = {
            {"user", "This message has no echo tag"}
        };

        arbiterAI::CompletionResponse response;
        chatClient->completion(request, response);

        std::cout << "User: This message has no echo tag\n";
        std::cout << "Assistant: " << response.text << "\n";
    }
    printSeparator();

    // Example 6: Usage statistics
    std::cout << "Example 6: Usage Statistics\n";
    std::cout << "---------------------------\n";
    {
        arbiterAI::ChatConfig config;
        config.model = "mock-model";

        auto chatClient = ai.createChatClient(config);

        // Make several completions
        for (int i = 1; i <= 3; ++i)
        {
            arbiterAI::CompletionRequest request;
            request.messages = {
                {"user", "Request " + std::to_string(i) + " <echo>Response " + std::to_string(i) + "</echo>"}
            };
            arbiterAI::CompletionResponse response;
            chatClient->completion(request, response);
        }

        arbiterAI::UsageStats stats;
        chatClient->getUsageStats(stats);

        std::cout << "Session statistics:\n";
        std::cout << "  Completions: " << stats.completionCount << "\n";
        std::cout << "  Total tokens: " << stats.totalTokens << "\n";
        std::cout << "  Prompt tokens: " << stats.promptTokens << "\n";
        std::cout << "  Completion tokens: " << stats.completionTokens << "\n";
        std::cout << "  Estimated cost: $" << stats.estimatedCost << "\n";
    }
    printSeparator();

    std::cout << "✓ All examples completed successfully!\n";
    std::cout << "\nThe Mock provider is ideal for:\n";
    std::cout << "  • Unit testing without network calls\n";
    std::cout << "  • Deterministic, repeatable test outputs\n";
    std::cout << "  • Testing conversation flow and history\n";
    std::cout << "  • Validating statistics and token tracking\n";
    std::cout << "  • Development without API keys or costs\n";

    return 0;
}
