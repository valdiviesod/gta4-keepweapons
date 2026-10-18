/* Host unit test for src/patscan.h (no game, no Windows). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "patscan.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } else { printf("ok: %s\n", msg); } } while (0)

int main(void)
{
    KwPattern p;
    uintptr_t m[8];
    uint8_t buf[64];
    int n;

    /* Parser */
    CHECK(kw_parse_pattern("E8 ? ? ? ? 83 C4 ??", &p) && p.len == 8, "parse: E8 ? ? ? ? 83 C4 ?? -> 8 tokens");
    CHECK(p.mask[1] == 0 && p.mask[0] == 0xFF && p.bytes[5] == 0x83, "parse: wildcard and hex masks");
    CHECK(!kw_parse_pattern("E8 G1", &p), "parse: rejects non-hex");
    CHECK(!kw_parse_pattern("E", &p), "parse: rejects one nibble");
    CHECK(!kw_parse_pattern("8B 3", &p), "parse: rejects truncated byte");
    CHECK(!kw_parse_pattern("", &p), "parse: rejects empty pattern");

    /* Scanner: single match with wildcards */
    memset(buf, 0x90, sizeof(buf));
    buf[10] = 0xE8; buf[11] = 1; buf[12] = 2; buf[13] = 3; buf[14] = 4;
    buf[15] = 0x83; buf[16] = 0xC4; buf[17] = 0x99;
    kw_parse_pattern("E8 ? ? ? ? 83 C4 ??", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 8);
    CHECK(n == 1 && m[0] == (uintptr_t)(buf + 10), "scan: finds single wildcard match at offset 10");

    /* No match */
    kw_parse_pattern("E8 ? ? ? ? 84 C4", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 8);
    CHECK(n == 0, "scan: no match when a fixed byte differs");

    /* Multiple matches, total count reported even above max */
    memset(buf, 0x00, sizeof(buf));
    buf[3] = 0x8B; buf[4] = 0x35; buf[20] = 0x8B; buf[21] = 0x35; buf[40] = 0x8B; buf[41] = 0x35;
    kw_parse_pattern("8B 35", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 2);
    CHECK(n == 3, "scan: reports total of 3 matches");
    CHECK(m[0] == (uintptr_t)(buf + 3) && m[1] == (uintptr_t)(buf + 20), "scan: first two addresses stored");

    /* Match at the very end of the range and not beyond */
    memset(buf, 0x00, sizeof(buf));
    buf[62] = 0xAA; buf[63] = 0xBB;
    kw_parse_pattern("AA BB", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 8);
    CHECK(n == 1 && m[0] == (uintptr_t)(buf + 62), "scan: match ending at range end is found");
    kw_parse_pattern("AA BB CC", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 8);
    CHECK(n == 0, "scan: no read past range end");

    /* Pause A variant with the extended second movzx: user@+3, script@+15 */
    memset(buf, 0x90, sizeof(buf));
    {
        static const uint8_t code[] = {
            0x0F, 0xB6, 0x0D, 0x11, 0x22, 0x33, 0x44,   /* movzx ecx, [user] */
            0x0F, 0xB6, 0xC0, 0x0B, 0xC1,               /* movzx eax,al; or eax,ecx */
            0x0F, 0xB6, 0x0D, 0x55, 0x66, 0x77, 0x88    /* movzx ecx, [script] */
        };
        memcpy(buf + 4, code, sizeof(code));
    }
    kw_parse_pattern("0F B6 0D ? ? ? ? 0F B6 C0 0B C1 0F B6 0D ? ? ? ?", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 8);
    CHECK(n == 1 && m[0] == (uintptr_t)(buf + 4), "pause-A: matches the code shape");
    CHECK(kw_read_u32(m[0] + 3) == 0x44332211u, "pause-A: user flag address read at +3 (little endian)");
    CHECK(kw_read_u32(m[0] + 15) == 0x88776655u, "pause-A: script flag address read at +15");

    /* Pause B variant: user@+3, script@+10 */
    memset(buf, 0x90, sizeof(buf));
    {
        static const uint8_t code[] = {
            0x0F, 0xB6, 0x0D, 0x01, 0x02, 0x03, 0x04,   /* movzx ecx, [user] */
            0x0F, 0xB6, 0x15, 0x05, 0x06, 0x07, 0x08,   /* movzx edx, [script] */
            0x33, 0xC0, 0x0B, 0xC1
        };
        memcpy(buf + 8, code, sizeof(code));
    }
    kw_parse_pattern("0F B6 0D ? ? ? ? 0F B6 15 ? ? ? ? 33 C0 0B C1", &p);
    n = kw_scan(&p, (uintptr_t)buf, (uintptr_t)buf + sizeof(buf), m, 8);
    CHECK(n == 1 && kw_read_u32(m[0] + 10) == 0x08070605u, "pause-B: script flag address read at +10");

    printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
