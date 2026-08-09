#include "arbiterAI/inferenceScheduler.h"
#include "arbiterAI/modelRuntime.h"
#include "arbiterAI/modelManager.h"
#include "arbiterAI/hardwareDetector.h"
#include "arbiterAI/telemetryCollector.h"
#include "arbiterAI/providers/llama.h"

#include <llama.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace arbiterAI
{

// ── TokenChannel ──────────────────────────────────────────────

void TokenChannel::push(const std::string &token)
{
    if(m_cancelled.load()) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens.push(token);
    }
    m_cv.notify_one();
}

void TokenChannel::finish(ErrorCode result)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_result=result;
    }
    m_done.store(true);
    m_cv.notify_all();
}

bool TokenChannel::pop(std::string &token, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if(!m_cv.wait_for(lock, timeout, [this]()
        {
            return !m_tokens.empty()||m_done.load()||m_cancelled.load();
        }))
    {
        // Timeout — no token available, but not done either.
        // Caller can use this to send keepalive.
        return true; // still alive, just no data yet
    }

    if(m_cancelled.load())
    {
        return false;
    }

    if(!m_tokens.empty())
    {
        token=std::move(m_tokens.front());
        m_tokens.pop();
        return true;
    }

    // Queue empty and done — no more tokens.
    return false;
}

bool TokenChannel::isDone() const
{
    return m_done.load();
}

ErrorCode TokenChannel::getResult() const
{
    return m_result;
}

void TokenChannel::cancel()
{
    m_cancelled.store(true);
    m_cv.notify_all();
}

bool TokenChannel::isCancelled() const
{
    return m_cancelled.load();
}

// ── InferenceScheduler ────────────────────────────────────────

InferenceScheduler &InferenceScheduler::instance()
{
    static InferenceScheduler s;
    return s;
}

InferenceScheduler::~InferenceScheduler()
{
    shutdown();
}

void InferenceScheduler::initialize(const std::vector<int> &gpuIndices)
{
    if(m_running.load())
    {
        spdlog::warn("[scheduler] already initialized");
        return;
    }

    spdlog::info("[scheduler] initializing with {} accelerator(s)", gpuIndices.size());

    // Create per-accelerator queues
    for(int idx:gpuIndices)
    {
        auto q=std::make_unique<AcceleratorQueue>();
        q->gpuIndex=idx;
        q->running.store(true);

        auto gpus=HardwareDetector::instance().getGpus();
        for(const GpuInfo &gpu:gpus)
        {
            if(gpu.index==idx)
            {
                q->deviceName=gpu.name;
                break;
            }
        }

        spdlog::info("[scheduler] accelerator {}: {}", idx, q->deviceName);
        m_accelerators.push_back(std::move(q));
    }

    // If no GPUs provided, create a single CPU-based accelerator queue
    if(m_accelerators.empty())
    {
        auto q=std::make_unique<AcceleratorQueue>();
        q->gpuIndex=-1;
        q->deviceName="CPU";
        q->running.store(true);
        spdlog::info("[scheduler] no GPUs specified, using single CPU accelerator queue");
        m_accelerators.push_back(std::move(q));
    }

    m_running.store(true);

    // Start tokenizer thread
    m_tokenizerThread=std::thread(&InferenceScheduler::tokenizerLoop, this);

    // Start accelerator threads
    for(auto &q:m_accelerators)
    {
        q->workerThread=std::thread(&InferenceScheduler::acceleratorLoop, this, std::ref(*q));
    }

    spdlog::info("[scheduler] started: 1 tokenizer thread, {} accelerator thread(s)",
        m_accelerators.size());
}

void InferenceScheduler::shutdown()
{
    if(!m_running.load()) return;

    spdlog::info("[scheduler] shutting down");
    m_running.store(false);

    // Wake tokenizer
    m_tokenizerCv.notify_all();

    // Wake all accelerators
    for(auto &q:m_accelerators)
    {
        q->running.store(false);
        q->cv.notify_all();
    }

    // Join threads
    if(m_tokenizerThread.joinable())
        m_tokenizerThread.join();

    for(auto &q:m_accelerators)
    {
        if(q->workerThread.joinable())
            q->workerThread.join();
    }

    m_accelerators.clear();
    spdlog::info("[scheduler] shutdown complete");
}

