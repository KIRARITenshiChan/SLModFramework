/* Phase 5 game state adapter v0 (read-only).
 * See state.h and docs/RUNTIME_STATE.md for the layout evidence. */
#include "common.h"
#include "scene.h"
#include "state.h"

#include <stdlib.h>
#include <string.h>

#define STATE_MAX_OI        512
#define STATE_MAX_COUNTERS  64
#define STATE_STRIDE        146      /* objInfoList entry size (measured) */
#define STATE_OI_NAME_OFF   0x5A     /* UTF-16LE OI name inside the entry (measured) */
#define STATE_VALUE_OFF     0x20A    /* current counter value, encoded ~int16 (measured) */
#define STATE_SCAN_CHUNK    0x40000
#define STATE_SCAN_LIMIT    0x40000000ULL   /* 1 GiB: the object heap lives past 80 MB */
#define STATE_MAX_OBJECTS   128
#define STATE_MAX_ALTERABLES 26
#define STATE_ALT_OFF       0x80     /* headerObject.hoOffsetValue (measured) */
#define STATE_CVALUE_SIZE   16       /* sizeof(CValue) with pack(2) */
#define STATE_RVAL_PTR_OFF  0x00     /* rVal.rvpValues */
#define STATE_RVAL_CNT_OFF  0x04     /* rvFree1[0] observed as the value count (26) */
/* RunObject layout with pack(2): headerObject 0xB8 + rCom 120 + rMvt 158 ->
 * rAni at 0x1CE (size 80) -> rSpr at 0x21E (36) -> rVal at 0x242, which matches
 * the measured hoOffsetValue (0x80 = 0x242) exactly, so the chain is validated.
 * Field order taken from Cncf.h rAni and cross-checked against CRAni.cs. */
#define STATE_ANIM_FORCED_OFF       0x1CE  /* rAni.raAnimForced (0 = normal, anim+1 = forced) */
#define STATE_ANIM_DIRFORCED_OFF    0x1D2  /* rAni.raAnimDirForced */
#define STATE_ANIM_SPEEDFORCED_OFF  0x1D6  /* rAni.raAnimSpeedForced */
#define STATE_ANIM_ON_OFF           0x1DE  /* rAni.raAnimOn (current sequence) */
#define STATE_ANIM_FRAME_OFF        0x212  /* rAni.raAnimFrame */
#define STATE_ANIM_FRAMES_OFF       0x216  /* rAni.raAnimNumberOfFrame */
#define STATE_ANIM_MIN_SIZE         0x220  /* rAni ends at 0x21E, require its full extent */
/* headerObject on-screen position/size (frame coordinates, int variants). */
#define STATE_X_OFF         0x4C
#define STATE_Y_OFF         0x54
#define STATE_IMG_W_OFF     0x60
#define STATE_IMG_H_OFF     0x64
/* RunHeader.rhObjectList (0x1278) is an array of { headerObject*, routine }
 * entries indexed by object number; rhNObjects lives at rh+0x3C. The published
 * map is built once per scene, so objects Created by events after that build
 * (frame-31 "Active 19" / Bonnet is the first case) are only reachable from
 * this live list. Offsets validated against Cncf.h with pack(2) using the
 * known anchors rhApp=0x18, rhFrame=0x1C, rhQuit=0x30, rhQuitParam=0x38,
 * rhNumberOi=0x54, rhOiList=0x58 (all match the live runtime). */
#define STATE_OBJLIST_OFF   0x1278
#define STATE_NOBJ_OFF      0x3C
#define STATE_MAX_LIVE_OBJS 4096

struct OiEntry {
    unsigned char type;
    short handle;      /* oilOi: the object-info handle stored in every object's hoOi */
    char name[40];
};

struct CounterSlot {
    char name[40];
    unsigned long obj;
    int value;
    int valid;
};

struct AlterableSlot {
    char name[40];
    unsigned long obj;
    int type;
    unsigned long rvalAddr;        /* obj + hoOffsetValue, inline rVal; 0 = not watched */
    int count;                     /* number of alterable values (<= 26) */
    int valid;                     /* last read succeeded */
    int values[STATE_MAX_ALTERABLES];
};

struct Published {
    int count;
    int mapScene;                  /* scene index this map was built for */
    int objCount;
    CounterSlot slots[STATE_MAX_COUNTERS];
    AlterableSlot objs[STATE_MAX_OBJECTS];
};

static OiEntry g_ois[STATE_MAX_OI];
static int g_oiCount;
static unsigned long g_oiList;
static Published g_pub[2];
static volatile LONG g_pubCur = 0;
static int g_status;              /* 0 init, 1 ok, 2 no RunHeader, 3 scene lost, 4 gate */
static int g_sceneIndex = -1;
static unsigned long g_mapFrame;
static unsigned long g_mapRh;
static int g_mapScene = -1;       /* frame index the published map belongs to */
static int pendingScene = -1;
static int g_objectCount;
static int g_logBudget;
static int g_valuesReady;          /* 1 once PollValues() ran for the published map */
static int g_liveLogBudget;        /* remaining "live object fallback" log lines */

