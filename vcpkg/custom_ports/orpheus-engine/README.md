# orpheus-engine (chatllm.cpp) — engine-side artifact for local Orpheus TTS

The arbiterAI `Orpheus` provider (`src/arbiterAI/providers/orpheus.{h,cpp}`) is
**engine-agnostic**: it `dlopen`s a self-contained shared library with `RTLD_LOCAL`
and calls a single flat C entry point. This directory is where that engine
library — `liborpheus_engine.so` — is built from
[chatllm.cpp](https://github.com/foldl/chatllm.cpp), which ships a native C++
Orpheus + SNAC decoder (`models/orpheus.cpp`).

## The ABI contract (what the .so must export)

```c
// Synthesize `text` with the Orpheus model at `model_path` (a chatllm GGUF),
// using optional `voice`, writing a 24 kHz WAV to `out_wav_path`.
// Returns 0 on success, non-zero on failure. Must be self-contained: chatllm's
// forked ggml is statically linked inside this .so with hidden visibility so it
// never resolves against the host process's (llama) ggml.
extern "C" int arbiter_orpheus_tts(const char *model_path,
                                   const char *text,
                                   const char *voice,
                                   const char *out_wav_path);
```

The provider finds the library via `ARBITER_ORPHEUS_ENGINE` (full path) or the
loader search path (`liborpheus_engine.so`).

## Why a separate .so (not static link, not the plain C binding)

- chatllm.cpp bundles a **forked ggml** that is API-incompatible with the llama
  port's ggml, so it cannot be stripped-and-shared the way whisper/sd/vibevoice
  are. Isolating it in its own `.so` (built with `-fvisibility=hidden`, ggml
  static inside) and loading with `RTLD_LOCAL` keeps the two ggmls apart.
- chatllm's own C binding (`bindings/libchatllm.h`) has **no audio output** — its
  `print_type` enum is text-only and the PCM is an internal C++ return from
  `ConditionalGeneration::speech_synthesis(...)`. So the shim must reach that
  C++ path, not the flat text binding.

## Implementing the shim (remaining chatllm-side work)

`arbiter_orpheus_tts` wraps chatllm's C++ so it must be developed against a real
chatllm build (its C++ classes are not a stable public API). Concretely:

1. Build chatllm as usual and locate the pipeline entry that loads a model and
   runs generation (see `bindings/libchatllm.cpp` for how a model is created and
   driven; the TTS path invokes `speech_synthesis` internally).
2. Add a small C++ translation unit exporting `arbiter_orpheus_tts` that: loads
   the Orpheus model, formats the Orpheus prompt from `text`, runs generation to
   obtain the SNAC-coded token stream, calls `speech_synthesis(...)` to get the
   `std::vector<int16_t>` PCM + sample rate, and writes a canonical 16-bit mono
   WAV to `out_wav_path`. (A ~40-line WAV writer; the provider only reads bytes
   back, so any valid WAV works.)
3. Link that TU with chatllm + its ggml into `liborpheus_engine.so`:
   `-shared -fvisibility=hidden` and keep only `arbiter_orpheus_tts` exported.

### Alternative shim (no C++ coupling)

If chatllm's `main` CLI can save TTS audio to a file, the shim can instead be a
trivial `fork`/`exec` of the chatllm binary with the model/text/output args —
zero linking against chatllm internals. This needs the CLI's audio-save option
confirmed against the build (not documented upstream at time of writing).

## Status — implemented and build-verified

- arbiterAI `Orpheus` provider + dlopen contract: built + linked.
- `shim/orpheus_engine.cpp`: the shim (`arbiter_orpheus_tts`) — compiles against
  chatllm's real `Pipeline::speech_synthesis` API and provides `log_internal`
  (normally in `main.cpp`, which the engine excludes).
- `liborpheus_engine.so`: **builds** via `portfile.cmake` (chatllm core + all
  models + static PIC ggml + shim, ~single 140 MB self-contained file).
  Verified: `ldd` shows no ggml `.so` deps; `nm -D` exports **only**
  `arbiter_orpheus_tts` (0 ggml symbols leak); `dlopen(RTLD_NOW|RTLD_LOCAL)` +
  `dlsym` succeed; and arbiterAI's provider loads it, calls the entry point, and
  error-handles a bad model cleanly (returns non-zero → `GenerationError`, no
  crash).
- **Not yet run:** actual synthesis with a real Orpheus GGUF (needs the model
  file — no network for weights in the dev container), same as the other local
  engines' end-to-end tests. Point `ARBITER_ORPHEUS_ENGINE` at the built `.so`
  and `ORPHEUS_MODEL_PATH` at an Orpheus GGUF to run `OrpheusProviderTest`.

## Build

`vcpkg` feature `orpheus` (see `vcpkg.json`) runs `portfile.cmake`. Or build
directly: clone chatllm.cpp `--recursive`, drop `shim/orpheus_engine.{cpp,map}`
in, append the `orpheus_engine` target (see the portfile), and
`cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DGGML_VULKAN=OFF`
`--target orpheus_engine`.
