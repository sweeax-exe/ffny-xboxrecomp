/* Synthetic bytes only: localized edits in a reused small texture must not
 * retain the cache's old signature. No device, game image or texture assets. */
#include "nv2a/ready_texture_sig.h"
#include <assert.h>
#include <stdio.h>

static uint64_t old_sparse(const uint32_t *words, uint32_t count)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < count; i += 61) {
        h ^= words[i]; h *= 1099511628211ull;
    }
    if (count) { h ^= words[count - 1]; h *= 1099511628211ull; }
    return h;
}

int main(void)
{
    static uint32_t original[16384], changed[16384];
    static const uint32_t lengths[] = {1, 2, 3, 4, 5, 7, 31, 65, 4096, 32768, 65536};
    uint32_t i, k;
    uint64_t baseline;
    memset(original, 0x5A, sizeof original);
    memcpy(changed, original, sizeof changed);
    changed[100] ^= 0x00FF0000u;
    assert(old_sparse(original, 8192) == old_sparse(changed, 8192));
    assert(ready_tex_sig(original, 32768) != ready_tex_sig(changed, 32768));
    changed[100] = original[100];
    baseline = ready_tex_sig(original, 32768);
    assert(baseline == ready_tex_sig(changed, 32768));
    /* Exercise every word position, including the old stride's blind spots. */
    for (i = 0; i < 8192; ++i) {
        changed[i] ^= 1;
        assert(baseline != ready_tex_sig(changed, 32768));
        changed[i] = original[i];
    }
    for (k = 0; k < sizeof lengths / sizeof lengths[0]; ++k) {
        uint32_t n = lengths[k];
        uint8_t *bytes = (uint8_t *)changed;
        baseline = ready_tex_sig(changed, n);
        bytes[n - 1] ^= 1;
        assert(baseline != ready_tex_sig(changed, n));
        bytes[n - 1] ^= 1;
    }
    /* Guest data need not be word aligned; byte tails must participate too. */
    baseline = ready_tex_sig((uint8_t *)changed + 1, 5);
    ((uint8_t *)changed)[5] ^= 1;
    assert(baseline != ready_tex_sig((uint8_t *)changed + 1, 5));
    assert(ready_tex_sig(NULL, 0) != ready_tex_sig(original, 1));
    puts("localized texture edits: passed (8192 word positions, byte tails, unaligned data)");
    return 0;
}
