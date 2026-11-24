#include "arbiterAI/providers/reasoningBudget.h"

#include <gtest/gtest.h>

namespace arbiterAI
{

namespace
{

const ThinkTokens thinkTokens{100, 101};
const int32_t wordToken=7;

// Feeds count ordinary tokens; returns how many asked for an injection.
int feedWords(ReasoningBudget &budget, int count)
{
    int asks=0;
    for(int i=0; i<count; ++i)
    {
        if(budget.observe(wordToken, " word"))
            asks++;
    }
    return asks;
}

}//namespace

TEST(ReasoningBudget, UnsetBudgetIsANoOp)
{
    ReasoningBudget budget(std::nullopt, thinkTokens);

    EXPECT_FALSE(budget.enabled());
    EXPECT_FALSE(budget.wantsPrefill());
    EXPECT_FALSE(budget.observe(thinkTokens.open, "<think>"));
    EXPECT_EQ(feedWords(budget, 50), 0);
}

TEST(ReasoningBudget, NoThinkTokensInVocabIsANoOp)
{
    for(int value:{0, 4})
    {
        ReasoningBudget budget(value, ThinkTokens{});

        EXPECT_FALSE(budget.enabled()) << "budget " << value;
        EXPECT_FALSE(budget.wantsPrefill()) << "budget " << value;
        budget.observePrompt("<|im_start|>assistant\n<think>\n");
        EXPECT_FALSE(budget.observe(wordToken, "<think>")) << "budget " << value;
        EXPECT_EQ(feedWords(budget, 20), 0) << "budget " << value;
        EXPECT_FALSE(budget.inThinking()) << "budget " << value;
    }
}

TEST(ReasoningBudget, ZeroBudgetWantsThePrefill)
{
    ReasoningBudget zero(0, thinkTokens);
    EXPECT_TRUE(zero.enabled());
    EXPECT_TRUE(zero.wantsPrefill());
    EXPECT_EQ(ReasoningBudget::prefillText(), "<think>\n\n</think>\n\n");

    ReasoningBudget capped(1024, thinkTokens);
    EXPECT_TRUE(capped.enabled());
    EXPECT_FALSE(capped.wantsPrefill());
}

TEST(ReasoningBudget, PromptEndingWithThePrefillIsClosed)
{
    ReasoningBudget budget(0, thinkTokens);
    budget.observePrompt("<|im_start|>assistant\n"+ReasoningBudget::prefillText());

    EXPECT_FALSE(budget.inThinking());
    EXPECT_EQ(feedWords(budget, 20), 0);
}

TEST(ReasoningBudget, OpenTagAsOneSpecialToken)
{
    ReasoningBudget budget(3, thinkTokens);

    EXPECT_FALSE(budget.observe(thinkTokens.open, ""));
    EXPECT_TRUE(budget.inThinking());
    EXPECT_FALSE(budget.observe(wordToken, " a"));
    EXPECT_FALSE(budget.observe(wordToken, " b"));
    EXPECT_TRUE(budget.observe(wordToken, " c"));
    EXPECT_EQ(budget.thinkingTokens(), 3);
}

TEST(ReasoningBudget, OpenTagSplitAcrossPieces)
{
    ReasoningBudget budget(2, thinkTokens);

    EXPECT_FALSE(budget.observe(wordToken, "<th"));
    EXPECT_FALSE(budget.inThinking());
    EXPECT_FALSE(budget.observe(wordToken, "ink"));
    EXPECT_FALSE(budget.observe(wordToken, ">"));
    EXPECT_TRUE(budget.inThinking());
    EXPECT_EQ(budget.thinkingTokens(), 0);
    EXPECT_FALSE(budget.observe(wordToken, " a"));
    EXPECT_TRUE(budget.observe(wordToken, " b"));
}

TEST(ReasoningBudget, CloseTagSplitAcrossPiecesEndsTheBlock)
{
    ReasoningBudget budget(10, thinkTokens);

    budget.observe(thinkTokens.open, "<think>");
    budget.observe(wordToken, " a");
    budget.observe(wordToken, "</th");
    EXPECT_TRUE(budget.inThinking());
    budget.observe(wordToken, "ink>");
    EXPECT_FALSE(budget.inThinking());
    EXPECT_EQ(feedWords(budget, 20), 0);
}

TEST(ReasoningBudget, PromptEndingInsideTheBlock)
{
    ReasoningBudget budget(2, thinkTokens);
    budget.observePrompt("<|im_start|>user\nhi<|im_end|>\n<|im_start|>assistant\n<think>\n");

    EXPECT_TRUE(budget.inThinking());
    EXPECT_FALSE(budget.observe(wordToken, " a"));
    EXPECT_TRUE(budget.observe(wordToken, " b"));
}

TEST(ReasoningBudget, PromptWithAClosedEarlierBlockIsNotInside)
{
    ReasoningBudget budget(2, thinkTokens);
    budget.observePrompt("<|im_start|>assistant\n<think>\nold\n</think>\n\nanswer<|im_end|>\n<|im_start|>assistant\n");

    EXPECT_FALSE(budget.inThinking());
    EXPECT_EQ(feedWords(budget, 20), 0);
}

TEST(ReasoningBudget, CapReachedEmitsTheCloseOnce)
{
    ReasoningBudget budget(4, thinkTokens);

    budget.observe(thinkTokens.open, "<think>");
    EXPECT_EQ(feedWords(budget, 3), 0);
    EXPECT_TRUE(budget.observe(wordToken, " d"));
    budget.injected();

    EXPECT_FALSE(budget.inThinking());
    EXPECT_EQ(budget.injections(), 1);
    EXPECT_EQ(feedWords(budget, 50), 0);
    EXPECT_EQ(budget.injections(), 1);
}

TEST(ReasoningBudget, ModelClosingBeforeTheCapInjectsNothing)
{
    ReasoningBudget budget(8, thinkTokens);

    budget.observe(thinkTokens.open, "<think>");
    EXPECT_EQ(feedWords(budget, 5), 0);
    EXPECT_FALSE(budget.observe(thinkTokens.close, "</think>"));
    EXPECT_FALSE(budget.inThinking());
    EXPECT_EQ(feedWords(budget, 50), 0);
    EXPECT_EQ(budget.injections(), 0);
}

TEST(ReasoningBudget, ZeroBudgetClosesABlockTheModelOpensAnyway)
{
    ReasoningBudget budget(0, thinkTokens);
    budget.observePrompt(ReasoningBudget::prefillText());

    EXPECT_TRUE(budget.observe(thinkTokens.open, "<think>"));
}

}//namespace arbiterAI
