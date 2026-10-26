#ifndef NV2A_READY_TEXTURE_SIG_H
#define NV2A_READY_TEXTURE_SIG_H

#include <stdint.h>
#include <string.h>

/* Small UI textures can differ in just a few compressed color words. Sampling
 * misses those edits when guest allocations are reused. Cover every byte up
 * to 64 KiB; retain the bounded sampling cost for large scene/video surfaces.
 * This is an internal change detector, not a persistent texture identity. */
static uint64_t ready_tex_sig(const void *data, uint32_t bytes)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t n = bytes / 4, step = bytes <= 65536u ? 1u : 61u, i, word;
    uint64_t h = 1469598103934665603ull;
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
