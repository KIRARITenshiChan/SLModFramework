/* scene.cpp -- read-only Fusion frame (CRunFrame) identification.
 * See scene.h for the empirically derived layout and the anchor strategy.
 * Runs on the framework worker thread only; drawing threads read Current().
 */
#include "common.h"
#include "scene.h"
#include "frames_table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_CHUNK (256 * 1024)
#define SCENE_MAX_REGIONS 2048
#define SCENE_RESCAN_MIN_MS 1000   /* full rescan rate limit */
#define SCENE_NAME_MAX 79

static SceneInfo g_cur = { -1, 0, "", 0, 0, 0, 0, 0 };
static unsigned long g_cachedBase;
static unsigned long g_anchorStruct;
static unsigned long g_rhPtr;
static unsigned long g_appAddr;      /* cached CRunApp* (fast frame read) */
static DWORD g_appFailSince;
static DWORD g_lastAppScan;          /* throttles CRunApp signature scans */
static DWORD g_lastScanTick;
static unsigned long g_polls;
static unsigned char g_hSet[65536];
static int g_tablesReady;

struct SceneRegion { UINT_PTR base; SIZE_T size; };
static SceneRegion g_regions[SCENE_MAX_REGIONS];
static int g_regionCount;

static int ReadSelf(UINT_PTR addr, void *buf, SIZE_T len)
{
    SIZE_T got = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, buf, len, &got)) return 0;
    return got == len;
}

/* Only used by the development-only frame warp below (writes two RunHeader
 * fields; the scene module itself stays read-only otherwise). */
static int WriteSelf(UINT_PTR addr, const void *buf, SIZE_T len)
{
    SIZE_T wrote = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), (LPVOID)addr, buf, len, &wrote)) return 0;
    return wrote == len;
}

static void BuildTables(void)
{
    int i;
    if (g_tablesReady) return;
    memset(g_hSet, 0, sizeof(g_hSet));
    for (i = 0; i < kFrameCount; ++i) {
        unsigned h = kFrameTable[i].h;
        if (h < 65536) g_hSet[h] = 1;
    }
    g_tablesReady = 1;
}

