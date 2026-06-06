# core/inference/

OGA C-API (ORT-GenAI) wrapper: load a model from a directory path, run the
autoregressive decode loop, yield tokens. Platform-agnostic (the OGA API is plain C;
DirectML is D3D12-based and present on every backend). Filled by task **core-inference**.