static const char *kWatch[] = {
    "Freddy", "danger level", "danger level 2", "danger level 3", "danger level 4",
    "danger lv", "got you", "beat", "interface", "jumpscare", "restart 1", "create scare",
    "final stage", "hour", "power left", "side", "near", "was here", "going here",
    "viewing cam", "count seconds", NULL
};

/* Objects whose Active alterable array is polled every tick. Keep this list
 * tiny: each entry costs two ReadProcessMemory calls per poll. */
static const char *kAlterableWatch[] = {
    "Ennard", "flip it", NULL
};

static int ReadSelf(unsigned long addr, void *out, size_t len)
{
    SIZE_T got = 0;
    if (!addr) return 0;
    if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(ULONG_PTR)addr, out, len, &got)) return 0;
    return got == len;
}

static int ReadU32(unsigned long addr, unsigned long *out)
{
    unsigned long v = 0;
    if (!ReadSelf(addr, &v, 4)) return 0;
    *out = v;
    return 1;
}

static void WideToAscii(const unsigned short *w, char *out, size_t cap)
{
    size_t n = 0;
    int i;
    for (i = 0; i < 24 && n + 1 < cap; ++i) {
        unsigned short c = w[i];
        if (!c) break;
        out[n++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[n] = 0;
}

static int IsWatch(const char *name)
{
    int i;
    for (i = 0; kWatch[i]; ++i)
        if (_stricmp(name, kWatch[i]) == 0) return 1;
    return 0;
}

static int IsAlterableWatch(const char *name)
{
    int i;
    for (i = 0; kAlterableWatch[i]; ++i)
        if (_stricmp(name, kAlterableWatch[i]) == 0) return 1;
    return 0;
}

/* Read the CValue array behind one watched Active object. The rVal header
 * (rvpValues pointer + count) is re-read each poll because the allocator may
 * move the value block; a failed read leaves valid=0 so stale numbers are
 * never published as confident. */
static int ReadAlterables(AlterableSlot *os)
{
    unsigned char head[8];
    unsigned char buf[STATE_MAX_ALTERABLES * STATE_CVALUE_SIZE];
    unsigned long ptr = 0;
    int cnt = 0, i;
    os->valid = 0;
    if (!os->rvalAddr || !ReadSelf(os->rvalAddr, head, sizeof(head))) return 0;
    memcpy(&ptr, head + STATE_RVAL_PTR_OFF, 4);
    memcpy(&cnt, head + STATE_RVAL_CNT_OFF, 4);
    if (!ptr || ptr < 0x10000 || cnt <= 0 || cnt > STATE_MAX_ALTERABLES) return 0;
    if (!ReadSelf(ptr, buf, (size_t)cnt * STATE_CVALUE_SIZE)) return 0;
    for (i = 0; i < cnt; ++i) {
        unsigned int type = 0;
        int v = 0;
        memcpy(&type, buf + i * STATE_CVALUE_SIZE, 4);
        if (type == 0) {
            memcpy(&v, buf + i * STATE_CVALUE_SIZE + 8, 4);
        } else if (type == 2) {
            double d = 0.0;
            memcpy(&d, buf + i * STATE_CVALUE_SIZE + 8, 8);
            if (d > -1000000000.0 && d < 1000000000.0) v = (int)d;
            else return 0;
        } else {
            /* String/other CValue types have no numeric alterable meaning. */
            return 0;
        }
        os->values[i] = v;
    }
    os->count = cnt;
    os->valid = 1;
    return 1;
}

static int ValidateObject(unsigned long obj, int *oiOut)
{
    unsigned char hdr[0x24];
    short hoNumber, hoOi, hoType;
    int hoSize, i, idx = -1;
    unsigned long oiListPtr, expect;
    if (!ReadSelf(obj, hdr, sizeof(hdr))) return 0;
    memcpy(&hoNumber, hdr + 0x00, 2);
    memcpy(&hoSize, hdr + 0x04, 4);
    memcpy(&hoOi, hdr + 0x12, 2);
    memcpy(&hoType, hdr + 0x18, 2);
    memcpy(&oiListPtr, hdr + 0x1C, 4);
    if (hoNumber <= 0 || hoSize < 0x40 || hoSize > 0x8000) return 0;
    /* hoOi is the OI *handle* (oilOi), not the table index: look it up. */
    for (i = 0; i < g_oiCount; ++i) {
        if (g_ois[i].handle == hoOi) { idx = i; break; }
    }
    if (idx < 0) return 0;
    if ((unsigned char)hoType != g_ois[idx].type) return 0;
    expect = g_oiList + (unsigned long)idx * STATE_STRIDE;
    if (oiListPtr != expect) return 0;      /* object must belong to this frame's OI table */
    *oiOut = idx;
    return 1;
}

static int BuildMap(unsigned long frameBase)
{
    DWORD t0 = GetTickCount();
    unsigned long rh = 0, app = 0, numberOiRaw = 0;
    int numberOi, i, objs = 0, counters = 0, stored = 0, regions = 0;
    unsigned char *buf;
    ULONG_PTR addr = 0x10000;
    unsigned long long scanned = 0;
    Published *dst;

    g_oiCount = 0;
    g_oiList = 0;
    if (!ReadU32(frameBase + 0x7C, &rh) || !rh) {
        Logf("state| no runheader: frame=%08lX frame+0x7C=0", frameBase);
        return 0;
    }
    {
        unsigned long back = 0;
        if (!ReadU32(rh + 0x1C, &back) || back != frameBase) {
            Logf("state| runheader validation failed rh=%08lX rh+0x1C=%08lX frame=%08lX", rh, back, frameBase);
            return 0;
        }
    }
    ReadU32(rh + 0x18, &app);
    if (!ReadU32(rh + 0x54, &numberOiRaw) || !ReadU32(rh + 0x58, &g_oiList) || !g_oiList) {
        Logf("state| runheader field read failed rh=%08lX", rh);
        return 0;
    }
    numberOi = (int)numberOiRaw;
    if (numberOi <= 0 || numberOi > STATE_MAX_OI) {
        Logf("state| unexpected numberOi=%d rh=%08lX", numberOi, rh);
        return 0;
    }
    for (i = 0; i < numberOi; ++i) {
        unsigned char entry[STATE_STRIDE];
        if (!ReadSelf(g_oiList + (unsigned long)i * STATE_STRIDE, entry, sizeof(entry))) break;
        g_ois[i].type = entry[4];   /* short oilType; all observed types fit in a byte */
        memcpy(&g_ois[i].handle, entry, 2);
        WideToAscii((const unsigned short *)(entry + STATE_OI_NAME_OFF), g_ois[i].name, sizeof(g_ois[i].name));
    }
    if (i < numberOi) {
        Logf("state| oi table read failed at %d/%d oiList=%08lX", i, numberOi, g_oiList);
        return 0;
    }
    g_oiCount = numberOi;

    dst = &g_pub[1 - (int)g_pubCur];
    dst->objCount = 0;
    buf = (unsigned char *)malloc(STATE_SCAN_CHUNK);
    if (!buf) {
        Logf("state| scan buffer allocation failed");
        return 0;
    }
    for (;;) {
        MEMORY_BASIC_INFORMATION mbi;
        DWORD prot;
        ULONG_PTR base;
        SIZE_T size, off;
        if (VirtualQueryEx(GetCurrentProcess(), (LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi)) break;
        base = (ULONG_PTR)mbi.BaseAddress;
        size = mbi.RegionSize;
        prot = mbi.Protect & 0xFF;
        if (mbi.State == MEM_COMMIT && size &&
            (prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
             prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY)) {
            ++regions;
            for (off = 0; off < size; off += STATE_SCAN_CHUNK) {
                SIZE_T n = size - off;
                SIZE_T got = 0;
                SIZE_T k;
                if (n > STATE_SCAN_CHUNK) n = STATE_SCAN_CHUNK;
                if (ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(base + off), buf, n, &got) && got >= 8) {
                    scanned += got;
                    for (k = 0; k + 4 <= got; k += 4) {
                        unsigned long v;
                        int oi = 0;
                        memcpy(&v, buf + k, 4);
                        if (v != rh) continue;
                        if (!ValidateObject((unsigned long)(base + off + k - 8), &oi)) continue;
                        ++objs;
                        if (g_ois[oi].type == 7 && counters < STATE_MAX_COUNTERS) {
                            CounterSlot *cs = &dst->slots[counters];
                            lstrcpynA(cs->name, g_ois[oi].name, sizeof(cs->name));
                            cs->obj = (unsigned long)(base + off + k - 8);
                            cs->value = 0;
                            cs->valid = 0;
                            ++counters;
                        }
                        if (stored < STATE_MAX_OBJECTS) {
                            AlterableSlot *os = &dst->objs[stored];
                            memset(os, 0, sizeof(*os));
                            lstrcpynA(os->name, g_ois[oi].name, sizeof(os->name));
                            os->obj = (unsigned long)(base + off + k - 8);
                            os->type = g_ois[oi].type;
                            if (os->type == 2 && IsAlterableWatch(os->name)) {
                                unsigned char oh[0x88];
                                int hoSize = 0;
                                unsigned long valOff = 0;
                                if (ReadSelf(os->obj, oh, sizeof(oh))) {
                                    memcpy(&hoSize, oh + 0x04, 4);
                                    memcpy(&valOff, oh + STATE_ALT_OFF, 4);
                                    if (hoSize >= 0x40 && hoSize <= 0x8000 &&
                                        valOff >= 0x100 && valOff + 0xA0 <= (unsigned long)hoSize) {
                                        os->rvalAddr = os->obj + valOff;
                                        ReadAlterables(os);
                                    }
                                }
                            }
                            ++stored;
                        }
                    }
                }
                if (scanned > STATE_SCAN_LIMIT) break;
            }
        }
        addr = base + size;
        if (addr <= base || scanned > STATE_SCAN_LIMIT) break;
    }
    free(buf);

    dst->count = counters;
    dst->objCount = stored;
    dst->mapScene = g_sceneIndex;
    g_mapFrame = frameBase;
    g_mapRh = rh;
    g_objectCount = objs;
    g_valuesReady = 0;              /* values are stale until PollValues() runs */
    g_liveLogBudget = 8;
    g_pubCur = 1 - (int)g_pubCur;   /* publish the freshly built map */
    Logf("state| map frame=%08lX rh=%08lX app=%08lX oi=%d objs=%d counters=%d stored=%d regions=%d bytes=%llu ms=%lu",
         frameBase, rh, app, g_oiCount, objs, counters, stored, regions, scanned,
         (unsigned long)(GetTickCount() - t0));
    for (i = 0; i < counters && i < 12; ++i)
        Logf("state| counter[%02d] %s obj=%08lX", i, dst->slots[i].name, dst->slots[i].obj);
    return 1;
}

static int PollValues(void)
{
    Published *p = &g_pub[(int)g_pubCur];
    int i, changed = 0;
    for (i = 0; i < p->count; ++i) {
        CounterSlot *cs = &p->slots[i];
        unsigned short u = 0;
        int s, v, old, wasValid;
        if (!ReadSelf(cs->obj + STATE_VALUE_OFF, &u, 2)) {
            cs->valid = 0;
            continue;
        }
        s = (short)u;
        v = ~s;
        if (v <= -1000000 || v >= 1000000) {
            cs->valid = 0;
            continue;
        }
        wasValid = cs->valid;
        old = cs->value;
        cs->valid = 1;
        cs->value = v;
        if (!wasValid) {
            if (g_logBudget < 4 && IsWatch(cs->name)) {
                Logf("state| counter %s initial=%d", cs->name, v);
                ++g_logBudget;
            }
        } else if (old != v) {
            ++changed;
            if (g_logBudget < 4 && IsWatch(cs->name)) {
                Logf("state| counter %s %d -> %d", cs->name, old, v);
                ++g_logBudget;
            }
        }
    }
    for (i = 0; i < p->objCount; ++i) {
        AlterableSlot *os = &p->objs[i];
        int wasValid, old3;
        if (!os->rvalAddr) continue;
        wasValid = os->valid;
        old3 = (os->count > 3) ? os->values[3] : 0;
        ReadAlterables(os);
        if (wasValid && os->valid && os->count > 3 && os->values[3] != old3 &&
            _stricmp(os->name, "Ennard") == 0 && g_logBudget < 4) {
            Logf("state| Ennard[3] %d -> %d", old3, os->values[3]);
            ++g_logBudget;
        }
    }
    g_valuesReady = 1;
    return changed;
}

namespace state {

void Init(void)
{
    g_status = 0;
    g_sceneIndex = -1;
    g_mapFrame = 0;
    g_mapRh = 0;
    g_mapScene = -1;
    pendingScene = -1;
    g_objectCount = 0;
    g_oiCount = 0;
    g_oiList = 0;
    g_pubCur = 0;
    g_pub[0].count = 0;
    g_pub[0].objCount = 0;
    g_pub[0].mapScene = -1;
    g_pub[1].count = 0;
    g_pub[1].objCount = 0;
    g_pub[1].mapScene = -1;
    g_valuesReady = 0;
    Logf("state| init: counter value = ~int16(obj+0x%03X), oi stride=%d name_off=0x%02X",
         STATE_VALUE_OFF, STATE_STRIDE, STATE_OI_NAME_OFF);
}

int Poll(void)
{
    const SceneInfo *sc;
    static unsigned long pendingFrame;

    if (g_fw.gate != 1) {
        if (g_status != 4) Logf("state| disabled: version gate is not ok (%s)", GateStatusName());
        g_status = 4;
        return 0;
    }
    sc = scene::Current();
    if (!sc || !sc->valid) {
        if (g_status != 3) Logf("state| paused: scene unknown, no frame map");
        g_status = 3;
        return 0;
    }
    g_sceneIndex = sc->index;
    g_logBudget = 0;
    if (sc->base != g_mapFrame || sc->index != g_mapScene) {
        /* Debounce: build the object map only when the same frame survived one poll.
           Frame index is compared too: the runtime reuses CRunFrame memory and some frames
           have rhPtr=0, so a base/rh comparison alone would keep a stale map. */
        if (sc->base == pendingFrame && sc->index == pendingScene) {
            if (BuildMap(sc->base)) { g_status = 1; g_mapScene = sc->index; }
            else g_status = 2;
            pendingFrame = 0;
            pendingScene = -1;
        } else {
            pendingFrame = sc->base;
            pendingScene = sc->index;
        }
        return 0;
    }
    /* The game reuses CRunFrame memory: the same frame address can belong to a new frame.
       Compare the RunHeader as well so the object map is rebuilt after every frame change. */
    {
        unsigned long rhNow = 0;
        if (ReadU32(sc->base + 0x7C, &rhNow) && rhNow && rhNow != g_mapRh) {
            Logf("state| frame memory reused, rebuilding map (rh %08lX -> %08lX)", g_mapRh, rhNow);
            if (BuildMap(sc->base)) { g_status = 1; g_mapScene = sc->index; }
            else g_status = 2;
            return 0;
        }
    }
    return PollValues();
}

int Status(void) { return g_status; }
int MapScene(void) { return g_pub[(int)g_pubCur].mapScene; }
int ValuesReady(void) { return g_valuesReady; }

const char *StatusName(void)
{
    switch (g_status) {
    case 0: return "初始化";
    case 1: return "ok";
    case 2: return "未知(无 RunHeader)";
    case 3: return "场景未知/暂停";
    case 4: return "版本门禁未通过";
    default: return "?";
    }
}

int SceneIndex(void) { return g_sceneIndex; }
int ObjectCount(void) { return g_objectCount; }

int CounterCount(void) { return g_pub[(int)g_pubCur].count; }

const char *CounterName(int i)
{
    const Published *p = &g_pub[(int)g_pubCur];
    return (i >= 0 && i < p->count) ? p->slots[i].name : NULL;
}

int CounterValue(int i)
{
    const Published *p = &g_pub[(int)g_pubCur];
    return (i >= 0 && i < p->count) ? p->slots[i].value : 0;
}

int CounterValid(int i)
{
    const Published *p = &g_pub[(int)g_pubCur];
    return (i >= 0 && i < p->count) ? p->slots[i].valid : 0;
}

int FindCounter(const char *name, int *outValue)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name) return 0;
    for (i = 0; i < p->count; ++i) {
        if (_stricmp(p->slots[i].name, name) != 0) continue;
        if (!p->slots[i].valid) return 0;
        if (outValue) *outValue = p->slots[i].value;
        return 1;
    }
    return 0;
}

static int WriteSelf(unsigned long addr, const void *src, size_t len)
{
    SIZE_T wrote = 0;
    if (!addr || !len) return 0;
    if (!WriteProcessMemory(GetCurrentProcess(), (LPVOID)(ULONG_PTR)addr, src, len, &wrote)) return 0;
    return wrote == len;
}

static int FindCounterSlot(const char *name)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name) return -1;
    for (i = 0; i < p->count; ++i)
        if (_stricmp(p->slots[i].name, name) == 0) return i;
    return -1;
}

