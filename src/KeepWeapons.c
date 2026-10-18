/*
 * KeepWeapons.asi
 *
 * Keeps the player's weapons and ammo when the player is Wasted (death) and,
 * optionally, when the player is Busted (arrest). Targets GTA IV Complete
 * Edition 1.2.0.59 (32-bit GTAIV.exe), also TLAD/TBoGT, under Proton, loaded
 * through the Ultimate ASI Loader that ships with FusionFix (dinput8.dll).
 *
 * Design (see README.md for sources and risks):
 *  - Pattern addresses are copied from FusionFix (ThirteenAG). Every pattern
 *    is required; if any fails, the plugin writes nothing to game code and
 *    stays inert.
 *  - One inline 5-byte call-site patch redirects the CGameProcess call in the
 *    main loop to our hook. The hook runs on the game's main thread, only when
 *    the game is not paused, and calls the original function afterwards.
 *  - Natives are looked up in the game's native table (same hash lookup as
 *    FusionFix) and invoked with a scrNativeCallContext-compatible struct.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "patscan.h"

/* ------------------------------------------------------------------------- */
/* Native hashes. Copied from FusionFix source/natives.ixx and iv-sdk         */
/* include/Scripting/NativeHashes.h (both agree on every value below).        */
/* ------------------------------------------------------------------------- */
enum {
    NAT_GET_PLAYER_ID,
    NAT_CONVERT_INT_TO_PLAYERINDEX,
    NAT_GET_PLAYER_CHAR,
    NAT_DOES_CHAR_EXIST,
    NAT_IS_CHAR_DEAD,
    NAT_IS_PLAYER_DEAD,
    NAT_IS_PLAYER_BEING_ARRESTED,
    NAT_IS_SCREEN_FADED_IN,
    NAT_GET_CHAR_WEAPON_IN_SLOT,
    NAT_GET_AMMO_IN_CHAR_WEAPON,
    NAT_GET_CURRENT_CHAR_WEAPON,
    NAT_REMOVE_ALL_CHAR_WEAPONS,
    NAT_GIVE_WEAPON_TO_CHAR,
    NAT_SET_CHAR_AMMO,
    NAT_SET_CURRENT_CHAR_WEAPON,
    NAT_COUNT
};

static const struct {
    const char* name;
    uint32_t hash;
} g_natDefs[NAT_COUNT] = {
    {"GET_PLAYER_ID",               0x62E319C6u},
    {"CONVERT_INT_TO_PLAYERINDEX",  0x5996315Eu},
    {"GET_PLAYER_CHAR",             0x511454A9u},
    {"DOES_CHAR_EXIST",             0x46531797u},
    {"IS_CHAR_DEAD",                0x6A6B4F18u},
    {"IS_PLAYER_DEAD",              0x12AE0E27u},
    {"IS_PLAYER_BEING_ARRESTED",    0x79A95BF9u},
    {"IS_SCREEN_FADED_IN",          0x5E0713B2u},
    {"GET_CHAR_WEAPON_IN_SLOT",     0x74EC7580u},
    {"GET_AMMO_IN_CHAR_WEAPON",     0x23E140A9u},
    {"GET_CURRENT_CHAR_WEAPON",     0x5AB8289Fu},
    {"REMOVE_ALL_CHAR_WEAPONS",     0x6BA520F0u},
    {"GIVE_WEAPON_TO_CHAR",         0x03E90416u},
    {"SET_CHAR_AMMO",               0x437D247Eu},
    {"SET_CURRENT_CHAR_WEAPON",     0x6CF44DD6u},
};

/* ------------------------------------------------------------------------- */
/* Byte patterns (FusionFix, ThirteenAG/GTAIV.EFLC.FusionFix @ cad0970)       */
/* ------------------------------------------------------------------------- */
/* Deferred-init sentinel: FusionFix Init() registration in source/dllmain.cpp */
#define PAT_SENTINEL   "F3 0F 10 44 24 ? F3 0F 59 05 ? ? ? ? EB ? E8"
/* rage::scrEngine::ms_dwNativeTableSize (source/comvars.ixx) */
#define PAT_TSIZE_A    "8B 35 ? ? ? ? 85 F6 75 06 33 C0 5E C2 04 00 53 57 8B 7C 24 10"
#define PAT_TSIZE_B    "8B 3D ? ? ? ? 85 FF 75 04 33 C0 5F C3"
/* rage::scrEngine::ms_pNatives (source/comvars.ixx) */
#define PAT_NATIVES_A  "8B 1D ? ? ? ? 8B CF 8B 04 D3 3B C7 74 19 8D 64 24 00 85 C0"
#define PAT_NATIVES_B  "8B 1D ? ? ? ? 8B CE 8B 04 D3 3B C6 74 17 85 C0"
/* CTimer::ms_bUserPause / ms_bScriptPause (source/comvars.ixx).
 * A-variant is extended with the second 'movzx ecx,[abs]' so the script-pause
 * address is part of the match instead of being read blindly at +15. */
