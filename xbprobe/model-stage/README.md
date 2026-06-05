# model-stage/ — bring your own ONNX weights

The model weights are **not** committed (multi-GB, and you should pull them under
the model's own license). Each dev fetches their own.

This probe was validated against **Phi-3-mini-4k-instruct, INT4 / DirectML**.

## Fetch it (Hugging Face)

The repo + subdir are set in your `.env` (`HF_MODEL_REPO`, `HF_MODEL_SUBDIR`).
Download that file set into `model-stage/phi3-mini-int4/`:

```powershell
# with the huggingface CLI (pip install -U "huggingface_hub[cli]")
huggingface-cli download microsoft/Phi-3-mini-4k-instruct-onnx `
  --include "directml/directml-int4-awq-block-128/*" `
  --local-dir .\_hf
# then move the file set up to model-stage\phi3-mini-int4\
```

The folder must end up with: `model.onnx`, `model.onnx.data`, `genai_config.json`,
`config.json`, the `tokenizer*` files, `special_tokens_map.json`, `added_tokens.json`.

> Any ORT-GenAI ONNX/DirectML export works — point `.env` at it. We do **not**
> convert PyTorch on-device; the export must already be ONNX/DirectML.

## How it gets onto the console
- For the **probe**, the model is optional (the probe can also download on-device,
  see T4 in `FINDINGS.md`).
- To stage manually, SMB-push the folder to `\\XBOX\D$` (creds in `.env`) or let
  the on-device `BackgroundDownloader` path fetch it.