/* Resolve name -> inline rVal address for an Active object, re-reading
 * hoOffsetValue every time because object blocks can be reallocated. */
/* Resolve a name to a live object through RunHeader.rhObjectList. Objects
 * created after the scene map was built are missing from the published
 * snapshot; this covers them without a full memory scan. The same OI-handle
 * validation as ValidateObject() is applied, so a stale rh (scene changed
 * since the map build) simply returns 0. */
static int LiveObjectByName(const char *name, int wantType, unsigned long *outObj)
{
    unsigned long rh = g_mapRh, listPtr = 0, nobjRaw = 0, back = 0;
    int i, count;
    if (!name || !outObj || !rh) return 0;
    if (!ReadU32(rh + 0x1C, &back) || back != g_mapFrame) return 0;
    if (!ReadU32(rh + STATE_NOBJ_OFF, &nobjRaw) || !nobjRaw || nobjRaw > STATE_MAX_LIVE_OBJS) return 0;
    if (!ReadU32(rh + STATE_OBJLIST_OFF, &listPtr) || listPtr < 0x10000) return 0;
    count = (int)nobjRaw;
    for (i = 0; i < count; ++i) {
        unsigned long entry[2], obj;
        unsigned char hdr[0x24];
        short hoOi = 0, hoType = 0;
        int k, oi = -1;
        if (!ReadSelf(listPtr + (unsigned long)i * 8, entry, sizeof(entry))) break;
        obj = entry[0];
        if (obj < 0x10000) continue;
        if (!ReadSelf(obj, hdr, sizeof(hdr))) continue;
        memcpy(&hoOi, hdr + 0x12, 2);
        memcpy(&hoType, hdr + 0x18, 2);
        for (k = 0; k < g_oiCount; ++k) {
            if (g_ois[k].handle == hoOi) { oi = k; break; }
        }
        if (oi < 0 || (unsigned char)hoType != g_ois[oi].type) continue;
        if (wantType >= 0 && (unsigned char)hoType != (unsigned char)wantType) continue;
        if (_stricmp(g_ois[oi].name, name) != 0) continue;
        *outObj = obj;
        return 1;
    }
    return 0;
}

