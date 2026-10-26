#ifndef NV2A_READY_TEXTURE_SIG_H
#define NV2A_READY_TEXTURE_SIG_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Every byte of `bytes`, in four independent 64-bit FNV-style lanes of 8 bytes
 * each, then folded together. The single serial chain this replaced did one
 * dependent multiply per 4 bytes; four lanes over 8 bytes do the same work
 * about eight times faster, and a changed word still always changes its lane.
 * Shared by the texture change detectors (ready_tex_sig, pal_tex_sig). */
static uint64_t dense_sig64(const uint8_t *p, size_t bytes, uint64_t seed)
{
    const uint64_t prime = 1099511628211ull;
    uint64_t h0 = seed, h1 = seed ^ 0x9E3779B97F4A7C15ull;
    uint64_t h2 = seed ^ 0xC2B2AE3D27D4EB4Full, h3 = seed ^ 0x165667B19E3779F9ull;
    size_t i = 0;
    for (; i + 32 <= bytes; i += 32) {
        uint64_t w[4];
        memcpy(w, p + i, sizeof w);
        h0 = (h0 ^ w[0]) * prime;
        h1 = (h1 ^ w[1]) * prime;
        h2 = (h2 ^ w[2]) * prime;
        h3 = (h3 ^ w[3]) * prime;
    }
    for (; i < bytes; ++i)
        h0 = (h0 ^ p[i]) * prime;
    h0 = (h0 ^ (h1 >> 7 | h1 << 57)) * prime;
    h0 = (h0 ^ (h2 >> 13 | h2 << 51)) * prime;
    h0 = (h0 ^ (h3 >> 29 | h3 << 35)) * prime;
    return h0 ^ (h0 >> 32) ^ (uint64_t)bytes;
}

/* RECOMP_FAST_SIG=0: the original single-chain signatures. */
static int sig_fast_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("RECOMP_FAST_SIG");
        on = !(e && *e == '0');
    }
    return on;
}

/* Small UI textures can differ in just a few compressed color words. Sampling
 * misses those edits when guest allocations are reused. Cover every byte up
 * to 64 KiB; retain the bounded sampling cost for large scene/video surfaces.
 * This is an internal change detector, not a persistent texture identity. */
static uint64_t ready_tex_sig(const void *data, uint32_t bytes)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t n = bytes / 4, step = bytes <= 65536u ? 1u : 61u, i, word;
    uint64_t h = 1469598103934665603ull;
    if (step == 1 && sig_fast_on())
        return dense_sig64(p, bytes, h);
    for (i = 0; i < n; i += step) {
        memcpy(&word, p + (size_t)i * 4, sizeof word);
        h ^= word;
        h *= 1099511628211ull;
    }
    if (n) {
        memcpy(&word, p + (size_t)(n - 1) * 4, sizeof word);
        h ^= word;
        h *= 1099511628211ull;
    }
    for (i = n * 4; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

#endif
