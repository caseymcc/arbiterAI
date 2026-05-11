#!/bin/bash
# context_stress_test.sh — Progressively fill context on ai-lab to find the real limit
#
# The model (Qwen3.5-27B:Q4_K_M) is loaded with 248832 context on the MI50 32GB.
# This script sends increasingly large prompts and observes when llama.cpp errors.
#
# Strategy: Use a binary search approach. Start with a known-good token count,
# then double until failure, then binary search between last-good and first-bad.

set -euo pipefail

SERVER="http://192.168.2.101:8081"
MODEL="Qwen3.5-27B"
RESULTS_FILE="/tmp/context_stress_results.txt"

echo "Context Stress Test - $(date)" | tee "$RESULTS_FILE"
echo "Server: $SERVER" | tee -a "$RESULTS_FILE"
echo "Model: $MODEL (Q4_K_M)" | tee -a "$RESULTS_FILE"
echo "Configured context: 248832" | tee -a "$RESULTS_FILE"
echo "========================================" | tee -a "$RESULTS_FILE"

# Generate a repeating text block to fill context
# ~4 chars per token for English text is a rough estimate
# We'll use a simple repeating pattern
generate_payload() {
    local target_tokens=$1
    # Each word "hello " is roughly 1-2 tokens; use ~3.5 chars/token estimate
    local char_count=$((target_tokens * 4))
    
    # Generate repeating text
    local text=""
    local block="The quick brown fox jumps over the lazy dog. This is a test of context window capacity. "
    local block_len=${#block}
    local repeats=$((char_count / block_len + 1))
    
    # Use python for efficiency with large strings
    python3 -c "
import json, sys

target_chars = $char_count
block = 'The quick brown fox jumps over the lazy dog. This is a test of context window capacity. '
text = (block * ($repeats))[:target_chars]

payload = {
    'model': '$MODEL',
    'messages': [
        {'role': 'system', 'content': 'You are a helpful assistant. Respond with exactly one word: OK'},
        {'role': 'user', 'content': text}
    ],
    'max_tokens': 5,
    'temperature': 0.0
}

json.dump(payload, sys.stdout)
"
}

# Send a request and check if it succeeds
test_context() {
    local target_tokens=$1
    local start_time=$(date +%s%N)
    
    echo -n "  Testing ~${target_tokens} tokens... " | tee -a "$RESULTS_FILE"
    
    # Generate payload and send
    local response
    local http_code
    
    # Write payload to temp file to handle large sizes
    generate_payload "$target_tokens" > /tmp/context_test_payload.json
    local payload_size=$(wc -c < /tmp/context_test_payload.json)
    echo -n "(payload: ${payload_size} bytes) " | tee -a "$RESULTS_FILE"
    
    # Send request with extended timeout (large context = slow)
    response=$(curl -sf -w "\n%{http_code}" \
        --max-time 300 \
        -X POST "${SERVER}/v1/chat/completions" \
        -H "Content-Type: application/json" \
        -d @/tmp/context_test_payload.json 2>&1) || {
        local exit_code=$?
        echo "CURL_ERROR (exit=$exit_code)" | tee -a "$RESULTS_FILE"
        echo "  Response: $(echo "$response" | tail -5)" | tee -a "$RESULTS_FILE"
        return 1
    }
    
    http_code=$(echo "$response" | tail -1)
    local body=$(echo "$response" | sed '$d')
    
    local end_time=$(date +%s%N)
    local elapsed_ms=$(( (end_time - start_time) / 1000000 ))
    
    if [ "$http_code" = "200" ]; then
        local prompt_tokens=$(echo "$body" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('usage',{}).get('prompt_tokens','?'))" 2>/dev/null || echo "?")
        echo "OK (HTTP 200, prompt_tokens=${prompt_tokens}, ${elapsed_ms}ms)" | tee -a "$RESULTS_FILE"
        return 0
    else
        local error_msg=$(echo "$body" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('error',{}).get('message','unknown')[:200])" 2>/dev/null || echo "$body" | head -c 200)
        echo "FAILED (HTTP ${http_code}, ${elapsed_ms}ms)" | tee -a "$RESULTS_FILE"
        echo "  Error: ${error_msg}" | tee -a "$RESULTS_FILE"
        return 1
    fi
}

# Phase 1: Exponential probing - find the ballpark where it fails
echo "" | tee -a "$RESULTS_FILE"
echo "Phase 1: Exponential probing" | tee -a "$RESULTS_FILE"
echo "----------------------------------------" | tee -a "$RESULTS_FILE"

# Start with small amounts and increase
TOKEN_SIZES=(1000 4000 8000 16000 32000 64000 96000 128000 160000 192000 224000 240000 248000)

last_good=0
first_bad=0

for tokens in "${TOKEN_SIZES[@]}"; do
    if test_context "$tokens"; then
        last_good=$tokens
    else
        first_bad=$tokens
        break
    fi
done

if [ "$first_bad" -eq 0 ]; then
    echo "" | tee -a "$RESULTS_FILE"
    echo "All tests passed! Model handled up to ~${last_good} tokens." | tee -a "$RESULTS_FILE"
    echo "The full 248832 context appears usable." | tee -a "$RESULTS_FILE"
else
    # Phase 2: Binary search between last_good and first_bad
    echo "" | tee -a "$RESULTS_FILE"
    echo "Phase 2: Binary search between ${last_good} and ${first_bad}" | tee -a "$RESULTS_FILE"
    echo "----------------------------------------" | tee -a "$RESULTS_FILE"
    
    low=$last_good
    high=$first_bad
    
    while [ $((high - low)) -gt 2000 ]; do
        mid=$(( (low + high) / 2 ))
        if test_context "$mid"; then
            low=$mid
        else
            high=$mid
        fi
    done
    
    echo "" | tee -a "$RESULTS_FILE"
    echo "========================================" | tee -a "$RESULTS_FILE"
    echo "RESULT: Maximum usable context is approximately ${low}-${high} tokens" | tee -a "$RESULTS_FILE"
    echo "  Last successful: ~${low} tokens" | tee -a "$RESULTS_FILE"
    echo "  First failure:   ~${high} tokens" | tee -a "$RESULTS_FILE"
    echo "  Configured max:  248832 tokens" | tee -a "$RESULTS_FILE"
    echo "  Utilization:     $(python3 -c "print(f'{${low}/248832*100:.1f}%')")" | tee -a "$RESULTS_FILE"
fi

echo "" | tee -a "$RESULTS_FILE"
echo "Full results saved to: $RESULTS_FILE" | tee -a "$RESULTS_FILE"
echo "Done - $(date)" | tee -a "$RESULTS_FILE"