static int ObjectRval(const char *name, unsigned long *rvalAddr)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name) return 0;
    for (i = 0; i < p->objCount; ++i) {
        unsigned char oh[0x88];
        int hoSize = 0;
        unsigned long valOff = 0, obj;
        if (_stricmp(p->objs[i].name, name) != 0) continue;
        if (p->objs[i].type != 2) return 0;
        obj = p->objs[i].obj;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        memcpy(&valOff, oh + STATE_ALT_OFF, 4);
        if (hoSize < 0x40 || hoSize > 0x8000) return 0;
        if (valOff < 0x100 || valOff + 0xA0 > (unsigned long)hoSize) return 0;
        *rvalAddr = obj + valOff;
        return 1;
    }
    /* Objects created after the scene map (e.g. frame-31 Bonnet walking body)
     * are only in the live object list: fall back to it. */
    {
        unsigned long obj = 0, valOff = 0;
        unsigned char oh[0x88];
        int hoSize = 0;
        if (!LiveObjectByName(name, 2, &obj)) return 0;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        memcpy(&valOff, oh + STATE_ALT_OFF, 4);
        if (hoSize < 0x40 || hoSize > 0x8000) return 0;
        if (valOff < 0x100 || valOff + 0xA0 > (unsigned long)hoSize) return 0;
        *rvalAddr = obj + valOff;
        return 1;
    }
}

