// core/queue/InferenceQueue.h
// Single-flight FIFO queue over IInferenceEngine.  Exposes itself AS an
// IInferenceEngine to the server; holds one worker thread that serializes all
// decode calls.  Plain C++20 only -- ZERO WinRT/GDK/Windows.h (ADR-0006 SS6).

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

#include "pal/IInferenceEngine.h"
#include "pal/types.h"

namespace xbai {

class InferenceQueue final : public IInferenceEngine {
public:
    // engine         -- the real inference backend (not owned, must outlive this)
    // max_queue_depth -- max pending requests before rejecting with overload error
    explicit InferenceQueue(IInferenceEngine& engine,
                            size_t max_queue_depth = 4);

    ~InferenceQueue() override;

    // IInferenceEngine ---------------------------------------------------

    // Delegates directly to engine_.load(); queue does not own the load lifecycle.
    bool load(const ModelConfig& model, std::string& error) override;

    // Enqueues the request, blocks until the worker completes it, returns result.
    // If the queue is full, returns immediately with FinishReason::Error /
    // error == "server overloaded" (caller maps this to 503).
    ChatResponse generate(const ChatRequest& request,
                          const TokenCallback& on_token) override;

    // Signal worker thread to exit and join it.  Called automatically by dtor.
    void shutdown();

private:
    struct Job {
        ChatRequest                    request;
        TokenCallback                  on_token;
        std::promise<ChatResponse>     result;
    };

    void worker_loop();

    IInferenceEngine&           engine_;
    const size_t                max_queue_depth_;

    std::mutex                  mu_;
    std::condition_variable     cv_;
    std::deque<Job>             queue_;
    std::atomic<bool>           stop_{ false };
    std::thread                 worker_;
};

}  // namespace xbai
