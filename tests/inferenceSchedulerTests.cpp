#include "arbiterAI/inferenceScheduler.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/modelRuntime.h"

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <thread>

namespace arbiterAI
{

// ── TokenChannel ──────────────────────────────────────────────

TEST(TokenChannelTest, PushPopOrder)
{
    TokenChannel channel;

    channel.push("alpha");
    channel.push("beta");

    std::string token;
    ASSERT_TRUE(channel.pop(token, std::chrono::milliseconds(100)));
    EXPECT_EQ(token, "alpha");

    ASSERT_TRUE(channel.pop(token, std::chrono::milliseconds(100)));
    EXPECT_EQ(token, "beta");
}

TEST(TokenChannelTest, PopDrainsAfterFinish)
{
    TokenChannel channel;

    channel.push("alpha");
    channel.push("beta");
    channel.finish(ErrorCode::Success);

    std::string token;
    ASSERT_TRUE(channel.pop(token, std::chrono::milliseconds(100)));
    EXPECT_EQ(token, "alpha");

    ASSERT_TRUE(channel.pop(token, std::chrono::milliseconds(100)));
    EXPECT_EQ(token, "beta");

    EXPECT_FALSE(channel.pop(token, std::chrono::milliseconds(100)));
    EXPECT_TRUE(channel.isDone());
    EXPECT_EQ(channel.getResult(), ErrorCode::Success);
}

TEST(TokenChannelTest, PopTimeoutKeepsAlive)
{
    TokenChannel channel;

    // No tokens and not finished — pop times out but reports the
    // channel as still alive (caller uses this to send keepalives).
    std::string token;
    EXPECT_TRUE(channel.pop(token, std::chrono::milliseconds(50)));
    EXPECT_TRUE(token.empty());
    EXPECT_FALSE(channel.isDone());
}

TEST(TokenChannelTest, FinishStoresResult)
{
    TokenChannel channel;

    channel.finish(ErrorCode::NetworkError);

    std::string token;
    EXPECT_FALSE(channel.pop(token, std::chrono::milliseconds(100)));
    EXPECT_TRUE(channel.isDone());
    EXPECT_EQ(channel.getResult(), ErrorCode::NetworkError);
}

TEST(TokenChannelTest, CancelWakesConsumer)
{
    TokenChannel channel;

    std::thread canceller([&channel]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        channel.cancel();
    });

    std::string token;
    bool alive=channel.pop(token, std::chrono::seconds(5));
    canceller.join();

    EXPECT_FALSE(alive);
    EXPECT_TRUE(channel.isCancelled());
}

TEST(TokenChannelTest, PushAfterCancelIsDropped)
{
    TokenChannel channel;

    channel.cancel();
    channel.push("alpha");

    std::string token;
    EXPECT_FALSE(channel.pop(token, std::chrono::milliseconds(100)));
}

TEST(TokenChannelTest, ConcurrentProducerConsumer)
{
    TokenChannel channel;
    constexpr int tokenCount=200;

    std::thread producer([&channel]()
    {
        for(int i=0; i<tokenCount; i++)
        {
            channel.push("tok"+std::to_string(i));
        }
        channel.finish(ErrorCode::Success);
    });

    int received=0;
    int idlePolls=0;

    while(idlePolls<100)
    {
        std::string token;
        if(!channel.pop(token, std::chrono::milliseconds(100)))
        {
            break;
        }
        if(token.empty())
        {
            idlePolls++;
            continue;
        }
        received++;
    }
    producer.join();

    EXPECT_EQ(received, tokenCount);
    EXPECT_EQ(channel.getResult(), ErrorCode::Success);
}

// ── InferenceScheduler ────────────────────────────────────────

namespace
{

bool waitForComplete(const std::shared_ptr<InferenceJob> &job,
    std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(job->completionMutex);
    return job->completionCv.wait_for(lock, timeout, [&job]()
    {
        return job->complete.load();
    });
}

} // anonymous namespace

class InferenceSchedulerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ModelRuntime::reset();
        ModelManager::reset();

        // No GPUs — the scheduler falls back to a single CPU queue.
        InferenceScheduler::instance().initialize({});
    }

    void TearDown() override
    {
        InferenceScheduler::instance().shutdown();
    }
};