#define PAT_PAUSE_A    "0F B6 0D ? ? ? ? 0F B6 C0 0B C1 0F B6 0D ? ? ? ?"
#define PAT_PAUSE_B    "0F B6 0D ? ? ? ? 0F B6 15 ? ? ? ? 33 C0 0B C1"
/* Call site of CGameProcess in the main loop (source/dllmain.cpp, hbCGameProcess) */
#define PAT_GAMEPROCESS "E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9"

/* ------------------------------------------------------------------------- */
/* Game-facing state                                                         */
/* ------------------------------------------------------------------------- */

/* Layout of rage::scrNativeCallContext + NativeContext (FusionFix natives.ixx,
 * iv-sdk NativeInvoke.h). Size 0xA0, args/returns live at m_TempStack. */
typedef struct NativeCtx {
    void*    m_pReturn;        /* 0x00 */
    uint32_t m_nArgCount;      /* 0x04 */
    void*    m_pArgs;          /* 0x08 */
    uint32_t m_nDataCount;     /* 0x0C */
    void*    m_pOriginalData[4]; /* 0x10 */
    float    m_TemporaryData[16]; /* 0x20 */
    uint32_t m_TempStack[16];  /* 0x60 */
} NativeCtx;

typedef char kw_assert_ctx_layout[(offsetof(NativeCtx, m_TempStack) == 0x60) ? 1 : -1];
typedef char kw_assert_ctx_size[(sizeof(NativeCtx) == 0xA0) ? 1 : -1];

typedef void (__cdecl *NativeHandler)(NativeCtx*);
typedef void (__cdecl *GameProcessFn)(void);

#define KW_MAX_WEAPONS 12
#define KW_FIRST_SLOT  1
#define KW_LAST_SLOT   11

typedef struct {
    uint32_t weapon;
    uint32_t ammo;
} KwWeapon;

typedef struct {
    int valid;
    int count;
    KwWeapon w[KW_MAX_WEAPONS];
    uint32_t current;
} Snapshot;

enum { ST_ALIVE = 0, ST_DOWN = 1 };
enum { CAUSE_NONE = 0, CAUSE_WASTED = 1, CAUSE_BUSTED = 2 };

#define RESPAWN_SETTLE_MS  1500u    /* wait this long after alive+controllable */
#define FADE_FALLBACK_MS   20000u   /* restore anyway if screen never reports faded-in */
#define DOWN_TIMEOUT_MS    180000u  /* give up if the player never comes back */

static char g_dir[MAX_PATH];
static char g_logPath[MAX_PATH + 32];
static char g_iniPath[MAX_PATH + 32];

static int g_cfgKeepOnDeath = 1;
static int g_cfgKeepOnArrest = 1;

static uintptr_t g_base;        /* GetModuleHandle(NULL) */
static uintptr_t g_scanEnd;     /* end of the last executable section */
static uintptr_t g_imageEnd;    /* base + SizeOfImage */
static int g_secCount;
static uintptr_t g_secStart[32];
static uintptr_t g_secEnd[32];
static int g_secExec[32];

static uint8_t* g_pUserPause;
static uint8_t* g_pScriptPause;
static uint32_t* g_pNativeTableSize;
static uint32_t** g_ppNativeTable;   /* address of the global that holds the table pointer */
static GameProcessFn g_origGameProcess;
static NativeHandler g_nat[NAT_COUNT];

static volatile int g_initDone;     /* init finished (success or permanent failure) */
static volatile int g_enabled;      /* hook is installed and the plugin may act */
static int g_natsResolved;
static int g_natsWaitLogged;

static int g_state = ST_ALIVE;
static int g_cause = CAUSE_NONE;
static DWORD g_downTick;
static DWORD g_aliveTick;
static Snapshot g_snap;

