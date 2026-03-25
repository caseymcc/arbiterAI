# API Interface Analysis: ArbiterAI Server

An analysis of major LLM provider API interfaces, how they compare to the OpenAI-compatible API already served by ArbiterAI, and the advantages and disadvantages of supporting each as an additional server interface.

## Table of Contents

1. [Current State](#1-current-state)
2. [API Comparison](#2-api-comparison)
3. [Anthropic Messages API](#3-anthropic-messages-api)
4. [OpenAI Responses API](#4-openai-responses-api)
5. [Ollama API](#5-ollama-api)
6. [Cohere API](#6-cohere-api)
7. [Google Gemini API](#7-google-gemini-api)
8. [Summary Matrix](#8-summary-matrix)
9. [Recommendation](#9-recommendation)

---

## 1. Current State

ArbiterAI server currently exposes a single HTTP API interface modeled after the **OpenAI Chat Completions API**:

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/v1/chat/completions` | POST | Chat completion (streaming + non-streaming) |
| `/v1/models` | GET | List available models |
| `/v1/models/:id` | GET | Get individual model info |
| `/v1/embeddings` | POST | Generate embeddings |
| `/health` | GET | Health check |

This interface was chosen because it is the de facto standard that most LLM tooling (LiteLLM, LangChain, OpenAI Python SDK, etc.) speaks natively. The server also exposes proprietary `/api/` endpoints for model management, telemetry, and the dashboard — those are not part of this analysis.

### What the ArbiterAI library already abstracts

The ArbiterAI C++ library internally supports multiple providers (OpenAI, Anthropic, DeepSeek, OpenRouter, Llama.cpp, Mock), each with their own wire format. The server sits on top of this library and translates HTTP requests into `CompletionRequest` structs, which the library routes to the correct provider.

Adding a new server-side API interface means adding a new set of HTTP route handlers that translate a different wire format into the same `CompletionRequest` / `CompletionResponse` structs. The provider layer is unaffected.

---

## 2. API Comparison

### Structural Differences at a Glance

| Feature | OpenAI | Anthropic | Ollama | Cohere | Gemini |
|---------|--------|-----------|--------|--------|--------|
| **Endpoint** | `/v1/chat/completions` | `/v1/messages` | `/api/chat` | `/v2/chat` | `/v1beta/models/{m}:generateContent` |
| **Auth** | `Authorization: Bearer` | `x-api-key` | None (local) | `Authorization: Bearer` | `x-goog-api-key` or OAuth |
| **System prompt** | `role: "system"` message | Top-level `system` field | `role: "system"` message | `preamble` field | `systemInstruction` field |
| **Message content** | String or content parts array | Content blocks array | String | String | Content parts array |
| **Tool calling** | `tools[]` + `tool_choice` | `tools[]` + `tool_choice` | `tools[]` | `tools[]` + `tool_results[]` | `tools[]` + `toolConfig` |
| **Streaming** | SSE `data:` lines | SSE `event:` + `data:` lines | NDJSON (newline-delimited JSON) | SSE `data:` lines | SSE `data:` lines |
| **Usage/tokens** | In response `usage{}` | In response `usage{}` | In response metadata | In response `meta.tokens` | In response `usageMetadata{}` |
| **Token field names** | `prompt_tokens`, `completion_tokens` | `input_tokens`, `output_tokens` | `prompt_eval_count`, `eval_count` | `input_tokens`, `output_tokens` | `promptTokenCount`, `candidatesTokenCount` |
| **Stop reason** | `finish_reason` | `stop_reason` | `done_reason` | `finish_reason` | `finishReason` |
| **Multimodal** | Content parts with `image_url` | Content blocks with `image` | `images` field (base64) | Not in chat | Content parts with `inlineData` |

---

## 3. Anthropic Messages API

**Endpoint:** `POST /v1/messages`

### Key Differences from OpenAI

1. **System prompt is a top-level field**, not a message:
   ```json
   {
     "model": "claude-sonnet-4-20250514",
     "system": "You are a helpful assistant.",
     "messages": [{"role": "user", "content": "Hello"}]
   }
   ```

2. **Content is an array of typed blocks**, not a plain string:
   ```json
   {"role": "assistant", "content": [{"type": "text", "text": "Hello!"}]}
   ```

3. **Tool use returns `tool_use` blocks** in the content array (not a separate `tool_calls` field):
   ```json
   {
     "content": [
       {"type": "tool_use", "id": "call_123", "name": "get_weather", "input": {"city": "Boston"}}
     ]
   }
   ```

4. **Auth via `x-api-key` header** (not `Authorization: Bearer`). Also requires `anthropic-version` header.

5. **Streaming uses typed SSE events** (`message_start`, `content_block_start`, `content_block_delta`, `message_delta`, `message_stop`) rather than a uniform `data:` JSON line.

6. **Response is a `Message` object** (not a `ChatCompletion`): `id`, `type: "message"`, `role`, `content[]`, `stop_reason`, `usage`.

7. **`max_tokens` is required** (OpenAI defaults it).

8. **Extended thinking** (chain-of-thought) is a first-class feature with `thinking` blocks in the response.

9. **Prompt caching** is exposed via `cache_control` on content blocks — a unique Anthropic feature.

### Advantages of Supporting

| Advantage | Detail |
|-----------|--------|
| **Native Claude SDK users** | Applications built with the Anthropic Python/TypeScript SDK could connect directly without needing a translation layer like LiteLLM. |
| **Thinking block passthrough** | Extended thinking output could be streamed to clients that understand `thinking` blocks — something the OpenAI format cannot represent. |
| **Prompt caching control** | Clients could pass `cache_control` hints that map to ArbiterAI's own `CacheManager`, enabling smarter caching at the API level. |
| **Content block richness** | Anthropic's typed content blocks (`text`, `image`, `tool_use`, `tool_result`) are more expressive than OpenAI's flat `content` string. |
| **Market positioning** | Shows ArbiterAI as a true multi-API gateway, not just another OpenAI-compatible proxy. |

### Disadvantages of Supporting

| Disadvantage | Detail |
|--------------|--------|
| **Significant format translation** | System prompt extraction, content block ↔ string conversion, tool_use blocks ↔ tool_calls, different streaming event types — all require non-trivial mapping. |
| **Maintenance burden** | Anthropic's API evolves rapidly (beta features, version headers). Tracking `anthropic-version` and new block types adds ongoing work. |
| **Incomplete mapping** | Some Anthropic features (prompt caching `cache_control`, `thinking` blocks, content citations) have no equivalent in ArbiterAI's internal structs and would be silently dropped or require struct changes. |
| **Low demand for local models** | Anthropic's SDK is designed for their cloud API. Users running local models via ArbiterAI are unlikely to use the Anthropic SDK to connect — they'd use the OpenAI SDK or LiteLLM. |
| **Two streaming parsers** | Anthropic's event-based SSE (`event: content_block_delta\ndata: {...}`) is structurally different from OpenAI's `data: {...}\ndata: [DONE]`. Supporting both means two streaming code paths. |

### Effort Estimate

**Medium–Large.** The message format translation is the bulk of the work. Streaming requires a separate code path. Content blocks and tool_use mapping need careful handling.

---

## 4. OpenAI Responses API

**Endpoint:** `POST /v1/responses`

OpenAI introduced the Responses API as a successor to Chat Completions. It adds built-in tool execution, multi-turn agent flows, and background processing.

### Key Differences from Chat Completions

1. **Input is a flat list of items** (not strictly `messages`):
   ```json
   {
     "model": "gpt-4.1",
     "input": "Tell me a joke"
   }
   ```
   Or an array of input items with types (`message`, `item_reference`, etc.).

2. **Built-in tools** like `web_search`, `file_search`, `code_interpreter` are first-class — the server is expected to execute them, not just return tool calls.

3. **Stateful by default** — responses can reference previous response IDs, forming a conversation chain without the client re-sending history.

4. **Background mode** — requests can be queued and polled later.

5. **Richer output items** — the response contains `output[]` with typed items (`message`, `function_call_output`, `reasoning`, etc.).

### Advantages of Supporting

| Advantage | Detail |
|-----------|--------|
| **Future-proofing** | OpenAI is positioning Responses as the successor to Chat Completions. Early adoption signals forward compatibility. |
| **Agent frameworks** | Some newer agent frameworks may target the Responses API natively. |
| **Stateful conversations** | The `previous_response_id` pattern aligns with ArbiterAI's `ChatClient` session model, which already tracks history. |

### Disadvantages of Supporting

| Disadvantage | Detail |
|--------------|--------|
| **Server-side tool execution** | The Responses API expects the server to execute tools like `web_search` and `code_interpreter`. ArbiterAI doesn't have these capabilities and would need to either stub them or reject them. |
| **Massive scope** | The API is significantly more complex than Chat Completions — stateful sessions, background jobs, input item types, output item types, annotations, etc. |
| **Rapidly evolving** | Still in active development at OpenAI. The spec changes frequently. |
| **Minimal ecosystem demand** | Most third-party tools (LiteLLM, LangChain, vLLM, Ollama) still target Chat Completions. The Responses API is OpenAI-specific. |
| **No clear mapping** | ArbiterAI's `CompletionRequest`/`CompletionResponse` is modeled after Chat Completions. The Responses API would need different internal representations. |

### Effort Estimate

**Very Large.** Fundamentally different paradigm. Would require new internal abstractions for stateful sessions, background job management, and possibly built-in tool execution. Not recommended at this time.

---

## 5. Ollama API

**Endpoint:** `POST /api/chat`

Ollama is the most popular local LLM runner. Its API is simple and optimized for local inference.

### Key Differences from OpenAI

1. **NDJSON streaming** (one JSON object per line) instead of SSE:
   ```json
   {"model":"llama3","message":{"role":"assistant","content":"Hello"},"done":false}
   {"model":"llama3","message":{"role":"assistant","content":"!"},"done":true,"total_duration":1234567890}
   ```

2. **Simpler model management** — `/api/tags` to list, `/api/pull` to download, `/api/show` for details.

3. **Different token field names**: `prompt_eval_count`, `eval_count`, `prompt_eval_duration`, `eval_duration`.

4. **Image support** via `images` field (base64 array) on messages, not content parts.

5. **`/api/generate`** endpoint for raw text completion (not chat-formatted).

6. **`keep_alive`** parameter controls how long models stay in memory — similar to ArbiterAI's pin/ready concept.

7. **`format`** parameter for JSON mode (`"json"`).

### Advantages of Supporting

| Advantage | Detail |
|-----------|--------|
| **Drop-in Ollama replacement** | Many tools and UIs (Open WebUI, Continue.dev, Aider, etc.) support Ollama as a backend. ArbiterAI could be a drop-in replacement with richer features (multi-GPU, cost tracking, model fit). |
| **Large user base** | Ollama has a massive user base in the local LLM community. Speaking their API captures that audience immediately. |
| **Simple format** | The Ollama API is much simpler than OpenAI's — fewer edge cases, no `system_fingerprint`, no `n` parameter, no `logprobs`. Translation is straightforward. |
| **Model management alignment** | Ollama's `/api/pull`, `/api/tags`, `/api/show`, `/api/delete` map well to ArbiterAI's existing `/api/models/*` endpoints. A thin adapter layer could expose them at Ollama-compatible paths. |
| **`keep_alive` → pin** | Ollama's `keep_alive` parameter ("keep model loaded for X minutes") maps naturally to ArbiterAI's pin/ready model lifecycle. |
| **Local-first audience** | Users choosing Ollama are already local-model users — exactly ArbiterAI's target for llama.cpp inference. |

### Disadvantages of Supporting

| Disadvantage | Detail |
|--------------|--------|
| **NDJSON streaming** | Ollama uses newline-delimited JSON, not SSE. This requires a different streaming code path in cpp-httplib (raw chunked response vs SSE). |
| **Duplicate model management** | Ollama's `/api/pull` (download by HuggingFace tag) has different semantics than ArbiterAI's config-driven download. Mapping between them may confuse users. |
| **Library/Modelfile concept** | Ollama has `Modelfile` (like Dockerfile for models) and a library of pre-configured models. ArbiterAI has a different model config system. Full compatibility would require parsing Modelfiles. |
| **Feature gaps** | Ollama supports raw `/api/generate` (non-chat), `/api/create` (custom models from Modelfile), `/api/copy`, `/api/push` — features that have no ArbiterAI equivalent. |
| **Embedding endpoint difference** | Ollama uses `/api/embed` (not `/v1/embeddings`), with different request/response shapes. |

### Effort Estimate

**Small–Medium.** The chat endpoint is simple to translate. NDJSON streaming is the main implementation difference. Model management endpoints are a nice-to-have but can be deferred.

---

## 6. Cohere API

**Endpoint:** `POST /v2/chat`

### Key Differences from OpenAI

1. **System prompt via `preamble` field** (v1) or standard `system` role (v2).

2. **Tool results are a separate field** (`tool_results[]`) rather than role-based messages.

3. **Connectors** — first-class support for RAG connectors that the server is expected to manage.

4. **Citation generation** — responses include `citations[]` with document references.

5. **Grounded generation** — the API can return `documents[]` alongside the response.

### Advantages of Supporting

| Advantage | Detail |
|-----------|--------|
| **RAG-native features** | If ArbiterAI adds RAG/retrieval, Cohere's citation and connector model is well-designed. |
| **V2 is OpenAI-like** | Cohere's v2 API moved closer to OpenAI's format. Translation is simpler than v1. |

### Disadvantages of Supporting

| Disadvantage | Detail |
|--------------|--------|
| **Small ecosystem** | Few tools target the Cohere API directly. Most use the OpenAI-compatible layer Cohere already provides. |
| **Connectors and RAG** | Core Cohere differentiators (connectors, grounded generation) require server-side infrastructure ArbiterAI doesn't have. |
| **Niche audience** | Cohere users tend to use Cohere's own SDK. There's minimal demand to access local models via the Cohere wire format. |
| **Two API versions** | Cohere has both v1 and v2 APIs with different semantics. Supporting both doubles the work. |

### Effort Estimate

**Medium.** The v2 chat format is relatively close to OpenAI. But the value-add is low given the small user base.

---

## 7. Google Gemini API

**Endpoint:** `POST /v1beta/models/{model}:generateContent`

### Key Differences from OpenAI

1. **Model in URL path**, not request body:
   ```
   POST /v1beta/models/gemini-2.0-flash:generateContent
   ```

2. **Content parts with inline data** for multimodal:
   ```json
   {"parts": [{"text": "Describe"}, {"inlineData": {"mimeType": "image/png", "data": "..."}}]}
   ```

3. **`generationConfig`** instead of top-level parameters:
   ```json
   {"generationConfig": {"temperature": 0.7, "maxOutputTokens": 1024}}
   ```

4. **Safety settings** are a required/common field — `safetySettings[]` with harm categories and thresholds.

5. **Streaming via `streamGenerateContent`** — a different endpoint path, not a request field.

6. **Token usage** uses `usageMetadata` with `promptTokenCount`, `candidatesTokenCount`.

### Advantages of Supporting

| Advantage | Detail |
|-----------|--------|
| **Large user base** | Gemini is widely used, especially in the Google Cloud ecosystem. |
| **Multimodal native** | Gemini's content parts model handles text, images, video, and audio natively. |
| **Google AI Studio users** | Developers prototyping in Google AI Studio could point at an ArbiterAI server for local testing. |

### Disadvantages of Supporting

| Disadvantage | Detail |
|--------------|--------|
| **Model-in-URL routing** | The model is in the URL path, which requires different routing patterns than the current `/v1/chat/completions` approach. |
| **Deeply different structure** | `generationConfig` wrapping, `safetySettings`, `candidatesTokenCount` — the format diverges significantly from OpenAI. |
| **Separate streaming endpoint** | Streaming is a different URL (`streamGenerateContent`), not a request body flag. |
| **Google-specific features** | Safety settings, grounding with Google Search, code execution — all require Google-specific infrastructure. |
| **Auth complexity** | Supports both API key (`x-goog-api-key`) and OAuth2. OAuth2 is complex to implement for a local server. |
| **Low local-model demand** | Like Anthropic, Gemini SDK users are typically targeting Google's cloud. Few would use it to connect to a local server. |

### Effort Estimate

**Medium–Large.** The structural differences are significant. The model-in-URL pattern requires route changes. Safety settings and generation config wrapping add translation complexity.

---

## 8. Summary Matrix

| API | Translation Effort | User Demand for Local LLMs | Ecosystem Tool Coverage | Unique Value-Add | Recommendation |
|-----|-------------------|---------------------------|------------------------|-----------------|----------------|
| **OpenAI Chat Completions** | — (already done) | High | Very High (LiteLLM, LangChain, etc.) | De facto standard | ✅ Already supported |
| **Ollama** | Small–Medium | **Very High** | High (Open WebUI, Continue, Aider) | Drop-in replacement for popular local runner | ✅ **Recommended next** |
| **Anthropic Messages** | Medium–Large | Low | Medium (Anthropic SDK only) | Thinking blocks, content blocks | ⚠️ Consider later |
| **OpenAI Responses** | Very Large | Low | Low (OpenAI SDK only, new) | Stateful agents | ❌ Not recommended yet |
| **Cohere** | Medium | Very Low | Low | RAG/citations | ❌ Not recommended |
| **Google Gemini** | Medium–Large | Low | Medium | Multimodal, Google ecosystem | ❌ Not recommended |

---

## 9. Recommendation

### Priority 1: Ollama API Compatibility

The Ollama API should be the next interface added to the ArbiterAI server for the following reasons:

1. **Audience alignment** — Ollama users are local-model users. ArbiterAI's llama.cpp backend serves the same audience with additional features (multi-GPU, cost tracking, model fit calculation, telemetry).

2. **Ecosystem capture** — Dozens of popular tools (Open WebUI, Continue.dev, Aider, LM Studio chat UIs, Obsidian plugins) support Ollama as a backend. A compatible interface makes ArbiterAI a drop-in replacement.

3. **Low effort** — The Ollama chat format is simpler than OpenAI's. The main work is NDJSON streaming output and mapping a few field names. The model management endpoints (`/api/tags`, `/api/show`) map naturally to existing ArbiterAI functionality.

4. **Feature superiority story** — "Use ArbiterAI instead of Ollama and get multi-GPU support, hardware-aware model selection, cost tracking, and cloud provider fallback — all with the same API."

### Priority 2: Anthropic Messages API (Later)

Worth considering once ArbiterAI has a larger user base and if there's demand from users who want to use the Anthropic SDK to connect to local models or to cloud Claude through the same server.

### Not Recommended Now

- **OpenAI Responses API** — Too new, too complex, requires server-side tool execution. Wait for the ecosystem to settle.
- **Cohere API** — Too niche for the effort.
- **Google Gemini API** — Structural differences are large and the local-model demand is low.

### Architecture Note

To support multiple API interfaces cleanly, consider:

1. **Interface-specific route files** — `routes_openai.cpp`, `routes_ollama.cpp`, `routes_anthropic.cpp` — each translating their wire format to/from the shared `CompletionRequest`/`CompletionResponse` structs.

2. **Shared helpers** — Keep `generateId()`, `errorJson()`, model/hardware JSON serializers in a `routes_common.cpp`.

3. **Interface toggle** — Let the server configuration enable/disable interfaces via command-line flags (`--enable-ollama`, `--enable-anthropic`) so users only expose the APIs they need.

---

## Further Reading

- [LiteLLM Compatibility Task](tasks/litellm_compatibility.md) — Details on the OpenAI-compatible API implementation
- [Developer Guide](developer.md) — ArbiterAI architecture and API reference
- [Local Model Management](tasks/local_model_management.md) — Standalone server design and model management