int ReadCounterLive(const char *name, int *outValue)
{
    int i = FindCounterSlot(name);
    const Published *p = &g_pub[(int)g_pubCur];
    unsigned short u = 0;
    short s;
    int v;
    if (i < 0) return 0;
    if (!ReadSelf(p->slots[i].obj + STATE_VALUE_OFF, &u, 2)) return 0;
    s = (short)u;
    v = ~s;
    if (v <= -1000000 || v >= 1000000) return 0;
    if (outValue) *outValue = v;
    return 1;
}

int WriteCounter(const char *name, int value)
{
    int i = FindCounterSlot(name);
    const Published *p = &g_pub[(int)g_pubCur];
    short raw;
    if (i < 0) return 0;
    if (value <= -32767 || value > 32768) return 0;
    raw = (short)~value;
    if (!WriteSelf(p->slots[i].obj + STATE_VALUE_OFF, &raw, sizeof(raw))) return 0;
    return 1;
}

int WriteAlterable(const char *name, int index, int value)
{
    unsigned long rvalAddr = 0;
    unsigned char head[8];
    unsigned long ptr = 0;
    unsigned int type = 0;
    int cnt = 0, v = value;
    if (index < 0 || index >= STATE_MAX_ALTERABLES) return 0;
    if (!ObjectRval(name, &rvalAddr)) return 0;
    if (!ReadSelf(rvalAddr, head, sizeof(head))) return 0;
    memcpy(&ptr, head + STATE_RVAL_PTR_OFF, 4);
    memcpy(&cnt, head + STATE_RVAL_CNT_OFF, 4);
    if (!ptr || ptr < 0x10000 || cnt <= 0 || cnt > STATE_MAX_ALTERABLES) return 0;
    if (index >= cnt) return 0;
    if (!ReadSelf(ptr + (unsigned long)index * STATE_CVALUE_SIZE, &type, 4)) return 0;
    if (type != 0) return 0;   /* only numeric long values are safe to overwrite */
    return WriteSelf(ptr + (unsigned long)index * STATE_CVALUE_SIZE + 8, &v, 4);
}

