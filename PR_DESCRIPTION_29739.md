# Optimize base64 decoding when AVX2 is available (GH #29739)

## Problem

Base64 **encoding** was accelerated with AVX2 in 2025 (PR #29178), but **decoding** remained scalar-only. On x86_64 systems with AVX2, decode was leaving performance on the table. This PR adds an AVX2-optimized decode path so that both encode and decode can use SIMD when the CPU supports it.

Decode is used by `EVP_DecodeUpdate` (streaming, e.g. BIO) and `EVP_DecodeBlock` (single block); both go through `evp_decodeblock_int()` in `crypto/evp/encode.c`. Callers include PEM, X.509/SPKI, CT, and other base64-consuming code.

## Implementation choices and reasoning

### 1. Mirror the existing encode design

- **Choice:** Add `decode_base64_avx2()` in `crypto/evp/enc_b64_avx2.c`, with the same `OPENSSL_TARGET_AVX2` / runtime `OPENSSL_ia32cap_P` dispatch pattern as the encoder.
- **Reasoning:** Keeps encode and decode consistent, reuses the same build and CPU-detection approach, and avoids a separate module or dispatch layer.

### 2. Support both standard and SRP alphabets in AVX2 (Option A)

- **Choice:** Implement decode LUTs for both `data_ascii2bin` (standard) and `srpdata_ascii2bin` (SRP), and select at runtime via a `use_srp` flag.
- **Reasoning:** Matches the encoder, which already has both alphabets in AVX2; avoids leaving SRP decode on the slow path and keeps behavior symmetric.

### 3. Bulk decode only; last block and padding stay scalar

- **Choice:** AVX2 path decodes only full 32-byte (or 64-byte) chunks with no padding. The last block (and any padding) is always handled by the existing scalar loop in `evp_decodeblock_int()`.
- **Reasoning:** Padding and edge cases (e.g. `=`) are awkward in SIMD and already correct in scalar code. Keeping them there avoids subtle bugs and keeps the AVX2 path simple and fast.

### 4. Integration point

- **Choice:** In `evp_decodeblock_int()`, after trim/strip and `n % 4` checks, compute `avx2_len = (n - 4) & ~31` and call `decode_base64_avx2(use_srp, t, f, avx2_len)` when AVX2 is available. Advance pointers and `n` by the decoded amount, then run the existing scalar loop for the remainder.
- **Reasoning:** Single place to hook in; both `EVP_DecodeUpdate` and `EVP_DecodeBlock` benefit without duplicating logic.

### 5. Optimizations applied

- **64 bytes per loop iteration:** Process two vectors per iteration to cut loop overhead and improve ILP; tail of 32–63 bytes handled with one vector.
- **Load only 4 LUTs per alphabet:** Branch on `use_srp` once and load the active set (shared `decode_0` plus three alphabet-specific LUTs) instead of loading all eight tables every time.
- **Prefetch next input chunk:** `_mm_prefetch(input + 64)` (or +32 in the tail) to hide memory latency, matching the encoder.
- **Shared `decode_0` table:** Standard and SRP share the same LUT for high-byte range 0–31 (identical content), saving 32 bytes of rodata.
- **De-interleave and pack constants at file scope:** `b64d_shuf_sel`, `b64d_dec_shuf`, and `b64d_dec_perm` live in static const arrays so the hot path doesn’t rebuild them from immediates.

### 6. Refactors for clarity and flexibility

- **Alphabet as a table set:** `decode_alphabets[2][3]` holds pointers to the three per-alphabet LUTs; selection is `decode_alphabets[use_srp][0..2]`. One load path, no repeated if/else, and easy to extend to more alphabets later.
- **“Process one vector” helper:** `decode_one_vector(src, dst, use_srp, valid_max)` encapsulates load → decode → validate → reshuffle. The main loop and the 32-byte tail both use it, removing duplicated validate/reshuffle logic.
- **32-byte tail as `if`:** Replaced `while (srclen >= 32)` with `if (srclen >= 32)` because callers only ever pass multiples of 32, so the tail runs at most once. Makes the contract explicit.

### 7. Modern C and OpenSSL conventions

- **`static inline`** for hot helpers (`ascii2bin_avx2`, `dec_reshuffle`, `decode_one_vector`) to encourage inlining and match the rest of `enc_b64_avx2.c`.
- **`static_assert`** on key table sizes (`decode_0`, `b64d_shuf_sel`, `b64d_dec_shuf`) to catch size mistakes at compile time.
- **`restrict`** on `decode_base64_avx2`’s `out` and `src` to document non-aliasing and help the optimizer.
- **`__owur`** on the decode function so callers are warned if they ignore the return value.
- **Const correctness** and **snake_case** retained; **`int`** for lengths to stay consistent with `EVP_DecodeBlock` and the existing API.

## Tests

- **No new regression tests added.** Existing tests already exercise the decode path with inputs large enough to use the AVX2 code on x86_64:
  - **`test/bio_base64_test.c`** uses lengths 192, 768, 1536 bytes (256+ base64 chars per run).
  - **`test/evp_test.c`** Base64 tests use `EVP_DecodeUpdate` / `EVP_DecodeFinal`, which call `evp_decodeblock_int()` and thus the new AVX2 path when built and run on an AVX2-capable host.
- Verification: `make test TESTS='test_evp'` and `make test TESTS=test/bio_base64_test` both pass.

## Documentation

- **CHANGES.md:** Under “Changes between 4.0 and 4.1”, added a bullet: *Added AVX2 optimized base64 decoding on `x86_64` when AVX2 is available. Both standard and SRP alphabets are supported.* with attribution to John Claus.
- **`PERFORMANCE_NOTE_29739.md`** (optional, in tree): Describes how to benchmark decode (e.g. `openssl enc -d -base64` on a large file) and what to expect; includes a placeholder table for x86_64 AVX2 vs scalar results. Can be used in the PR body or as a reference; not part of the committed patch unless added explicitly.
- No man-page or design-doc changes; this is an internal optimization consistent with the existing AVX2 encode documentation.

## Files changed

| File | Change |
|------|--------|
| `crypto/evp/enc_b64_avx2.c` | Add AVX2 decode path: LUTs (shared `decode_0`, `decode_alphabets[2][3]`), `b64d_shuf_sel`, `b64d_dec_shuf`, `b64d_dec_perm`; `ascii2bin_avx2`, `dec_reshuffle`, `decode_one_vector` (all `static inline`); `decode_base64_avx2` (64-byte loop, then 32-byte tail via `if`). `static_assert` on table sizes. |
| `crypto/evp/enc_b64_avx2.h` | Declare `decode_base64_avx2` with `__owur` and `restrict` on `out`/`src`. |
| `crypto/evp/encode.c` | In `evp_decodeblock_int()`, when AVX2 is available and `avx2_len > 0`, call `decode_base64_avx2(use_srp, t, f, avx2_len)` and advance `ret`, `t`, `f`, `n`; scalar loop handles the remainder. |
| `CHANGES.md` | New bullet in 4.0→4.1 section for AVX2 base64 decode (John Claus). |
