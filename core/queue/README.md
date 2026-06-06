# core/queue/

Single-flight FIFO queue + request lifecycle; owns the decode worker thread (ADR-0006 §2).
Concurrent requests accepted, generations run one at a time; bounded queue → 429/503 on
overflow (ADR-0002). Filled by task **core-queue**.