/* P8 QoL: read/write an Active object's frame-space position (hoX/hoY).
 * Used to drive the frame-27 flip trigger the way the game does: the game's
 * "trigger cursor" object is tested against "flip trigger" every tick, so
 * placing the cursor rectangle inside the trigger fires the real event chain
 * (Create Active 8 + forced animation + Speaker #299/#300) instead of just
 * snapping flip it[0]. */
int ReadObjectRect(const char *name, int *outX, int *outY, int *outW, int *outH)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name) return 0;
    for (i = 0; i < p->objCount; ++i) {
        unsigned char oh[0x88];
        int hoSize = 0, x = 0, y = 0, w = 0, h = 0;
        unsigned long obj;
        if (_stricmp(p->objs[i].name, name) != 0) continue;
        obj = p->objs[i].obj;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        if (hoSize < 0x40 || hoSize > 0x8000) return 0;
        memcpy(&x, oh + STATE_X_OFF, 4);
        memcpy(&y, oh + STATE_Y_OFF, 4);
        memcpy(&w, oh + STATE_IMG_W_OFF, 4);
        memcpy(&h, oh + STATE_IMG_H_OFF, 4);
        if (x <= -100000 || x >= 100000 || y <= -100000 || y >= 100000) return 0;
        if (w < 0 || w > 100000 || h < 0 || h > 100000) return 0;
        if (outX) *outX = x;
        if (outY) *outY = y;
        if (outW) *outW = w;
        if (outH) *outH = h;
        return 1;
    }
    {
        unsigned long obj = 0;
        unsigned char oh[0x88];
        int hoSize = 0, x = 0, y = 0, w = 0, h = 0;
        if (!LiveObjectByName(name, -1, &obj)) return 0;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        if (hoSize < 0x40 || hoSize > 0x8000) return 0;
        memcpy(&x, oh + STATE_X_OFF, 4);
        memcpy(&y, oh + STATE_Y_OFF, 4);
        memcpy(&w, oh + STATE_IMG_W_OFF, 4);
        memcpy(&h, oh + STATE_IMG_H_OFF, 4);
        if (x <= -100000 || x >= 100000 || y <= -100000 || y >= 100000) return 0;
        if (w < 0 || w > 100000 || h < 0 || h > 100000) return 0;
        if (outX) *outX = x;
        if (outY) *outY = y;
        if (outW) *outW = w;
        if (outH) *outH = h;
        if (g_liveLogBudget > 0) {
            --g_liveLogBudget;
            Logf("state| live rect fallback name=%s obj=%08lX x=%d y=%d w=%d h=%d", name, obj, x, y, w, h);
        }
        return 1;
    }
}