/* ------------------------------------------------------------------------- */
/* Logging (kernel32/user32 only, no CRT dependency)                          */
/* ------------------------------------------------------------------------- */
static void Logf(const char* fmt, ...)
{
    char buf[1024];
    char line[1100];
    va_list ap;
    DWORD written = 0;
    HANDLE h;

    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    wsprintfA(line, "[%u] %s\r\n", (unsigned)GetTickCount(), buf);

    if (g_logPath[0] == 0) return;
    h = CreateFileA(g_logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        WriteFile(h, line, (DWORD)strlen(line), &written, NULL);
        CloseHandle(h);
    }
}

static void SetupPaths(HINSTANCE hinst)
{
    char path[MAX_PATH];
    char* slash;
    DWORD n;

    n = GetModuleFileNameA(hinst, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        path[0] = 0;
    }
    slash = strrchr(path, '\\');
    if (!slash) slash = strrchr(path, '/');
    if (slash) slash[1] = 0; else path[0] = 0;

    strcpy(g_dir, path);
    strcpy(g_logPath, g_dir);
    strcat(g_logPath, "KeepWeapons.log");
    strcpy(g_iniPath, g_dir);
    strcat(g_iniPath, "KeepWeapons.ini");
}

static void LoadConfig(void)
{
    /* Missing file or key -> defaults (both 1, per the task). */
    g_cfgKeepOnDeath = GetPrivateProfileIntA("MAIN", "KeepOnDeath", 1, g_iniPath) != 0;
    g_cfgKeepOnArrest = GetPrivateProfileIntA("MAIN", "KeepOnArrest", 1, g_iniPath) != 0;
}

/* ------------------------------------------------------------------------- */
/* Image / pattern helpers                                                   */
/* ------------------------------------------------------------------------- */
static int ParseImage(void)
{
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS* nt;
    IMAGE_SECTION_HEADER* sec;
    unsigned i;

    g_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!g_base) return 0;
    dos = (IMAGE_DOS_HEADER*)g_base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    nt = (IMAGE_NT_HEADERS*)(g_base + (uintptr_t)dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0; /* must be 32-bit */

    g_imageEnd = g_base + nt->OptionalHeader.SizeOfImage;
    g_scanEnd = 0;
    g_secCount = 0;
    sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections && g_secCount < 32; i++, sec++) {
        uintptr_t s = g_base + sec->VirtualAddress;
        uint32_t size = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
        int exec = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        g_secStart[g_secCount] = s;
        g_secEnd[g_secCount] = s + size;
        g_secExec[g_secCount] = exec;
        g_secCount++;
        if (exec && s + size > g_scanEnd) g_scanEnd = s + size;
    }
    return g_scanEnd != 0;
}

/* A global must live inside the image and outside every executable section. */
static int IsDataAddr(uintptr_t addr)
{
    int i;
    if (addr < g_base || addr + 4 > g_imageEnd) return 0;
    for (i = 0; i < g_secCount; i++) {
        if (g_secExec[i] && addr >= g_secStart[i] && addr < g_secEnd[i]) return 0;
    }
    return 1;
}

static int ScanPat(const char* text, uintptr_t* out, int max)
{
    KwPattern p;
    if (!kw_parse_pattern(text, &p)) {
        Logf("internal: malformed pattern: %s", text);
        return -1;
    }
    return kw_scan(&p, g_base, g_scanEnd, out, max);
}

/*
 * Resolves a global whose absolute address is the 32-bit operand at 'off'
 * of the instruction matched by 'pat'. All matches must agree.
 */
static int ResolveGlobal(const char* name, const char* pat, int off, uintptr_t* outAddr)
{
    uintptr_t m[8];
    int n, k;
    uint32_t g0;

    n = ScanPat(pat, m, 8);
    if (n <= 0) {
        Logf("pattern %s: NOT FOUND", name);
        return 0;
    }
    if (n > 8) n = 8;
    g0 = kw_read_u32(m[0] + off);
    for (k = 1; k < n; k++) {
        if (kw_read_u32(m[k] + off) != g0) {
            Logf("pattern %s: ambiguous, %d matches disagree", name, n);
            return 0;
        }
    }
    Logf("pattern %s: %d match(es), first at 0x%08X, global=0x%08X", name, n, (unsigned)m[0], (unsigned)g0);
    if (!IsDataAddr((uintptr_t)g0)) {
        Logf("pattern %s: global 0x%08X is not a data address, rejected", name, (unsigned)g0);
        return 0;
    }
    *outAddr = (uintptr_t)g0;
    return 1;
}

