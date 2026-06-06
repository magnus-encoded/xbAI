# core/server/

Hand-rolled HTTP/1.1 + OpenAI-compatible `/v1/chat/completions` + SSE, built on the
`ISocketListener` seam (ADR-0002). No native HTTP lib. Filled by task **core-server**.
