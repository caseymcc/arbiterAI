# Mock Provider Quick Reference

## TL;DR

```cpp
// Use echo tags to control mock responses
request.messages = {{"user", "Question <echo>Answer</echo>"}};
client->completion(request, response);
// response.text == "Answer"
```

## Configuration

```json
{
  "models": [{
    "name": "mock-model",
    "provider": "mock"
  }]
}
```

## Echo Tag Syntax

| Pattern | Example | Result |
|---------|---------|--------|
| Basic | `<echo>text</echo>` | `"text"` |
| Multiline | `<echo>\nline1\nline2\n</echo>` | `"line1\nline2"` |
| Case insensitive | `<ECHO>text</ECHO>` | `"text"` |
| Multiple tags | `<echo>first</echo> <echo>second</echo>` | `"first"` (first match) |
| No tag | `message without tag` | Default mock message |
| Empty | `<echo></echo>` | `""` (empty string) |

## Quick Examples

### Simple Test
```cpp
TEST(Test, Simple) {
    request.messages = {{"user", "Hi <echo>Hello</echo>"}};
    client->completion(request, response);
    EXPECT_EQ(response.text, "Hello");
}
```

### Code Generation
```cpp
request.messages = {{"user", R"(
Code <echo>
def func():
    pass
</echo>
)"}};
// response.text == "def func():\n    pass"
```

### Streaming
```cpp
std::string result;
auto cb = [&](const std::string& chunk, bool done) {
    if (!done) result += chunk;
};
request.messages = {{"user", "Test <echo>stream</echo>"}};
client->streamingCompletion(request, cb);
// result == "stream"
```

### Conversation
```cpp
// Turn 1
request.messages = {{"user", "Hi <echo>Hello!</echo>"}};
client->completion(request, r1);

// Turn 2 (uses history)
request.messages = {{"user", "Name? <echo>I'm AI</echo>"}};
client->completion(request, r2);

// History contains 4 messages (2 user + 2 assistant)
```

## Common Patterns

### Test Setup
```cpp
arbiterAI::ArbiterAI& ai = arbiterAI::ArbiterAI::instance();
ai.initialize({"examples/mock_models.json"});

arbiterAI::ChatConfig config;
config.model = "mock-model";
auto client = ai.createChatClient(config);
```

### Assertion Pattern
```cpp
EXPECT_EQ(client->completion(request, response), ErrorCode::Success);
EXPECT_EQ(response.text, "expected");
EXPECT_GT(response.usage.total_tokens, 0);
```

### Streaming Pattern
```cpp
std::string accumulated;
int chunks = 0;
auto callback = [&](const std::string& chunk, bool done) {
    if (!done) {
        accumulated += chunk;
        chunks++;
    }
};
client->streamingCompletion(request, callback);
EXPECT_GT(chunks, 1);
```

## Files

| File | Purpose |
|------|---------|
| `src/arbiterAI/providers/mock.h` | Header |
| `src/arbiterAI/providers/mock.cpp` | Implementation |
| `docs/testing.md` | Full documentation |
| `examples/mock_example.cpp` | Working examples |
| `examples/mock_models.json` | Config |
| `tests/mockProviderTests.cpp` | Unit tests |

## See Also

- [Testing Guide](docs/testing.md) - Comprehensive testing documentation
- [Examples](examples/README.md) - Example code and patterns
- [Developer Guide](docs/developer.md) - Architecture details