std::shared_ptr<InferenceJob> InferenceScheduler::submit(const CompletionRequest &request, bool streaming)
{
    auto job=std::make_shared<InferenceJob>();
    job->id=m_nextJobId.fetch_add(1);
    job->request=request;
    job->streaming=streaming;
    job->submitTime=std::chrono::steady_clock::now();
    job->stage.store(InferenceStage::Queued);

    if(streaming)
    {
        job->channel=std::make_shared<TokenChannel>();
    }

    // Track job
    {
        std::lock_guard<std::mutex> lock(m_jobsMutex);
        m_activeJobs[job->id]=job;
    }

    // Enqueue for tokenization
    {
        std::lock_guard<std::mutex> lock(m_tokenizerMutex);
        m_tokenizerQueue.push_back(job);
    }
    m_tokenizerCv.notify_one();

    spdlog::info("[scheduler] job {} submitted (model='{}', streaming={})",
        job->id, request.model, streaming);

    return job;
}

void InferenceScheduler::cancel(uint64_t jobId)
{
    std::lock_guard<std::mutex> lock(m_jobsMutex);
    auto it=m_activeJobs.find(jobId);
    if(it!=m_activeJobs.end())
    {
        auto job=it->second.lock();
        if(job)
        {
            job->cancelled.store(true);
            job->stage.store(InferenceStage::Cancelled);
            if(job->channel)
            {
                job->channel->cancel();
            }
        }
    }
}

void InferenceScheduler::finishJob(const std::shared_ptr<InferenceJob> &job, ErrorCode result)
{
    job->result=result;
    job->stage.store(result==ErrorCode::Cancelled?
        InferenceStage::Cancelled:InferenceStage::Complete);

    {
        std::lock_guard<std::mutex> lock(job->completionMutex);
        job->complete.store(true);
    }
    job->completionCv.notify_all();

    if(job->channel)
    {
        job->channel->finish(result);
    }

    std::lock_guard<std::mutex> lock(m_jobsMutex);
    m_activeJobs.erase(job->id);
}

int InferenceScheduler::getTotalQueueDepth() const
{
    int total=0;
    {
        std::lock_guard<std::mutex> lock(m_tokenizerMutex);
        total+=static_cast<int>(m_tokenizerQueue.size());
    }
    for(const auto &q:m_accelerators)
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        total+=static_cast<int>(q->jobs.size());
        if(q->activeJob) total++;
    }
    return total;
}

int InferenceScheduler::getQueueDepth(int gpuIndex) const
{
    for(const auto &q:m_accelerators)
    {
        if(q->gpuIndex==gpuIndex)
        {
            std::lock_guard<std::mutex> lock(q->mutex);
            int depth=static_cast<int>(q->jobs.size());
            if(q->activeJob) depth++;
            return depth;
        }
    }
    return 0;
}

InferenceStage InferenceScheduler::getJobStage(uint64_t jobId) const
{
    std::lock_guard<std::mutex> lock(m_jobsMutex);
    auto it=m_activeJobs.find(jobId);
    if(it!=m_activeJobs.end())
    {
        auto job=it->second.lock();
        if(job)
        {
            return job->stage.load();
        }
    }
    return InferenceStage::Complete;
}

std::vector<JobSnapshot> InferenceScheduler::getActiveJobs() const
{
    std::vector<JobSnapshot> result;
    auto now=std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(m_jobsMutex);
    for(const auto &[id, weak]:m_activeJobs)
    {
        auto job=weak.lock();
        if(!job) continue;

        InferenceStage stage=job->stage.load();
        if(stage==InferenceStage::Complete||stage==InferenceStage::Cancelled) continue;
        if(job->cancelled.load()) continue;

        JobSnapshot snap;
        snap.id=job->id;
        snap.model=job->request.model;
        snap.stage=stage;
        snap.streaming=job->streaming;
        // tokens is only stable once the tokenizer has handed the job off
        if(stage==InferenceStage::WaitingAccelerator||stage==InferenceStage::Inferring)
        {
            snap.promptTokens=static_cast<int>(job->tokens.size());
        }
        snap.completionTokens=job->completionTokens.load();
        snap.queuePosition=job->queuePosition.load();
        snap.elapsedMs=std::chrono::duration<double, std::milli>(now-job->submitTime).count();
        result.push_back(snap);
    }
    return result;
}