static int CollectRegions(void)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR addr, maxAddr;
    g_regionCount = 0;
    GetSystemInfo(&si);
    addr = (UINT_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (UINT_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxAddr && g_regionCount < SCENE_MAX_REGIONS) {
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            g_regions[g_regionCount].base = (UINT_PTR)mbi.BaseAddress;
            g_regions[g_regionCount].size = mbi.RegionSize;
            g_regionCount++;
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    return g_regionCount;
}

static int RegionContains(UINT_PTR p)
{
    int i;
    for (i = 0; i < g_regionCount; ++i) {
        if (p >= g_regions[i].base && p < g_regions[i].base + g_regions[i].size) return 1;
    }
    return 0;
}

/* Frame names in this build are plain ASCII; reject anything else so a random
 * pointer with plausible dword neighbours cannot masquerade as a frame. */
static int ReadWideName(UINT_PTR p, char *out, int outSize)
{
    wchar_t wbuf[SCENE_NAME_MAX + 1];
    int i;
    if (p < 0x10000 || p > 0x7FFFFFFF) return 0;
    memset(wbuf, 0, sizeof(wbuf));
    if (!ReadSelf(p, wbuf, sizeof(wbuf) - sizeof(wchar_t))) return 0;
    for (i = 0; i < SCENE_NAME_MAX; ++i) {
        wchar_t c = wbuf[i];
        if (!c) break;
        if (c < 0x20 || c > 0x7E) return 0;
        out[i] = (char)c;
    }
    if (i == 0 || i >= outSize) return 0;
    out[i] = 0;
    return 1;
}

static int MatchFrame(unsigned w, unsigned h, unsigned layers, unsigned inst, const char *name)
{
    int i;
    for (i = 0; i < kFrameCount; ++i) {
        const FrameDef *d = &kFrameTable[i];
        if (d->w == w && d->h == h && d->layers == layers && d->instances == inst &&
            _stricmp(d->name, name) == 0)
            return i;
    }
    return -1;
}

static int ReadFrameAt(unsigned long base, SceneInfo *out)
{
    unsigned char raw[0x50];
    unsigned w, h, namePtr, layers, inst;
    char name[64];
    int idx;
    if (!base || base < 0x10000) return 0;
    memset(raw, 0, sizeof(raw));
    if (!ReadSelf((UINT_PTR)base, raw, sizeof(raw))) return 0;
    memcpy(&w, raw + 0x00, 4);
    memcpy(&h, raw + 0x04, 4);
    memcpy(&namePtr, raw + 0x10, 4);
    memcpy(&layers, raw + 0x2C, 4);
    memcpy(&inst, raw + 0x44, 4);
    if (w == 0 || w > 16384 || h == 0 || h > 16384) return 0;
    if (layers > 64 || inst > 4096 || namePtr < 0x10000) return 0;
    if (!ReadWideName((UINT_PTR)namePtr, name, sizeof(name))) return 0;
    idx = MatchFrame(w, h, layers, inst, name);
    if (idx < 0) return 0;
    if (out) {
        memset(out, 0, sizeof(*out));
        out->index = idx;
        out->valid = 1;
        lstrcpynA(out->name, name, sizeof(out->name));
        out->w = w; out->h = h; out->layers = layers; out->instances = inst;
        out->base = base;
    }
    return 1;
}

/* The anchor block found in all three dumps: PAMU* at +0x34, CRunFrame* at
 * +0x38 and RunHeader* at +0x3C, with the main window handle at +0x10. */
static int TryAnchor(UINT_PTR anchor, SceneInfo *out, unsigned long *rhOut)
{
    unsigned char raw[0x40];
    unsigned pamu, framePtr, rh;
    if (!anchor || anchor < 0x10000 || (anchor & 3)) return 0;
    if (!ReadSelf(anchor, raw, sizeof(raw))) return 0;
    memcpy(&pamu, raw + 0x34, 4);
    memcpy(&framePtr, raw + 0x38, 4);
    memcpy(&rh, raw + 0x3C, 4);
    if (pamu != (unsigned)g_fw.pamuAddr) return 0;
    if (!ReadFrameAt(framePtr, out)) return 0;
    if (rhOut) *rhOut = rh;
    return 1;
}

static int AnchorScan(SceneInfo *out, unsigned long *anchorOut, unsigned long *rhOut)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR addr, maxAddr;
    DWORD start = GetTickCount();
    unsigned char *buf;
    unsigned long long scanned = 0;
    int regions = 0, hits = 0, readFail = 0;
    int bestScore = 0;
    SceneInfo best;
    unsigned long bestAnchor = 0, bestRh = 0;
    if (!g_fw.pamuAddr) return 0;
    buf = (unsigned char *)malloc(SCENE_CHUNK);
    if (!buf) return 0;
    CollectRegions();
    GetSystemInfo(&si);
    addr = (UINT_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (UINT_PTR)si.lpMaximumApplicationAddress;
    memset(&best, 0, sizeof(best));
    while (addr < maxAddr) {
        SIZE_T scanLen, off;
        if ((GetTickCount() - start) > 8000) break;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
            continue;
        }
        regions++;
        scanLen = mbi.RegionSize;
        if (scanLen > 0x4000000) scanLen = 0x4000000;
        for (off = 0; off < scanLen; off += SCENE_CHUNK) {
            SIZE_T n = scanLen - off;
            SIZE_T i;
            UINT_PTR chunkBase;
            if (n > SCENE_CHUNK) n = SCENE_CHUNK;
            if (n < 64) break;
            chunkBase = (UINT_PTR)mbi.BaseAddress + off;
            if (!ReadSelf(chunkBase, buf, n)) { readFail++; break; }
            for (i = 0; i + 4 <= n; i += 4) {
                unsigned v;
                memcpy(&v, buf + i, 4);
                if (v != (unsigned)g_fw.pamuAddr) continue;
                hits++;
                if (i >= 0x34) {
                    SceneInfo tmp;
                    unsigned long rh = 0;
                    unsigned hwndW = 0;
                    int score = 1;
                    if (TryAnchor(chunkBase + i - 0x34, &tmp, &rh)) {
                        if (ReadSelf(chunkBase + i - 0x34 + 0x10, &hwndW, 4) &&
                            hwndW && IsWindow((HWND)(UINT_PTR)hwndW))
                            score = 2;
                        if (score > bestScore) {
                            bestScore = score;
                            best = tmp;
                            bestAnchor = (unsigned long)(chunkBase + i - 0x34);
                            bestRh = rh;
                        }
                    }
                }
            }
            scanned += n;
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    free(buf);
    if (bestScore > 0) {
        *out = best;
        if (anchorOut) *anchorOut = bestAnchor;
        if (rhOut) *rhOut = bestRh;
        Logf("scene| anchor ok struct=%08lX frame=%08lX rh=%08lX score=%d regions=%d bytes=%llu hits=%d read_fail=%d ms=%lu",
             bestAnchor, best.base, bestRh, bestScore, regions, scanned, hits, readFail,
             (unsigned long)(GetTickCount() - start));
        return 1;
    }
    Logf("scene| anchor not found regions=%d bytes=%llu hits=%d read_fail=%d ms=%lu",
         regions, scanned, hits, readFail, (unsigned long)(GetTickCount() - start));
    return 0;
}

/* Fallback: look for a candidate CRunFrame by its static signature only
 * (width/height pair pre-filter, then full name+geometry validation). */
static int PairScan(SceneInfo *out)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR addr, maxAddr;
    DWORD start = GetTickCount();
    unsigned char *buf;
    unsigned long long scanned = 0;
    int regions = 0, readFail = 0;
    int found = 0;
    SceneInfo best;
    buf = (unsigned char *)malloc(SCENE_CHUNK + 64);
    if (!buf) return 0;
    BuildTables();
    CollectRegions();
    memset(&best, 0, sizeof(best));
    GetSystemInfo(&si);
    addr = (UINT_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (UINT_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxAddr && !found) {
        SIZE_T scanLen, off;
        if ((GetTickCount() - start) > 8000) break;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
            continue;
        }
        regions++;
        scanLen = mbi.RegionSize;
        if (scanLen > 0x4000000) scanLen = 0x4000000;
        for (off = 0; off + 0x100 < scanLen; off += SCENE_CHUNK) {
            SIZE_T n = scanLen - off;
            SIZE_T i;
            UINT_PTR chunkBase;
            if (n > SCENE_CHUNK) n = SCENE_CHUNK;
            if (n < 0x100) break;
            chunkBase = (UINT_PTR)mbi.BaseAddress + off;
            if (!ReadSelf(chunkBase, buf, n)) { readFail++; break; }
            for (i = 0; i + 0x100 <= n; i += 4) {
                unsigned w, h, namePtr;
                memcpy(&w, buf + i, 4);
                memcpy(&h, buf + i + 4, 4);
                if (h >= 65536 || !g_hSet[h]) continue;
                memcpy(&namePtr, buf + i + 0x10, 4);
                if (namePtr < 0x10000 || !RegionContains((UINT_PTR)namePtr)) continue;
                if (ReadFrameAt((unsigned long)(chunkBase + i), &best)) { found = 1; break; }
            }
            scanned += n;
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    free(buf);
    if (found) {
        *out = best;
        Logf("scene| table scan ok frame=%08lX name=\"%s\" regions=%d bytes=%llu read_fail=%d ms=%lu",
             best.base, best.name, regions, scanned, readFail, (unsigned long)(GetTickCount() - start));
        return 1;
    }
    Logf("scene| table scan not found regions=%d bytes=%llu read_fail=%d ms=%lu",
         regions, scanned, readFail, (unsigned long)(GetTickCount() - start));
    return 0;
}

/* Fast path: once CRunApp for this process is known, the current frame can be
 * read with a few ReadProcessMemory calls (app+0x1D8). This also covers frames
 * whose RunHeader pointer is null, where the anchor scan cannot work and
 * PairScan would need a full memory sweep (14-20 s in practice). */
static int ReadAppFrame(unsigned long app, unsigned long *frameOut, unsigned *stateOut)
{
    unsigned char raw[16];
    unsigned maxIndex, maxHandle, frame;
    if (!app || app < 0x10000 || (app & 3)) return 0;
    if (!ReadSelf((UINT_PTR)app + 0xC4, raw, 12)) return 0;
    memcpy(&maxIndex, raw, 4);
    memcpy(&maxHandle, raw + 4, 4);
    if (maxIndex != (unsigned)kFrameCount || maxHandle < (unsigned)kFrameCount || maxHandle > 4096) return 0;
    if (!ReadSelf((UINT_PTR)app + 0x1D8, &frame, 4)) return 0;
    if (!frame || frame < 0x10000 || (frame & 3)) return 0;
    if (stateOut) {
        if (!ReadSelf((UINT_PTR)app + 0x1E4, raw, 4)) return 0;
        memcpy(stateOut, raw, 4);
    }
    if (frameOut) *frameOut = frame;
    return 1;
}

static int ValidateAppMap(unsigned long app)
{
    unsigned mapPtr = 0;
    unsigned short map[64];
    unsigned char seen[64];
    int i;
    if (!ReadSelf((UINT_PTR)app + 0xCC, &mapPtr, 4)) return 0;
    if (mapPtr < 0x10000 || mapPtr > 0x7FFFFFFF) return 0;
    if (!ReadSelf((UINT_PTR)mapPtr, map, (SIZE_T)kFrameCount * sizeof(unsigned short))) return 0;
    memset(seen, 0, sizeof(seen));
    for (i = 0; i < kFrameCount; ++i) {
        if (map[i] >= (unsigned short)kFrameCount || seen[map[i]]) return 0;
        seen[map[i]] = 1;
    }
    return 1;
}

static void CacheAppFromRh(unsigned long rh)
{
    unsigned app = 0;
    unsigned long frame = 0;
    if (!rh || g_appAddr) return;
    if (!ReadSelf((UINT_PTR)rh + 0x18, &app, 4)) return;
    if (!ReadAppFrame(app, &frame, NULL)) return;
    g_appAddr = app;
    Logf("scene| fast path ready app=%08lX via runheader rh=%08lX frame=%08lX", g_appAddr, rh, frame);
}

/* One-shot fallback: scan private memory for &m_frameMaxIndex (36,36 followed
 * by the handle->index map pointer) to obtain CRunApp when no RunHeader was
 * available at startup. */
static int ScanRunApp(void)
{
    SYSTEM_INFO si;
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR addr, maxAddr;
    DWORD start = GetTickCount();
    unsigned char *buf;
    unsigned long long scanned = 0;
    int regions = 0, hits = 0;
    {
        DWORD now = GetTickCount();
        if (g_lastAppScan && (DWORD)(now - g_lastAppScan) < 1500) return 0;
        g_lastAppScan = now ? now : 1;
    }
    buf = (unsigned char *)malloc(SCENE_CHUNK);
    if (!buf) return 0;
    GetSystemInfo(&si);
    addr = (UINT_PTR)si.lpMinimumApplicationAddress;
    maxAddr = (UINT_PTR)si.lpMaximumApplicationAddress;
    while (addr < maxAddr) {
        SIZE_T scanLen, off;
        if ((GetTickCount() - start) > 8000) break;
        if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) == 0) break;
        if (mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
            addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
            continue;
        }
        regions++;
        scanLen = mbi.RegionSize;
        if (scanLen > 0x4000000) scanLen = 0x4000000;
        for (off = 0; off + 12 <= scanLen; off += SCENE_CHUNK) {
            SIZE_T n = scanLen - off;
            SIZE_T i;
            UINT_PTR chunkBase;
            if (n > SCENE_CHUNK) n = SCENE_CHUNK;
            if (n < 16) break;
            chunkBase = (UINT_PTR)mbi.BaseAddress + off;
            if (!ReadSelf(chunkBase, buf, n)) break;
            for (i = 0; i + 12 <= n; i += 4) {
                unsigned a, b, c;
                unsigned long app;
                memcpy(&a, buf + i, 4);
                memcpy(&b, buf + i + 4, 4);
                memcpy(&c, buf + i + 8, 4);
                if (a != (unsigned)kFrameCount || b < (unsigned)kFrameCount) continue;
                if (c < 0x10000 || c > 0x7FFFFFFF) continue;
                hits++;
                if (i < 0xC4) continue;
                app = (unsigned long)(chunkBase + i - 0xC4);
                if (!ReadAppFrame(app, NULL, NULL)) continue;
                if (!ValidateAppMap(app)) continue;
                g_appAddr = app;
                Logf("scene| fast path ready app=%08lX (scan regions=%d bytes=%llu hits=%d ms=%lu)",
                     g_appAddr, regions, scanned, hits, (unsigned long)(GetTickCount() - start));
                free(buf);
                return 1;
            }
            scanned += n;
        }
        addr += mbi.RegionSize ? mbi.RegionSize : 0x1000;
    }
    free(buf);
    Logf("scene| fast path scan: runapp not found regions=%d bytes=%llu hits=%d ms=%lu",
         regions, scanned, hits, (unsigned long)(GetTickCount() - start));
    return 0;
}

static int Report(SceneInfo *tmp, const char *how)
{
    int changed = (!g_cur.valid) || (g_cur.index != tmp->index) || (strcmp(g_cur.name, tmp->name) != 0);
    if (!changed) return 0;
    g_cur = *tmp;
    Logf("scene| %s idx=%d name=\"%s\" w=%u h=%u layers=%u inst=%u base=%08lX rh=%08lX polls=%lu",
         how, g_cur.index, g_cur.name, g_cur.w, g_cur.h, g_cur.layers, g_cur.instances,
         g_cur.base, g_rhPtr, g_polls);
    return 1;
}

void scene::Init(void)
{
    memset(&g_cur, 0, sizeof(g_cur));
    g_cur.index = -1;
    g_cachedBase = 0;
    g_anchorStruct = 0;
    g_rhPtr = 0;
    g_appAddr = 0;
    g_appFailSince = 0;
    g_lastAppScan = 0;
    g_lastScanTick = 0;
    g_polls = 0;
    g_tablesReady = 0;
    BuildTables();
    Logf("scene| init: %d frame definitions loaded", kFrameCount);
}

int scene::Poll(void)
{
    SceneInfo tmp;
    DWORD now;
    unsigned long frame = 0;
    unsigned appState = 0;
    g_polls++;
    now = GetTickCount();
    /* Fast path: CRunApp+0x1D8 is the authoritative current-frame pointer and
     * works for every frame (including rh==0 frames where AnchorScan fails). */
    if (g_appAddr) {
        if (ReadAppFrame(g_appAddr, &frame, &appState)) {
            g_appFailSince = 0;
            if (ReadFrameAt(frame, &tmp)) {
                unsigned rh = 0;
                g_cachedBase = frame;
                if (ReadSelf((UINT_PTR)frame + 0x7C, &rh, 4) && rh) g_rhPtr = rh;
                Report(&tmp, "fast");
                return 1;
            }
        } else if (!g_appFailSince) {
            g_appFailSince = now ? now : 1;
        }
        if (g_appFailSince && (now - g_appFailSince) > 5000) {
            Logf("scene| fast path lost app=%08lX; falling back to scans", g_appAddr);
            g_appAddr = 0;
            g_appFailSince = 0;
        }
        if (g_appAddr) return 0;  /* transient: frame pointer not ready yet */
    }
    if (g_cachedBase && ReadFrameAt(g_cachedBase, &tmp)) {
        unsigned rh = 0;
        if (ReadSelf((UINT_PTR)g_cachedBase + 0x7C, &rh, 4) && rh) {
            g_rhPtr = rh;
            if (!g_appAddr) CacheAppFromRh(rh);
        }
        if (!g_appAddr) ScanRunApp();   /* throttled; obtains CRunApp once available */
        Report(&tmp, "changed");
        return 1;
    }
    if ((now - g_lastScanTick) < SCENE_RESCAN_MIN_MS) return 0;
    g_lastScanTick = now;
    if (AnchorScan(&tmp, &g_anchorStruct, &g_rhPtr)) {
        g_cachedBase = tmp.base;
        CacheAppFromRh(g_rhPtr);
        Report(&tmp, "detected");
        return 1;
    }
    if (PairScan(&tmp)) {
        unsigned long rh = 0;
        g_cachedBase = tmp.base;
        if (ReadSelf((UINT_PTR)tmp.base + 0x7C, &rh, 4) && rh) g_rhPtr = rh;
        CacheAppFromRh(g_rhPtr);
        if (!g_appAddr) ScanRunApp();
        Report(&tmp, "detected");
        return 1;
    }
    if (g_cur.valid) {
        Logf("scene| signature lost (base=%08lX polls=%lu); retrying", g_cur.base, g_polls);
        g_cur.valid = 0;
        g_cur.index = -1;
        g_cur.name[0] = 0;
        g_cur.base = 0;
    }
    return 0;
}

const SceneInfo *scene::Current(void) { return &g_cur; }
int scene::FrameCount(void) { return kFrameCount; }
const char *scene::FrameName(int index)
{
    if (index < 0 || index >= kFrameCount) return NULL;
    return kFrameTable[index].name;
}
unsigned long scene::RunHeaderPtr(void) { return g_rhPtr; }

/* -------------------------------------------------------------------------
 * Development-only frame warp (F9 menu in overlay.cpp). Mirrors the protocol
 * verified with tools/p5_jump.py (docs/RUNTIME_SCENE.md §5):
 *   RunHeader.rhQuitParam (rh+0x38, dword) = target frame handle
 *   RunHeader.rhQuit      (rh+0x30, short) = 3
 * Writing the two fields makes the runtime release the current frame and load
 * handle_to_index[handle]. Never part of the plugin API.
 * ---------------------------------------------------------------------- */

int scene::HandleCount(void) { return kFrameCount; }

static int ReadHandleMap(unsigned long app, unsigned short *map)
{
    unsigned mapPtr = 0;
    if (!app) return 0;
    if (!ReadSelf((UINT_PTR)app + 0xCC, &mapPtr, 4)) return 0;
    if (mapPtr < 0x10000 || mapPtr > 0x7FFFFFFF) return 0;
    if (!ReadSelf((UINT_PTR)mapPtr, map, (SIZE_T)kFrameCount * sizeof(unsigned short))) return 0;
    return 1;
}

int scene::HandleToFrameIndex(int handle)
{
    unsigned short map[64];
    if (handle < 0 || handle >= kFrameCount) return -1;
    if (!ReadHandleMap(g_appAddr, map)) return -1;
    if (map[handle] >= (unsigned short)kFrameCount) return -1;
    return (int)map[handle];
}

const char *scene::HandleFrameName(int handle)
{
    int idx = HandleToFrameIndex(handle);
    return idx >= 0 ? FrameName(idx) : NULL;
}

int scene::WarpToHandle(int handle)
{
    unsigned long app = g_appAddr, frame = 0, rh = 0;
    unsigned short map[64];
    unsigned rhApp = 0;
    LONG qp;
    short quit = 3;
    int attempt, idx;
    if (handle < 0 || handle >= kFrameCount) return -1;
    if (!app) {
        Logf("warp| rejected: CRunApp unknown (scene fast path not ready)");
        return -2;
    }
    /* During frame transitions m_Frame is 0 for a moment: retry briefly. */
    for (attempt = 0; attempt < 5; ++attempt) {
        if (!ReadAppFrame(app, &frame, NULL)) { Sleep(10); continue; }
        if (!ReadSelf((UINT_PTR)frame + 0x7C, &rh, 4) || !rh) { Sleep(10); continue; }
        if (!ReadSelf((UINT_PTR)rh + 0x18, &rhApp, 4) || rhApp != (unsigned)app) { Sleep(10); continue; }
        break;
    }
    if (!frame || !rh) {
        Logf("warp| rejected: no live RunHeader app=%08lX frame=%08lX rh=%08lX", app, frame, rh);
        return -3;
    }
    if (!ReadHandleMap(app, map)) {
        Logf("warp| rejected: handle map unreadable app=%08lX", app);
        return -4;
    }
    idx = (int)map[handle];
    if (idx < 0 || idx >= kFrameCount) {
        Logf("warp| rejected: mapping[%d]=%d out of range", handle, idx);
        return -4;
    }
    qp = (LONG)handle;
    if (!WriteSelf((UINT_PTR)rh + 0x38, &qp, 4)) {
        Logf("warp| rejected: rhQuitParam write failed rh=%08lX err=%lu", rh, GetLastError());
        return -5;
    }
    if (!WriteSelf((UINT_PTR)rh + 0x30, &quit, 2)) {
        Logf("warp| rejected: rhQuit write failed rh=%08lX err=%lu", rh, GetLastError());
        return -6;
    }
    Logf("warp| request handle=%d index=%d rh=%08lX app=%08lX from=\"%s\"", handle, idx, rh, app,
         g_cur.name[0] ? g_cur.name : "?");
    return 1;
}