/* CTimer pause flags. A-variant: user@+3, script@+15. B-variant: user@+3, script@+10. */
static int ResolvePause(uintptr_t* user, uintptr_t* script)
{
    uintptr_t m[8];
    int n, k;
    uint32_t u0, s0;

    n = ScanPat(PAT_PAUSE_A, m, 8);
    if (n > 0) {
        if (n > 8) n = 8;
        u0 = kw_read_u32(m[0] + 3);
        s0 = kw_read_u32(m[0] + 15);
        for (k = 1; k < n; k++) {
            if (kw_read_u32(m[k] + 3) != u0 || kw_read_u32(m[k] + 15) != s0) n = -1;
        }
        if (n > 0) {
            Logf("pattern ms_bPause(A): %d match(es), user=0x%08X script=0x%08X", n, (unsigned)u0, (unsigned)s0);
            if (IsDataAddr(u0) && IsDataAddr(s0)) { *user = u0; *script = s0; return 1; }
            Logf("pattern ms_bPause(A): addresses rejected");
            return 0;
        }
        Logf("pattern ms_bPause(A): ambiguous");
        return 0;
    }

    n = ScanPat(PAT_PAUSE_B, m, 8);
    if (n > 0) {
        if (n > 8) n = 8;
        u0 = kw_read_u32(m[0] + 3);
        s0 = kw_read_u32(m[0] + 10);
        for (k = 1; k < n; k++) {
            if (kw_read_u32(m[k] + 3) != u0 || kw_read_u32(m[k] + 10) != s0) n = -1;
        }
        if (n > 0) {
            Logf("pattern ms_bPause(B): %d match(es), user=0x%08X script=0x%08X", n, (unsigned)u0, (unsigned)s0);
            if (IsDataAddr(u0) && IsDataAddr(s0)) { *user = u0; *script = s0; return 1; }
            Logf("pattern ms_bPause(B): addresses rejected");
            return 0;
        }
        Logf("pattern ms_bPause(B): ambiguous");
        return 0;
    }

    Logf("pattern ms_bPause: NOT FOUND (both variants)");
    return 0;
}

/* Overwrites the rel32 of an E8 call at 'site' with a call to 'target'.
 * Returns the original call target via 'orig'. */
static int PatchCall(uintptr_t site, void* target, uintptr_t* orig)
{
    int64_t newRel;
    int32_t rel;
    DWORD oldProt = 0;

    if (*(const uint8_t*)site != 0xE8) return 0;
    rel = (int32_t)kw_read_u32(site + 1);
    newRel = (int64_t)(uintptr_t)target - (int64_t)(site + 5);
    if (newRel > 0x7FFFFFFFLL || newRel < -0x80000000LL) return 0;
    *orig = (uintptr_t)(site + 5) + (uintptr_t)rel;

    if (!VirtualProtect((LPVOID)site, 5, PAGE_EXECUTE_READWRITE, &oldProt)) return 0;
    *(int32_t*)(site + 1) = (int32_t)newRel;
    VirtualProtect((LPVOID)site, 5, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)site, 5);
    return 1;
}

/* ------------------------------------------------------------------------- */
/* Native invocation                                                          */
/* ------------------------------------------------------------------------- */

/* Same lookup as FusionFix natives.ixx NativeInvoke::GetNativeHandler. */
static NativeHandler LookupNative(uint32_t hash)
{
    uint32_t* tbl;
    uint32_t size, idx, tmp, handler, guard = 0;

    if (!g_ppNativeTable || !g_pNativeTableSize) return NULL;
    tbl = *g_ppNativeTable;
    size = *g_pNativeTableSize;
    if (!tbl || size == 0 || size > 0x100000u || hash == 0) return NULL;

    idx = hash % size;
    tmp = hash;
    handler = tbl[2 * idx];
    if (handler != hash) {
        while (handler) {
            tmp = (tmp >> 1) + 1;
            idx = (tmp + idx) % size;
            handler = tbl[2 * idx];
            if (handler == hash) break;
            if (++guard > size) return NULL;
        }
    }
    if (!handler) return NULL;
    return (NativeHandler)(uintptr_t)tbl[2 * idx + 1];
}