TEST_F(InferenceSchedulerTest, InitializeCreatesCpuQueueWhenNoGpus)
{
    EXPECT_TRUE(InferenceScheduler::instance().isRunning());
    EXPECT_EQ(InferenceScheduler::instance().getTotalQueueDepth(), 0);
    EXPECT_TRUE(InferenceScheduler::instance().getActiveJobs().empty());
}

TEST_F(InferenceSchedulerTest, SubmitUnknownModelFailsAndSignalsCompletion)
{
    CompletionRequest request;
    request.model="no-such-model";
    request.messages={{"user", "hello"}};

    auto job=InferenceScheduler::instance().submit(request, false);
    ASSERT_TRUE(job!=nullptr);

    ASSERT_TRUE(waitForComplete(job, std::chrono::seconds(10)));
    EXPECT_NE(job->result, ErrorCode::Success);

    // Terminal jobs must be removed from active tracking
    EXPECT_EQ(InferenceScheduler::instance().getJobStage(job->id), InferenceStage::Complete);
    EXPECT_TRUE(InferenceScheduler::instance().getActiveJobs().empty());
}

TEST_F(InferenceSchedulerTest, StreamingFailureFinishesChannel)
{
    CompletionRequest request;
    request.model="no-such-model";
    request.messages={{"user", "hello"}};

    auto job=InferenceScheduler::instance().submit(request, true);
    ASSERT_TRUE(job!=nullptr);
    ASSERT_TRUE(job->channel!=nullptr);

    // Pop until the channel reports finished — a failed job must
    // always finish its channel so the HTTP thread is not left hanging.
    int idlePolls=0;
    bool finished=false;

    while(idlePolls<100)
    {
        std::string token;
        if(!job->channel->pop(token, std::chrono::milliseconds(100)))
        {
            finished=true;
            break;
        }
        if(token.empty())
        {
            idlePolls++;
        }
    }

    ASSERT_TRUE(finished);
    EXPECT_TRUE(job->channel->isDone());
    EXPECT_NE(job->channel->getResult(), ErrorCode::Success);
}

TEST_F(InferenceSchedulerTest, CancelledJobSignalsCompletion)
{
    CompletionRequest request;
    request.model="no-such-model";
    request.messages={{"user", "hello"}};

    auto job=InferenceScheduler::instance().submit(request, false);
    InferenceScheduler::instance().cancel(job->id);

    // The job either gets cancelled or fails at load — but it must
    // always reach a terminal state and wake any waiter.
    ASSERT_TRUE(waitForComplete(job, std::chrono::seconds(10)));
    EXPECT_NE(job->result, ErrorCode::Success);
    EXPECT_TRUE(InferenceScheduler::instance().getActiveJobs().empty());
}

TEST_F(InferenceSchedulerTest, CancelUnknownJobIsNoop)
{
    InferenceScheduler::instance().cancel(987654321);
    EXPECT_TRUE(InferenceScheduler::instance().isRunning());
}

TEST_F(InferenceSchedulerTest, JobIdsAreUnique)
{
    CompletionRequest request;
    request.model="no-such-model";
    request.messages={{"user", "hello"}};

    auto job1=InferenceScheduler::instance().submit(request, false);
    auto job2=InferenceScheduler::instance().submit(request, false);

    EXPECT_NE(job1->id, job2->id);

    EXPECT_TRUE(waitForComplete(job1, std::chrono::seconds(10)));
    EXPECT_TRUE(waitForComplete(job2, std::chrono::seconds(10)));
}

TEST_F(InferenceSchedulerTest, GetJobStageUnknownReturnsComplete)
{
    EXPECT_EQ(InferenceScheduler::instance().getJobStage(123456789), InferenceStage::Complete);
}

TEST_F(InferenceSchedulerTest, ShutdownIsIdempotent)
{
    InferenceScheduler::instance().shutdown();
    EXPECT_FALSE(InferenceScheduler::instance().isRunning());

    InferenceScheduler::instance().shutdown();
    EXPECT_FALSE(InferenceScheduler::instance().isRunning());
}

} // namespace arbiterAI
