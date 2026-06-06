# core/model/

Model-directory layout + `genai_config.json` handling + model-id → path resolution.
Canonical root is `<storage-root>/models/<model-id>/` (a small `.onnx` graph + external
`.data` weight files, kept together). Reads the same path the downloader writes. Filled by
task **core-model**.