/* Returns 0 if the native is not available (not resolved, or paused). */
static int CallNat(int id, const uint32_t* args, uint32_t nargs, uint32_t* ret)
{
    NativeCtx c;
    uint32_t i;

    if (!g_nat[id]) return 0;
    if (g_pScriptPause && *g_pScriptPause) return 0;

    memset(&c, 0, sizeof(c));
    c.m_pArgs = c.m_TempStack;
    c.m_pReturn = c.m_TempStack;
    c.m_nArgCount = nargs;
    for (i = 0; i < nargs && i < 16; i++) c.m_TempStack[i] = args ? args[i] : 0;

    g_nat[id](&c);
    if (ret) *ret = c.m_TempStack[0];
    return 1;
}

/* Bool natives may write only one byte of the return slot, so read the low byte. */
static int CallBool(int id, const uint32_t* args, uint32_t nargs)
{
    uint32_t r = 0;
    if (!CallNat(id, args, nargs, &r)) return 0;
    return (r & 0xFF) != 0;
}

/* Returns 1 if all natives are resolved, 0 if the table is not ready yet, -1 on a missing native. */
static int ResolveNatives(void)
{
    uint32_t* tbl;
    uint32_t size;
    int i, missing = 0;

    if (!g_ppNativeTable || !g_pNativeTableSize) return -1;
    tbl = *g_ppNativeTable;
    size = *g_pNativeTableSize;
    if (!tbl || size == 0) return 0;

    for (i = 0; i < NAT_COUNT; i++) {
        g_nat[i] = LookupNative(g_natDefs[i].hash);
        if (!g_nat[i]) {
            Logf("native %s (0x%08X): NOT FOUND", g_natDefs[i].name, (unsigned)g_natDefs[i].hash);
            missing++;
        } else {
            Logf("native %s (0x%08X): ok -> 0x%08X", g_natDefs[i].name, (unsigned)g_natDefs[i].hash, (unsigned)(uintptr_t)g_nat[i]);
        }
    }
    return missing ? -1 : 1;
}

/* ------------------------------------------------------------------------- */
/* Game logic                                                                 */
/* ------------------------------------------------------------------------- */
static void GetPlayer(uint32_t* player, uint32_t* ped)
{
    uint32_t a[2], id = 0;

    *player = 0;
    *ped = 0;
    if (!CallNat(NAT_GET_PLAYER_ID, NULL, 0, &id)) return;
    a[0] = id;
    if (!CallNat(NAT_CONVERT_INT_TO_PLAYERINDEX, a, 1, player)) return;
    a[0] = *player;
    a[1] = (uint32_t)(uintptr_t)ped;
    CallNat(NAT_GET_PLAYER_CHAR, a, 2, NULL);
}

static void TakeSnapshot(uint32_t ped, Snapshot* out)
{
    Snapshot s;
    uint32_t slot;

    memset(&s, 0, sizeof(s));
    for (slot = KW_FIRST_SLOT; slot <= KW_LAST_SLOT; slot++) {
        uint32_t args[5];
        uint32_t weapon = 0, a0 = 0, a1 = 0, ammo = 0;

        args[0] = ped;
        args[1] = slot;
        args[2] = (uint32_t)(uintptr_t)&weapon;
        args[3] = (uint32_t)(uintptr_t)&a0;
        args[4] = (uint32_t)(uintptr_t)&a1;
        CallNat(NAT_GET_CHAR_WEAPON_IN_SLOT, args, 5, NULL);
        if (weapon == 0 || weapon == 0xFFFFFFFFu) continue; /* empty or unarmed */

        args[0] = ped;
        args[1] = weapon;
        args[2] = (uint32_t)(uintptr_t)&ammo;
        CallNat(NAT_GET_AMMO_IN_CHAR_WEAPON, args, 3, NULL);

        if (s.count < KW_MAX_WEAPONS) {
            s.w[s.count].weapon = weapon;
            s.w[s.count].ammo = ammo;
            s.count++;
        }
    }
    {
        uint32_t args[2];
        uint32_t cur = 0;
        args[0] = ped;
        args[1] = (uint32_t)(uintptr_t)&cur;
        CallNat(NAT_GET_CURRENT_CHAR_WEAPON, args, 2, NULL);
        s.current = cur;
    }
    s.valid = 1;
    *out = s;
}