// ── Tokenizer Thread ──────────────────────────────────────────

void InferenceScheduler::tokenizerLoop()
{
    spdlog::info("[scheduler:tokenizer] thread started");

    while(m_running.load())
    {
        std::shared_ptr<InferenceJob> job;

        {
            std::unique_lock<std::mutex> lock(m_tokenizerMutex);
            m_tokenizerCv.wait(lock, [this]()
            {
                return !m_tokenizerQueue.empty()||!m_running.load();
            });

            if(!m_running.load()) break;
            if(m_tokenizerQueue.empty()) continue;

            job=m_tokenizerQueue.front();
            m_tokenizerQueue.pop_front();
        }

        if(job->cancelled.load())
        {
            finishJob(job, ErrorCode::Cancelled);
            continue;
        }

        job->stage.store(InferenceStage::Tokenizing);
        job->tokenizeStartTime=std::chrono::steady_clock::now();

        spdlog::debug("[scheduler:tokenizer] tokenizing job {} (model='{}')",
            job->id, job->request.model);

        // Ensure model is loaded
        ModelRuntime &runtime=ModelRuntime::instance();
        ErrorCode loadResult=runtime.loadModel(job->request.model);
        if(loadResult!=ErrorCode::Success)
        {
            finishJob(job, loadResult);
            continue;
        }

        llama_model *llamaModel=runtime.getLlamaModel(job->request.model);
        if(!llamaModel)
        {
            finishJob(job, ErrorCode::ModelNotLoaded);
            continue;
        }

        // Get model info for template/format
        std::optional<ModelInfo> modelInfo=runtime.getLoadedModelInfo(job->request.model);
        if(!modelInfo)
        {
            finishJob(job, ErrorCode::ModelNotFound);
            continue;
        }

        // Tokenize — this only reads llama_model/vocab (thread-safe, no context needed)
        Llama llamaProvider;
        ErrorCode tokenizeResult;

        if(hasImageContent(job->request.messages))
        {
            mtmd_context *mtmdCtx=runtime.getMtmdContext(job->request.model);
            if(!mtmdCtx)
            {
                spdlog::warn("[scheduler:tokenizer] job {} carries images but model '{}' has no projector loaded",
                    job->id, job->request.model);
                job->errorDetail="model '"+job->request.model+"' does not accept image input";
                finishJob(job, ErrorCode::InvalidRequest);
                continue;
            }

            tokenizeResult=llamaProvider.tokenizeMultimodalPrompt(
                llamaModel, mtmdCtx, job->request, *modelInfo,
                job->multimodal, job->tokens, job->formattedPrompt,
                &job->chatPrompt);
        }
        else
        {
            tokenizeResult=llamaProvider.tokenizePrompt(
                llamaModel, job->request, *modelInfo, job->tokens, job->formattedPrompt,
                &job->chatPrompt);
        }

        if(tokenizeResult!=ErrorCode::Success)
        {
            job->errorDetail=llamaProvider.lastErrorDetail();
            finishJob(job, tokenizeResult);
            continue;
        }

        spdlog::debug("[scheduler:tokenizer] job {} tokenized: {} tokens",
            job->id, job->tokens.size());

        if(job->cancelled.load())
        {
            finishJob(job, ErrorCode::Cancelled);
            continue;
        }

        // Move to accelerator queue
        job->stage.store(InferenceStage::WaitingAccelerator);
        AcceleratorQueue &accel=selectAccelerator(*job);

        {
            std::lock_guard<std::mutex> lock(accel.mutex);
            accel.jobs.push_back(job);

            // Update queue positions
            int pos=1;
            if(accel.activeJob) pos++;
            for(auto &queued:accel.jobs)
            {
                queued->queuePosition.store(pos++);
            }
        }
        accel.cv.notify_one();
    }

    spdlog::info("[scheduler:tokenizer] thread exiting");
}