int WriteObjectPosition(const char *name, int x, int y)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name) return 0;
    if (x <= -100000 || x >= 100000 || y <= -100000 || y >= 100000) return 0;
    for (i = 0; i < p->objCount; ++i) {
        unsigned char oh[0x88];
        int hoSize = 0;
        unsigned long obj;
        if (_stricmp(p->objs[i].name, name) != 0) continue;
        obj = p->objs[i].obj;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        if (hoSize < 0x40 || hoSize > 0x8000) return 0;
        if (!WriteSelf(obj + STATE_X_OFF, &x, 4)) return 0;
        if (!WriteSelf(obj + STATE_Y_OFF, &y, 4)) return 0;
        return 1;
    }
    {
        unsigned long obj = 0;
        unsigned char oh[0x88];
        int hoSize = 0;
        if (!LiveObjectByName(name, -1, &obj)) return 0;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        if (hoSize < 0x40 || hoSize > 0x8000) return 0;
        if (!WriteSelf(obj + STATE_X_OFF, &x, 4)) return 0;
        if (!WriteSelf(obj + STATE_Y_OFF, &y, 4)) return 0;
        return 1;
    }
}

int ReadAlterableLive(const char *name, int index, int *outValue)
{
    unsigned long rvalAddr = 0;
    unsigned char head[8];
    unsigned long ptr = 0;
    int cnt = 0;
    int type = 0, v = 0;
    if (!outValue || index < 0 || index >= STATE_MAX_ALTERABLES) return 0;
    if (!ObjectRval(name, &rvalAddr)) return 0;
    if (!ReadSelf(rvalAddr, head, sizeof(head))) return 0;
    memcpy(&ptr, head + STATE_RVAL_PTR_OFF, 4);
    memcpy(&cnt, head + STATE_RVAL_CNT_OFF, 4);
    if (!ptr || ptr < 0x10000 || cnt <= 0 || cnt > STATE_MAX_ALTERABLES) return 0;
    if (index >= cnt) return 0;
    if (!ReadSelf(ptr + (unsigned long)index * STATE_CVALUE_SIZE, &type, 4)) return 0;
    if (type != 0) return 0;
    if (!ReadSelf(ptr + (unsigned long)index * STATE_CVALUE_SIZE + 8, &v, 4)) return 0;
    *outValue = v;
    return 1;
}