static void RestoreSnapshot(uint32_t ped, const Snapshot* s)
{
    uint32_t a[4];
    int i;

    /* Nothing was recorded (unarmed, or natives returned nothing): leave the
     * respawn loadout alone rather than stripping it. */
    if (s->count == 0) return;

    /* Clear first so a restore can never stack ammo on top of existing weapons. */
    a[0] = ped;
    CallNat(NAT_REMOVE_ALL_CHAR_WEAPONS, a, 1, NULL);
    for (i = 0; i < s->count; i++) {
        a[0] = ped;
        a[1] = s->w[i].weapon;
        a[2] = s->w[i].ammo;
        a[3] = 1; /* unk0: flag used by IV scripts, value unverified */
        CallNat(NAT_GIVE_WEAPON_TO_CHAR, a, 4, NULL);
        a[0] = ped;
        a[1] = s->w[i].weapon;
        a[2] = s->w[i].ammo;
        CallNat(NAT_SET_CHAR_AMMO, a, 3, NULL); /* make the total exact */
    }
    if (s->current) {
        a[0] = ped;
        a[1] = s->current;
        a[2] = 1;
        CallNat(NAT_SET_CURRENT_CHAR_WEAPON, a, 3, NULL);
    }
}

static const char* CauseName(int c)
{
    return c == CAUSE_WASTED ? "WASTED" : (c == CAUSE_BUSTED ? "BUSTED" : "NONE");
}

static void OnFrame(void)
{
    uint32_t player = 0, ped = 0, a[1];
    int dead, arrested, faded;
    DWORD now;

    if (!g_natsResolved) {
        int r = ResolveNatives();
        if (r == 0) {
            if (!g_natsWaitLogged) {
                Logf("natives: native table not ready yet, waiting");
                g_natsWaitLogged = 1;
            }
            return;
        }
        if (r < 0) {
            Logf("natives: a required native is missing, plugin disabled");
            g_enabled = 0;
            return;
        }
        g_natsResolved = 1;
        Logf("natives: all resolved");
    }
    if (g_pScriptPause && *g_pScriptPause) return;

    GetPlayer(&player, &ped);
    if (!ped) return;
    a[0] = ped;
    if (!CallBool(NAT_DOES_CHAR_EXIST, a, 1)) return;

    a[0] = ped;
    dead = CallBool(NAT_IS_CHAR_DEAD, a, 1);
    if (!dead) {
        a[0] = player;
        dead = CallBool(NAT_IS_PLAYER_DEAD, a, 1);
    }
    arrested = CallBool(NAT_IS_PLAYER_BEING_ARRESTED, NULL, 0);
    now = GetTickCount();

    if (g_state == ST_ALIVE) {
        if (dead || arrested) {
            int cause = arrested ? CAUSE_BUSTED : CAUSE_WASTED;
            int keep = (cause == CAUSE_BUSTED) ? g_cfgKeepOnArrest : g_cfgKeepOnDeath;
            if (keep && g_snap.valid) {
                g_state = ST_DOWN;
                g_cause = cause;
                g_downTick = now;
                g_aliveTick = 0;
                Logf("down: cause=%s weapons=%d current=%u, snapshot frozen", CauseName(cause), g_snap.count, (unsigned)g_snap.current);
            } else {
                if (g_snap.valid) Logf("down: cause=%s not kept by config, nothing frozen", CauseName(cause));
                g_snap.valid = 0;
            }
            return;
        }
        TakeSnapshot(ped, &g_snap);
        return;
    }

    /* ST_DOWN: wait for the respawn / release to finish, then restore once. */
    if (dead || arrested) {
        g_aliveTick = 0;
        if (now - g_downTick > DOWN_TIMEOUT_MS) {
            Logf("down: timed out after %u ms, giving up on restore", (unsigned)DOWN_TIMEOUT_MS);
            g_state = ST_ALIVE;
            g_snap.valid = 0;
        }
        return;
    }
    if (g_aliveTick == 0) g_aliveTick = now;
    faded = CallBool(NAT_IS_SCREEN_FADED_IN, NULL, 0);
    if (now - g_aliveTick >= RESPAWN_SETTLE_MS &&
        (faded || now - g_aliveTick >= FADE_FALLBACK_MS)) {
        int restored = g_snap.count;
        RestoreSnapshot(ped, &g_snap);
        Logf("restore: cause=%s weapons=%d current=%u faded=%d", CauseName(g_cause), restored,
             (unsigned)g_snap.current, faded);
        g_state = ST_ALIVE;
        g_cause = CAUSE_NONE;
        g_snap.valid = 0;
    }
}