// ── Accelerator Thread ────────────────────────────────────────

void InferenceScheduler::acceleratorLoop(AcceleratorQueue &queue)
{
    spdlog::info("[scheduler:accel:{}] thread started (device='{}')",
        queue.gpuIndex, queue.deviceName);

    while(queue.running.load())
    {
        std::shared_ptr<InferenceJob> job;

        {
            std::unique_lock<std::mutex> lock(queue.mutex);
            queue.cv.wait(lock, [&queue]()
            {
                return !queue.jobs.empty()||!queue.running.load();
            });

            if(!queue.running.load()) break;
            if(queue.jobs.empty()) continue;

            job=queue.jobs.front();
            queue.jobs.pop_front();
            queue.activeJob=job;

            // Update queue positions for remaining jobs
            int pos=1;
            for(auto &queued:queue.jobs)
            {
                queued->queuePosition.store(pos++);
            }
        }

        if(job->cancelled.load())
        {
            finishJob(job, ErrorCode::Cancelled);
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.activeJob=nullptr;
            continue;
        }

        job->stage.store(InferenceStage::Inferring);
        job->inferenceStartTime=std::chrono::steady_clock::now();
        job->queuePosition.store(0);

        spdlog::info("[scheduler:accel:{}] starting inference for job {} (model='{}', {} prompt tokens)",
            queue.gpuIndex, job->id, job->request.model, job->tokens.size());

        // Acquire inference lock and run
        ModelRuntime &runtime=ModelRuntime::instance();

        llama_model *llamaModel=runtime.getLlamaModel(job->request.model);
        llama_context *llamaCtx=runtime.getLlamaContext(job->request.model);

        if(!llamaModel||!llamaCtx)
        {
            spdlog::error("[scheduler:accel:{}] model handles not available for job {}",
                queue.gpuIndex, job->id);
            finishJob(job, ErrorCode::ModelNotLoaded);

            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.activeJob=nullptr;
            continue;
        }

        std::optional<ModelInfo> modelInfo=runtime.getLoadedModelInfo(job->request.model);
        if(!modelInfo)
        {
            finishJob(job, ErrorCode::ModelNotFound);

            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.activeJob=nullptr;
            continue;
        }

        // Lock the inference mutex (one inference at a time per context)
        runtime.beginInference(job->request.model);
        std::lock_guard<std::timed_mutex> inferenceLock(runtime.getInferenceMutex());

        // Check for cancellation after acquiring lock
        if(job->cancelled.load())
        {
            runtime.endInference(job->request.model);
            finishJob(job, ErrorCode::Cancelled);
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.activeJob=nullptr;
            continue;
        }

        // Build stream callback that pushes to channel
        std::function<void(const std::string &)> streamCallback=nullptr;
        if(job->streaming&&job->channel)
        {
            streamCallback=[&job](const std::string &token)
            {
                if(job->channel->isCancelled()) return;
                job->channel->push(token);
                job->completionTokens.fetch_add(1);
            };
        }

        // Abort callback — checks if the job was cancelled by client disconnect
        auto abortCheck=[&job]() -> bool
        {
            return job->cancelled.load();
        };

        // Run inference with pre-tokenized prompt
        int completionTokens=0;

        Llama llamaProvider;
        ErrorCode code=llamaProvider.runInferenceWithTokens(
            llamaModel, llamaCtx, job->request, *modelInfo,
            job->tokens, job->resultText,
            job->promptTokens, completionTokens,
            job->promptTimeMs, job->generationTimeMs,
            streamCallback, abortCheck,
            job->multimodal.valid()?&job->multimodal:nullptr,
            job->chatPrompt.get());

        runtime.endInference(job->request.model);
        job->completionTokens.store(completionTokens);

        if(code!=ErrorCode::Success)
        {
            job->errorDetail=llamaProvider.lastErrorDetail();
        }

        auto endTime=std::chrono::steady_clock::now();
        double totalTimeMs=std::chrono::duration<double, std::milli>(
            endTime-job->inferenceStartTime).count();

        finishJob(job, code);

        // Record telemetry
        if(code==ErrorCode::Success)
        {
            spdlog::info("[scheduler:accel:{}] job {} complete: prompt={} ({:.1f}ms), gen={} ({:.1f}ms), total={:.1f}ms",
                queue.gpuIndex, job->id, job->promptTokens, job->promptTimeMs,
                completionTokens, job->generationTimeMs, totalTimeMs);

            std::optional<LoadedModel> state=runtime.getModelState(job->request.model);

            InferenceStats stats;
            stats.jobId=job->id;
            stats.cancelled=false;
            stats.model=job->request.model;
            stats.variant=state?state->variant:"";
            stats.promptTokens=job->promptTokens;
            stats.completionTokens=completionTokens;
            stats.totalTimeMs=totalTimeMs;
            stats.promptTimeMs=job->promptTimeMs;
            stats.generationTimeMs=job->generationTimeMs;
            stats.latencyMs=std::chrono::duration<double, std::milli>(job->inferenceStartTime-job->submitTime).count();
            stats.tokensPerSecond=totalTimeMs>0.0?(completionTokens/(totalTimeMs/1000.0)):0.0;
            stats.promptTokensPerSecond=job->promptTimeMs>0.0?(job->promptTokens/(job->promptTimeMs/1000.0)):0.0;
            stats.generationTokensPerSecond=job->generationTimeMs>0.0?(completionTokens/(job->generationTimeMs/1000.0)):0.0;
            stats.timestamp=std::chrono::system_clock::now();
            TelemetryCollector::instance().recordInference(stats);
        }
        else if(code==ErrorCode::Cancelled)
        {
            spdlog::info("[scheduler:accel:{}] job {} cancelled after {} gen tokens ({:.1f}ms)",
                queue.gpuIndex, job->id, completionTokens, totalTimeMs);

            std::optional<LoadedModel> state=runtime.getModelState(job->request.model);

            InferenceStats stats;
            stats.jobId=job->id;
            stats.cancelled=true;
            stats.model=job->request.model;
            stats.variant=state?state->variant:"";
            stats.promptTokens=job->promptTokens;
            stats.completionTokens=completionTokens;
            stats.totalTimeMs=totalTimeMs;
            stats.promptTimeMs=job->promptTimeMs;
            stats.generationTimeMs=job->generationTimeMs;
            stats.latencyMs=std::chrono::duration<double, std::milli>(job->inferenceStartTime-job->submitTime).count();
            stats.tokensPerSecond=0.0;
            stats.promptTokensPerSecond=0.0;
            stats.generationTokensPerSecond=0.0;
            stats.timestamp=std::chrono::system_clock::now();
            TelemetryCollector::instance().recordInference(stats);
        }
        else
        {
            spdlog::error("[scheduler:accel:{}] job {} failed (error={})",
                queue.gpuIndex, job->id, static_cast<int>(code));
        }

        {
            std::lock_guard<std::mutex> lock(queue.mutex);
            queue.activeJob=nullptr;
        }
    }

    spdlog::info("[scheduler:accel:{}] thread exiting", queue.gpuIndex);
}

// ── Accelerator Selection ─────────────────────────────────────

AcceleratorQueue &InferenceScheduler::selectAccelerator(const InferenceJob &job)
{
    // For now, select the accelerator with the shortest queue.
    // Future: match by model's GPU affinity / loaded state.
    AcceleratorQueue *best=m_accelerators[0].get();
    int bestDepth=std::numeric_limits<int>::max();

    for(auto &q:m_accelerators)
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        int depth=static_cast<int>(q->jobs.size());
        if(q->activeJob) depth++;
        if(depth<bestDepth)
        {
            bestDepth=depth;
            best=q.get();
        }
    }

    return *best;
}

} // namespace arbiterAI
