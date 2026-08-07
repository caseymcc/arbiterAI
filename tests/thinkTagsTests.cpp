#include <gtest/gtest.h>

#include "arbiterAI/thinkTags.h"

namespace arbiterAI
{

TEST(ThinkTagsTest, PlainTextPassesThrough)
{
    ThinkTagParseResult r=parseThinkTags("The answer is 4.");
    EXPECT_EQ(r.content, "The answer is 4.");
    EXPECT_TRUE(r.reasoningContent.empty());
}

TEST(ThinkTagsTest, SplitsReasoningFromContent)
{
    ThinkTagParseResult r=parseThinkTags("<think>\nUser wants 2+2. That is 4.\n</think>\n\n4");
    EXPECT_EQ(r.content, "4");
    EXPECT_EQ(r.reasoningContent, "\nUser wants 2+2. That is 4.\n");
}

TEST(ThinkTagsTest, KeepsTextBeforeTheBlock)
{
    ThinkTagParseResult r=parseThinkTags("prefix <think>hmm</think> suffix");
    EXPECT_EQ(r.content, "prefix  suffix");
    EXPECT_EQ(r.reasoningContent, "hmm");
}

TEST(ThinkTagsTest, HandlesMultipleBlocks)
{
    ThinkTagParseResult r=parseThinkTags("<think>one</think>A<think>two</think>B");
    EXPECT_EQ(r.content, "AB");
    EXPECT_EQ(r.reasoningContent, "one\ntwo");
}

TEST(ThinkTagsTest, UnterminatedBlockDoesNotLeakIntoContent)
{
    // Generation hit the token limit mid-thought.
    ThinkTagParseResult r=parseThinkTags("<think>still thinking and then it stops");
    EXPECT_TRUE(r.content.empty());
    EXPECT_EQ(r.reasoningContent, "still thinking and then it stops");
}

TEST(ThinkTagsTest, ContentOfOnlyWhitespaceBecomesEmpty)
{
    ThinkTagParseResult r=parseThinkTags("<think>x</think>\n\n   ");
    EXPECT_TRUE(r.content.empty());
    EXPECT_EQ(r.reasoningContent, "x");
}

// ─── Streaming ───────────────────────────────────────────────

namespace
{

/// Feed `text` one character at a time — the worst case for tags spanning
/// chunk boundaries.
void feedCharByChar(ThinkTagStreamParser &parser, const std::string &text,
    std::string &content, std::string &reasoning)
{
    for(char c:text)
    {
        parser.feed(std::string(1, c), content, reasoning);
    }
    parser.flush(content, reasoning);
}

} // namespace

TEST(ThinkTagStreamTest, SplitsAcrossCharacterChunks)
{
    ThinkTagStreamParser parser;
    std::string content, reasoning;

    feedCharByChar(parser, "<think>reasoning here</think>final answer", content, reasoning);

    EXPECT_EQ(content, "final answer");
    EXPECT_EQ(reasoning, "reasoning here");
    EXPECT_EQ(parser.reasoning(), "reasoning here");
}

TEST(ThinkTagStreamTest, TagSplitAcrossChunkBoundary)
{
    ThinkTagStreamParser parser;
    std::string content, reasoning;

    // The opening tag arrives in three pieces.
    parser.feed("<th", content, reasoning);
    EXPECT_TRUE(content.empty())<<"a partial tag must not be emitted as content";
    parser.feed("ink>abc</thi", content, reasoning);
    parser.feed("nk>xyz", content, reasoning);
    parser.flush(content, reasoning);

    EXPECT_EQ(content, "xyz");
    EXPECT_EQ(reasoning, "abc");
}

TEST(ThinkTagStreamTest, PlainStreamIsUnchanged)
{
    ThinkTagStreamParser parser;
    std::string content, reasoning;

    feedCharByChar(parser, "just a normal answer", content, reasoning);

    EXPECT_EQ(content, "just a normal answer");
    EXPECT_TRUE(reasoning.empty());
}

TEST(ThinkTagStreamTest, UnterminatedBlockFlushesToReasoning)
{
    ThinkTagStreamParser parser;
    std::string content, reasoning;

    parser.feed("<think>cut off mid thou", content, reasoning);
    EXPECT_TRUE(parser.inThink());
    parser.flush(content, reasoning);

    EXPECT_TRUE(content.empty());
    EXPECT_EQ(reasoning, "cut off mid thou");
}

TEST(ThinkTagStreamTest, TextBeforeBlockIsContent)
{
    ThinkTagStreamParser parser;
    std::string content, reasoning;

    feedCharByChar(parser, "hello <think>quiet</think> world", content, reasoning);

    EXPECT_EQ(content, "hello  world");
    EXPECT_EQ(reasoning, "quiet");
}

TEST(ThinkTagStreamTest, MatchesNonStreamingParse)
{
    const std::string text="<think>step one\nstep two</think>\n\nThe answer.";

    ThinkTagParseResult batch=parseThinkTags(text);

    ThinkTagStreamParser parser;
    std::string content, reasoning;
    feedCharByChar(parser, text, content, reasoning);

    EXPECT_EQ(reasoning, batch.reasoningContent);
    // The streaming path can't retroactively trim the blank lines the batch
    // parser strips, so compare on the trimmed form.
    size_t firstReal=content.find_first_not_of(" \t\r\n");
    std::string trimmed=firstReal==std::string::npos?"":content.substr(firstReal);
    EXPECT_EQ(trimmed, batch.content);
}

} // namespace arbiterAI
