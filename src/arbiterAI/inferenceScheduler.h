#ifndef _ARBITERAI_INFERENCESCHEDULER_H_
#define _ARBITERAI_INFERENCESCHEDULER_H_

#include "arbiterAI/arbiterAI.h"
#include "arbiterAI/providers/llama.h"

#include <string>
#include <vector>
#include <deque>
#include <queue>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>
#include <cstdint>

namespace arbiterAI
{

/// States a request passes through in the inference pipeline.
enum class InferenceStage
{
    Queued,             // Waiting in the submission queue
    Tokenizing,         // Being tokenized by the tokenizer thread
    WaitingAccelerator, // Tokenized, waiting for an accelerator slot
    Inferring,          // Running on an accelerator
    Complete,           // Finished (success or error)
    Cancelled           // Cancelled by client disconnect
};

/// Thread-safe channel for streaming tokens from accelerator to HTTP thread.
class TokenChannel {
public:
    /// Push a token/chunk. Called by accelerator thread.
    void push(const std::string &token);

    /// Signal that generation is complete.
    void finish(ErrorCode result);

    /// Block until a token is available or finished.
    /// Returns false when the channel is finished (no more tokens).
    bool pop(std::string &token, std::chrono::milliseconds timeout=std::chrono::milliseconds(500));

    /// Check if the channel is done (finished or error).
    bool isDone() const;

    /// Get the final error code (only valid after isDone()).
    ErrorCode getResult() const;

    /// Signal cancellation (e.g. client disconnect).
    void cancel();

    bool isCancelled() const;

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::string> m_tokens;
    std::atomic<bool> m_done{false};
    std::atomic<bool> m_cancelled{false};
    ErrorCode m_result=ErrorCode::Success;
};

/// A single inference request flowing through the pipeline.
struct InferenceJob {
    /// Unique job ID for tracking.
    uint64_t id=0;

    /// The original completion request.
    CompletionRequest request;

    /// Whether this is a streaming request.
    bool streaming=false;

    /// Current pipeline stage (display/telemetry only — use 'cancelled' for control flow).
    std::atomic<InferenceStage> stage{InferenceStage::Queued};

    /// Cancellation flag. Separate from 'stage' so a cancel can never be
    /// lost to a concurrent forward stage transition.
    std::atomic<bool> cancelled{false};

    /// Timestamp when the job was submitted.
    std::chrono::steady_clock::time_point submitTime;

    /// Timestamp when each stage started (for telemetry).
    std::chrono::steady_clock::time_point tokenizeStartTime;
    std::chrono::steady_clock::time_point inferenceStartTime;

    /// Pre-tokenized prompt tokens (filled by tokenizer thread).  For a
    /// multimodal prompt these are the text-chunk tokens only.
    std::vector<int32_t> tokens;
    std::string formattedPrompt;

    /// Tokenized multimodal prompt (image chunks), when the request carries
    /// images.  Empty/invalid for text-only requests.
    MultimodalPrompt multimodal;

    /// Template-derived response parser for this request.  Null when the model
    /// has an api_format override or ships no usable chat template.
    std::shared_ptr<ChatPrompt> chatPrompt;

    /// Result for non-streaming requests.
    std::string resultText;
    int promptTokens=0;
    double promptTimeMs=0.0;
    double generationTimeMs=0.0;
    ErrorCode result=ErrorCode::Success;

    /// Human-readable detail for why the job failed (set alongside a non-Success
    /// result), surfaced in the API error response so clients see the reason,
    /// not just the error code. Empty on success.
    std::string errorDetail;

    /// Completion token count. Atomic because the dashboard snapshots it
    /// while the accelerator thread is still generating.
    std::atomic<int> completionTokens{0};

    /// For streaming requests — tokens pushed here by accelerator.
    std::shared_ptr<TokenChannel> channel;

    /// Condition variable signaled when job is complete (non-streaming).
    std::mutex completionMutex;
    std::condition_variable completionCv;
    std::atomic<bool> complete{false};

    /// Queue position (updated by scheduler for status reporting).
    std::atomic<int> queuePosition{0};
};

/// Snapshot of a single job for the dashboard/API.
struct JobSnapshot {
    uint64_t id=0;
    std::string model;
    InferenceStage stage=InferenceStage::Queued;
    bool streaming=false;
    int promptTokens=0;
    int completionTokens=0;
    int queuePosition=0;
    double elapsedMs=0.0;
};

/// Per-accelerator worker that processes inference jobs from its queue.
struct AcceleratorQueue {
    int gpuIndex=-1;
    std::string deviceName;

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::shared_ptr<InferenceJob>> jobs;
    std::shared_ptr<InferenceJob> activeJob; // currently inferring

    std::thread workerThread;
    std::atomic<bool> running{false};
};

/// Central inference pipeline scheduler.
///
/// Architecture:
///   HTTP threads → submit() → [tokenizer queue]
///                                    ↓ tokenizer thread
///                              [accelerator queue(s)]
///                                    ↓ accelerator thread(s)
///                              [completion / streaming channel]
///                                    ↓
///                              HTTP thread picks up result
class InferenceScheduler {
public:
    static InferenceScheduler &instance();

    /// Initialize the scheduler with the detected accelerators.
    /// Call once after hardware detection.
    void initialize(const std::vector<int> &gpuIndices);

    /// Shut down all worker threads.
    void shutdown();

    /// Submit a job to the pipeline. Returns immediately.
    /// The caller can wait on job->completionCv (non-streaming)
    /// or read from job->channel (streaming).
    std::shared_ptr<InferenceJob> submit(const CompletionRequest &request, bool streaming);

    /// Cancel a job by ID (e.g. on client disconnect).
    void cancel(uint64_t jobId);

    /// Get the current queue depth across all accelerators.
    int getTotalQueueDepth() const;

    /// Get the queue depth for a specific accelerator.
    int getQueueDepth(int gpuIndex) const;

    /// Get status info for a job.
    InferenceStage getJobStage(uint64_t jobId) const;

    /// Get snapshot of all active (non-complete) jobs.
    std::vector<JobSnapshot> getActiveJobs() const;

    /// Check if the scheduler is initialized and running.
    bool isRunning() const { return m_running.load(); }

private:
    InferenceScheduler()=default;
    ~InferenceScheduler();

    InferenceScheduler(const InferenceScheduler &)=delete;
    InferenceScheduler &operator=(const InferenceScheduler &)=delete;

    /// Mark a job terminal: set result/stage, signal waiters, finish the
    /// streaming channel, and remove it from the active job map.
    void finishJob(const std::shared_ptr<InferenceJob> &job, ErrorCode result);

    /// Tokenizer thread function.
    void tokenizerLoop();

    /// Accelerator worker thread function.
    void acceleratorLoop(AcceleratorQueue &queue);

    /// Pick the best accelerator queue for a job.
    AcceleratorQueue &selectAccelerator(const InferenceJob &job);

    // Tokenizer queue
    mutable std::mutex m_tokenizerMutex;
    std::condition_variable m_tokenizerCv;
    std::deque<std::shared_ptr<InferenceJob>> m_tokenizerQueue;
    std::thread m_tokenizerThread;

    // Accelerator queues (one per GPU)
    std::vector<std::unique_ptr<AcceleratorQueue>> m_accelerators;

    // Job tracking
    mutable std::mutex m_jobsMutex;
    std::map<uint64_t, std::weak_ptr<InferenceJob>> m_activeJobs;
    std::atomic<uint64_t> m_nextJobId{1};

    std::atomic<bool> m_running{false};
};

} // namespace arbiterAI

#endif//_ARBITERAI_INFERENCESCHEDULER_H_
