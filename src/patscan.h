/*
 * patscan.h - portable IDA-style byte pattern helpers used by KeepWeapons.asi.
 * Pure C, no Windows dependencies, so it can be unit-tested on the host.
 *
 * Pattern syntax (same as Hooking.Patterns / FusionFix):
 *   "E8 ? ? ? ? 83 C4 ??"  -> hex bytes, '?' or '??' is a one-byte wildcard.
 */
#ifndef KW_PATSCAN_H
#define KW_PATSCAN_H

#include <stddef.h>
#include <stdint.h>

#define KW_PAT_MAX 96

typedef struct {
    uint8_t bytes[KW_PAT_MAX];
    uint8_t mask[KW_PAT_MAX]; /* 0xFF = compare, 0x00 = wildcard */
    int len;
} KwPattern;

static inline int kw_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Returns 1 on success, 0 on malformed input. */
static inline int kw_parse_pattern(const char* s, KwPattern* p)
{
    int n = 0;
    p->len = 0;
    while (*s) {
        while (*s == ' ' || *s == '\t') s++;
        if (!*s) break;
        if (n >= KW_PAT_MAX) return 0;
        if (*s == '?') {
            s++;
            if (*s == '?') s++;
            p->bytes[n] = 0;
            p->mask[n] = 0;
            n++;
        } else {
            int hi = kw_hexval(s[0]);
            int lo = (hi >= 0 && s[1]) ? kw_hexval(s[1]) : -1;
            if (hi < 0 || lo < 0) return 0;
            p->bytes[n] = (uint8_t)(hi * 16 + lo);
            p->mask[n] = 0xFF;
            n++;
            s += 2;
        }
        if (*s && *s != ' ' && *s != '\t') return 0;
    }
    p->len = n;
    return n > 0;
}

/*
 * Scans [start, end) for the pattern. Stores up to 'max' match addresses in
 * 'out'. Returns the TOTAL number of matches (may exceed 'max').
 */
static inline int kw_scan(const KwPattern* p, uintptr_t start, uintptr_t end, uintptr_t* out, int max)
{
    int found = 0;
    uintptr_t a, last;
    if (p->len <= 0 || end < start) return 0;
    if ((uintptr_t)p->len > end - start) return 0;
    last = end - (uintptr_t)p->len;
    for (a = start; a <= last; a++) {
        const uint8_t* q = (const uint8_t*)a;
        int j;
        for (j = 0; j < p->len; j++) {
            if ((q[j] & p->mask[j]) != (p->bytes[j] & p->mask[j])) break;
        }
        if (j == p->len) {
            if (found < max && out) out[found] = a;
            found++;
        }
        if (a == last) break; /* guard against uintptr wrap */
    }
    return found;
}

/* Reads the 32-bit absolute address / displacement stored at instr+off. */
static inline uint32_t kw_read_u32(uintptr_t addr)
{
    uint32_t v;
    const uint8_t* q = (const uint8_t*)addr;
    v = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
    return v;
}

#endif /* KW_PATSCAN_H */