int WriteAnimation(const char *name, int anim)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name || anim < 0 || anim > 1000) return 0;
    for (i = 0; i < p->objCount; ++i) {
        unsigned char oh[0x88];
        int hoSize = 0, forced = 0, on = 0, frame = 0, frames = 0;
        unsigned long obj;
        if (_stricmp(p->objs[i].name, name) != 0) continue;
        if (p->objs[i].type != 2) return 0;          /* Active objects only */
        obj = p->objs[i].obj;
        if (!ReadSelf(obj, oh, sizeof(oh))) return 0;
        memcpy(&hoSize, oh + 0x04, 4);
        if (hoSize < STATE_ANIM_MIN_SIZE) return 0;
        if (!ReadSelf(obj + STATE_ANIM_FORCED_OFF, &forced, 4)) return 0;
        if (!ReadSelf(obj + STATE_ANIM_ON_OFF, &on, 4)) return 0;
        if (!ReadSelf(obj + STATE_ANIM_FRAME_OFF, &frame, 4)) return 0;
        if (!ReadSelf(obj + STATE_ANIM_FRAMES_OFF, &frames, 4)) return 0;
        if (forced < 0 || forced > 1000 || on < -1 || on > 1000 ||
            frame < 0 || frame > 10000 || frames < 0 || frames > 10000) return 0;
        {
            int next = anim + 1, zero = 0;
            if (!WriteSelf(obj + STATE_ANIM_FORCED_OFF, &next, 4)) return 0;
            if (!WriteSelf(obj + STATE_ANIM_DIRFORCED_OFF, &zero, 4)) return 0;
            if (!WriteSelf(obj + STATE_ANIM_SPEEDFORCED_OFF, &zero, 4)) return 0;
        }
        Logf("qol| anim force name=%s anim=%d on=%d frame=%d frames=%d prevForced=%d",
             name, anim, on, frame, frames, forced);
        return 1;
    }
    return 0;
}

int ObjectInfoCount(void) { return g_pub[(int)g_pubCur].objCount; }

const char *ObjectInfoName(int i)
{
    const Published *p = &g_pub[(int)g_pubCur];
    return (i >= 0 && i < p->objCount) ? p->objs[i].name : NULL;
}

int ObjectInfoType(int i)
{
    const Published *p = &g_pub[(int)g_pubCur];
    return (i >= 0 && i < p->objCount) ? p->objs[i].type : -1;
}

int FindObjectInfo(const char *name)
{
    const Published *p = &g_pub[(int)g_pubCur];
    int i;
    if (!name) return -1;
    for (i = 0; i < p->objCount; ++i)
        if (_stricmp(p->objs[i].name, name) == 0) return i;
    return -1;
}

int ObjectInfoAlterable(int i, int index, int *outValue)
{
    const Published *p = &g_pub[(int)g_pubCur];
    const AlterableSlot *os;
    if (i < 0 || i >= p->objCount) return 0;
    os = &p->objs[i];
    if (!os->valid || !os->rvalAddr || index < 0 || index >= os->count) return 0;
    if (outValue) *outValue = os->values[index];
    return 1;
}

int FindObjectAlterable(const char *name, int index, int *outValue)
{
    int i = FindObjectInfo(name);
    return (i >= 0) ? ObjectInfoAlterable(i, index, outValue) : 0;
}

void FormatSummary(char *out, size_t n)
{
    static const char *kScene1[] = {"option", "started", "night 2", "beat1", "keycard", "beat 3", NULL};
    static const char *kScene9[] = {"Freddy", "danger level 4", "got you", "beat", "interface", "restart 1", NULL};
    static const char *kScene27[] = {"hour", "power left", "final stage", "got you", "side", "near", "viewing cam", "count seconds", NULL};
    const char **list = NULL;
    size_t used;
    int i;

    if (!out || !n) return;
    out[0] = 0;
    if (g_status != 1) {
        snprintf(out, n, "status=%s", StatusName());
        out[n - 1] = 0;
        return;
    }
    if (g_sceneIndex == 1) list = kScene1;
    else if (g_sceneIndex == 9) list = kScene9;
    else if (g_sceneIndex == 27 || g_sceneIndex == 31) list = kScene27;
    snprintf(out, n, "scene=%d objs=%d ctr=%d rh=%08lX", g_sceneIndex, g_objectCount, CounterCount(), g_mapRh);
    out[n - 1] = 0;
    used = strlen(out);
    if (list) {
        for (i = 0; list[i] && used + 32 < n; ++i) {
            int v;
            if (!FindCounter(list[i], &v)) continue;
            snprintf(out + used, n - used, " %s=%d", list[i], v);
            used = strlen(out);
        }
    } else {
        int count = CounterCount();
        for (i = 0; i < count && i < 6 && used + 40 < n; ++i) {
            if (!CounterValid(i)) continue;
            snprintf(out + used, n - used, " %s=%d", CounterName(i), CounterValue(i));
            used = strlen(out);
        }
    }
    {
        int v = 0;
        if (used + 24 < n && FindObjectAlterable("Ennard", 3, &v)) {
            snprintf(out + used, n - used, " Ennard3=%d", v);
            used = strlen(out);
        }
    }
}

} /* namespace state */