/* Installed over the CGameProcess call in the main loop. */
static void __cdecl HookGameProcess(void)
{
    if (g_enabled && g_pUserPause && g_pScriptPause && !*g_pUserPause && !*g_pScriptPause)
        OnFrame();
    g_origGameProcess();
}

/* ------------------------------------------------------------------------- */
/* Initialization                                                             */
/* ------------------------------------------------------------------------- */
static int Fail(const char* why)
{
    Logf("init FAILED: %s. Plugin is inactive, no game code was modified.", why);
    g_initDone = 1;
    return -1;
}

/* Returns 1 = installed, 0 = not ready yet (retry), -1 = permanent failure. */
static int TryInit(void)
{
    uintptr_t m[4];
    uintptr_t tsizeGlobal = 0, nativesGlobal = 0, userP = 0, scriptP = 0;
    uintptr_t site = 0, orig = 0;
    int n;

    if (g_initDone) return g_enabled ? 1 : -1;
    if (!ParseImage()) return Fail("PE header parse failed (not a 32-bit image?)");

    n = ScanPat(PAT_SENTINEL, m, 1);
    if (n <= 0) return 0; /* the game code is not ready yet, the poll thread will retry */
    Logf("init: sentinel found (%d match), image base=0x%08X scan_end=0x%08X", n, (unsigned)g_base, (unsigned)g_scanEnd);

    if (!ResolveGlobal("ms_dwNativeTableSize(A)", PAT_TSIZE_A, 2, &tsizeGlobal) &&
        !ResolveGlobal("ms_dwNativeTableSize(B)", PAT_TSIZE_B, 2, &tsizeGlobal))
        return Fail("ms_dwNativeTableSize not resolved");

    if (!ResolveGlobal("ms_pNatives(A)", PAT_NATIVES_A, 2, &nativesGlobal) &&
        !ResolveGlobal("ms_pNatives(B)", PAT_NATIVES_B, 2, &nativesGlobal))
        return Fail("ms_pNatives not resolved");

    if (!ResolvePause(&userP, &scriptP))
        return Fail("CTimer pause flags not resolved");

    n = ScanPat(PAT_GAMEPROCESS, m, 4);
    if (n != 1) {
        Logf("pattern CGameProcess call site: %d matches (need exactly 1)", n);
        return Fail("CGameProcess call site not uniquely resolved");
    }
    site = m[0];
    Logf("pattern CGameProcess call site: 1 match at 0x%08X", (unsigned)site);

    /* All lookups succeeded. From here on we write to game code. */
    if (*(const uint8_t*)site != 0xE8)
        return Fail("CGameProcess call site does not start with E8");
    orig = site + 5 + (uintptr_t)kw_read_u32(site + 1);

    /* Publish everything the hook reads BEFORE the call is redirected: the
     * hook may run on the main thread as soon as the patch lands. */
    g_pNativeTableSize = (uint32_t*)tsizeGlobal;
    g_ppNativeTable = (uint32_t**)nativesGlobal;
    g_pUserPause = (uint8_t*)userP;
    g_pScriptPause = (uint8_t*)scriptP;
    g_origGameProcess = (GameProcessFn)orig;

    if (!PatchCall(site, (void*)(uintptr_t)HookGameProcess, &orig))
        return Fail("CGameProcess call patch rejected (target out of rel32 range or bad opcode)");
    Logf("init: hooked CGameProcess call at 0x%08X, original=0x%08X", (unsigned)site, (unsigned)orig);

    g_enabled = 1;
    g_initDone = 1;
    Logf("init: OK. Natives are resolved on the first frame where the table is populated.");
    return 1;
}

static DWORD WINAPI PollThread(LPVOID unused)
{
    int i;
    (void)unused;
    for (i = 0; i < 3000; i++) { /* up to 300 s */
        Sleep(100);
        if (TryInit() != 0) return 0;
    }
    Logf("init: sentinel not found after 300 s, plugin inactive");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE th;
        DisableThreadLibraryCalls(hinst);
        SetupPaths(hinst);
        LoadConfig();
        Logf("KeepWeapons loaded. KeepOnDeath=%d KeepOnArrest=%d", g_cfgKeepOnDeath, g_cfgKeepOnArrest);
        if (TryInit() == 0) {
            th = CreateThread(NULL, 0, PollThread, NULL, 0, NULL);
            if (th) CloseHandle(th);
            else Logf("init: CreateThread failed, plugin inactive");
        }
    }
    return TRUE;
}
