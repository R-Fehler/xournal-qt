# ONNX Runtime C API headers (vendored)

The headers of ONNX Runtime's C API, for the handwriting search's recogniser (`qt/src/hwr/TrocrRecognizer.cpp`).
Only the headers: the runtime itself (`libonnxruntime.so.1`) is loaded when the handwriting search is switched on
(`dlopen`), so the app builds and runs without it (see `qt/docs/handwriting-search.md`).

- Upstream: https://github.com/microsoft/onnxruntime, `include/onnxruntime/core/session/`
- Version: 1.30.0 (C API version 30; the app asks for version 16, so runtimes from 1.16 on work).
- Source: conda-forge `onnxruntime-cpp-1.30.0-h987d1f6_2_cpu.conda`, sha256
  `858a47f15fe30c70d285696212600dc149034afdfd58f8140336e1a55b945e4c` (GitHub downloads were blocked where this was
  vendored).
- Files: `onnxruntime_c_api.h`, `onnxruntime_ep_c_api.h` (included by the former) and `onnxruntime_error_code.h`,
  copied unchanged.
- License: MIT (see `LICENSE`).
