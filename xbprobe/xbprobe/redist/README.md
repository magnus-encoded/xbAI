# redist/ — native runtime DLLs (not committed)

These DLLs are bundled into the appx so the probe can `LoadLibrary` them by full
path inside the UWP app container. They are restored from NuGet + System32, not
committed. Run from the repo:

```powershell
.\xbprobe\Get-Dependencies.ps1
```

Produces:
- `DirectML.dll`            — Microsoft.AI.DirectML 1.15.2
- `onnxruntime.dll`         — Microsoft.ML.OnnxRuntime.DirectML 1.20.1
- `onnxruntime-genai.dll`   — Microsoft.ML.OnnxRuntimeGenAI.DirectML 0.6.0
- `vcruntime140.dll`, `vcruntime140_1.dll`, `msvcp140.dll`, `msvcp140_1.dll`
  — desktop VC++ runtime (from System32), bundled because the AppContainer's
  default CRT differs from the one ORT-GenAI links against.
