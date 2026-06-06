// core/queue/InferenceQueue.cpp

#include "queue/InferenceQueue.h"

#include <stdexcept>

namespace xbai {

InferenceQueue::InferenceQueue(IInferenceEngine& engine,
                               size_t max_queue_depth)
    : engine_(engine)
    , max_queue_depth_(max_queue_depth)
    , worker_(&InferenceQueue::worker_loop, this)
{}

InferenceQueue::~InferenceQueue()
{
    shutdown();
}

bool InferenceQueue::load(const ModelConfig& model, std::string& error)
{
    return engine_.load(model, error);
}

ChatResponse InferenceQueue::generate(const ChatRequest& request,
                                      const TokenCallback& on_token)
{
    std::promise<ChatResponse> promise;
    std::future<ChatResponse>  future = promise.get_future();

    {
        std::lock_guard<std::mutex> lock(mu_);

        if (queue_.size() >= max_queue_depth_) {
            ChatResponse err;
            err.finish = FinishReason::Error;
            err.error  = "server overloaded";
            return err;
        }

        Job job;
        job.request  = request;
        job.on_token = on_token;
        job.result   = std::move(promise);
        queue_.push_back(std::move(job));
    }

    cv_.notify_one();
    return future.get();  // block caller until worker delivers the result
}

void InferenceQueue::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_.store(true, std::memory_order_relaxed);
    }
    cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void InferenceQueue::worker_loop()
{
    for (;;) {
        Job job;

        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_relaxed) || !queue_.empty();
            });

            if (stop_.load(std::memory_order_relaxed) && queue_.empty()) {
                return;
            }

            job = std::move(queue_.front());
            queue_.pop_front();
        }

        // Run the decode synchronously on this thread; on_token callbacks fire here.
        ChatResponse result;
        try {
            result = engine_.generate(job.request, job.on_token);
        } catch (...) {
            result.finish = FinishReason::Error;
            result.error  = "inference engine threw an exception";
        }

        job.result.set_value(std::move(result));
    }
}

}  // namespace xbai
