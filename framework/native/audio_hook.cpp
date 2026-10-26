/* audio_hook.cpp - Phase 6 (P6a) read-only DirectSound probe.
 *
 * Chain: dsound!DirectSoundCreate -> IDirectSound::CreateSoundBuffer
 *        -> shared IDirectSoundBuffer vtable Play/SetVolume.
 *
 * For every created buffer we record format/size. On first Play we Lock() the
 * buffer, hash the first 4096 PCM bytes (FNV-1a 64) and match it against
 * native/sound_table.h (generated from the local asset dump). Logs only; no
 * buffer content or game memory is ever modified unless (P6b) audio_soften=1,
 * in which case only identified jumpscare scream buffers get a volume offset.
 */
#include "common.h"
#include "audio_hook.h"
#include "sound_table.h"

#include <mmsystem.h>
#include <dsound.h>
#include <math.h>
#include <psapi.h>
#include "MinHook.h"

#define MAX_TRACKED_BUFS 512  /* play-time adoption covers longer sessions; see AdoptOnPlayLocked */
#define FP_MAX (1 << 20)  /* hash cap: min(buffer bytes, 1 MiB); mirrors gen_sound_table.py */
#define LOG_CREATE_LIMIT 40
#define LOG_PLAY_LIMIT 80

typedef HRESULT (WINAPI *PFN_DirectSoundCreate)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);

static PFN_DirectSoundCreate g_origDSCreate = NULL;
static HRESULT (STDMETHODCALLTYPE *g_origCreateSoundBuffer)(IDirectSound*, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN) = NULL;
static HRESULT (STDMETHODCALLTYPE *g_origCreateSoundBuffer8)(IDirectSound8*, LPCDSBUFFERDESC, LPDIRECTSOUNDBUFFER*, LPUNKNOWN) = NULL;
typedef HRESULT (STDMETHODCALLTYPE *PFN_Play)(IDirectSoundBuffer*, DWORD);
typedef HRESULT (STDMETHODCALLTYPE *PFN_SetVolume)(IDirectSoundBuffer*, LONG);
#define MAX_PLAY_VARIANTS 8
#define MAX_VOL_VARIANTS 6
static PFN_Play g_playOrig[MAX_PLAY_VARIANTS];
static PFN_SetVolume g_volOrig[MAX_VOL_VARIANTS];
static void *s_playTargets[MAX_PLAY_VARIANTS];
static void *s_volTargets[MAX_VOL_VARIANTS];
static int s_playVariantCount;
static int s_volVariantCount;

struct BufRec {
    IDirectSoundBuffer *buf;
    DWORD bytes;
    DWORD rate;
    WORD  ch;
    WORD  bits;
    DWORD capsFlags;
    int   fpDone;
    unsigned long long fp;
    int   match;      /* index into kSoundTable, -1 = unknown */
    int   how;        /* 1 = fingerprint, 2 = size+format, 3 = stream chunk, 5 = stream ambiguous, 0 = none */
    int   chunk;      /* stream chunk index when how=3 (0 = first chunk) */
    int   ambiguous;
    unsigned long plays;
    unsigned long lastSeq;    /* last Play sequence number (LRU adoption/eviction) */
    int   volShifted;
    int   playVariant;
    int   volVariant;
};

static BufRec s_bufs[MAX_TRACKED_BUFS];
static int s_bufCount;

/* Sniff the first 4 KiB of a buffer: zero ratio + first 8 bytes. Used to tell
 * whether a buffer is filled before Play (one-shot sample) or after it (streaming). */
static void SniffLocked(BufRec *r, int *zeroPct, unsigned long long *first8)
{
    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0, n, i, z = 0;
    *zeroPct = -1;
    *first8 = 0;
    if (!r->buf || !r->bytes) return;
    if (FAILED(r->buf->Lock(0, r->bytes < 4096 ? r->bytes : 4096, &p1, &n1, &p2, &n2, 0)) || !p1) return;
    n = n1 < 4096 ? n1 : 4096;
    for (i = 0; i < n; i++) if (((const unsigned char *)p1)[i] == 0) z++;
    *zeroPct = n ? (int)(z * 100 / n) : -1;
    for (i = 0; i < 8 && i < n; i++)
        *first8 |= ((unsigned long long)((const unsigned char *)p1)[i]) << (8 * i);
    r->buf->Unlock(p1, n1, p2, n2);
}

/* Raw chunk dump for offline identification (probe only, capped). */
static int s_dumpCount;
static void DumpLocked(BufRec *r, LONG playNum)
{
    char dir[MAX_PATH], path[MAX_PATH];
    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0, n;
    FILE *f;
    if (s_dumpCount >= 40 || !r->buf || r->bytes < 1024) return;
    n = r->bytes < 262144 ? r->bytes : 262144;
    if (FAILED(r->buf->Lock(0, n, &p1, &n1, &p2, &n2, 0)) || !p1) return;
    lstrcpynA(dir, g_fw.logDir, sizeof(dir));
    {
        char *slash = strrchr(dir, (int)0x5C);
        if (slash) *slash = 0;
    }
    wsprintfA(path, "%s\\audio", dir);
    EnsureDirA(path);
    wsprintfA(path, "%s\\audio\\dump-%lu-%03ld-%lu-%lu-%u-%u.bin", dir,
              (unsigned long)g_fw.pid, (long)playNum, (unsigned long)r->bytes,
              (unsigned long)r->rate, (unsigned)r->ch, (unsigned)r->bits);
    f = fopen(path, "wb");
    if (f) {
        fwrite(p1, 1, n1, f);
        if (p2 && n2) fwrite(p2, 1, n2, f);
        fclose(f);
        s_dumpCount++;
        Logf("audio| dump #%d %s bytes=%lu", s_dumpCount, path, (unsigned long)(n1 + n2));
    }
    r->buf->Unlock(p1, n1, p2, n2);
}
static CRITICAL_SECTION s_cs;
static int s_csInit;
static volatile LONG s_creates;
static volatile LONG s_plays;
static volatile LONG s_matched;
static volatile LONG s_screams;
static char s_lastName[80];
static int s_installState; /* 0 = not tried, 1 = ok, -1 = failed/disabled */

/* Phase 6b plugin-facing runtime state (guarded by s_cs). */
#define AUDIO_MAX_WHITELIST 16
static int s_baseSoften, s_baseGain;         /* config values captured at probe install */
static SlmodfAudioMatch s_lastMatch;         /* last identified play event */
static char s_whitelist[AUDIO_MAX_WHITELIST][80];
static int s_whitelistCount;

/* --- winmm / waveOut probes: Fusion may play samples outside DirectSound --- */
typedef DWORD (WINAPI *PFN_mciSendCommandA)(MCIDEVICEID, UINT, DWORD, DWORD_PTR);
typedef DWORD (WINAPI *PFN_mciSendCommandW)(MCIDEVICEID, UINT, DWORD, DWORD_PTR);
typedef BOOL (WINAPI *PFN_PlaySoundA)(LPCSTR, HMODULE, DWORD);
typedef BOOL (WINAPI *PFN_PlaySoundW)(LPCWSTR, HMODULE, DWORD);
typedef MMRESULT (WINAPI *PFN_waveOutWrite)(HWAVEOUT, LPWAVEHDR, UINT);
typedef MMRESULT (WINAPI *PFN_waveOutOpen)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT (WINAPI *PFN_waveOutSetVolume)(HWAVEOUT, DWORD);
static PFN_mciSendCommandA g_origMciA;
static PFN_mciSendCommandW g_origMciW;
static PFN_PlaySoundA g_origPlaySoundA;
static PFN_PlaySoundW g_origPlaySoundW;
static PFN_waveOutWrite g_origWaveOutWrite;
static PFN_waveOutOpen g_origWaveOutOpen;
static PFN_waveOutSetVolume g_origWaveOutSetVolume;
enum { WM_CH_MCI_A = 0, WM_CH_MCI_W, WM_CH_PLAY_SOUND_A, WM_CH_PLAY_SOUND_W,
       WM_CH_WAVE_WRITE, WM_CH_WAVE_OPEN, WM_CH_WAVE_VOL, WM_CH_COUNT };
static volatile LONG s_winmmCalls[WM_CH_COUNT];

static int WinmmShouldLog(int which)
{
    LONG n = InterlockedIncrement(&s_winmmCalls[which]);
    return n <= 20 || (n % 500) == 0;
}

static void PlayCommon(int variant, IDirectSoundBuffer *self, DWORD r1);
static LONG VolumeCommon(int variant, IDirectSoundBuffer *self, LONG vol);
#define DECL_PLAY(N) static HRESULT STDMETHODCALLTYPE Hook_Play##N(IDirectSoundBuffer *self, DWORD r1);
#define DECL_VOL(N)  static HRESULT STDMETHODCALLTYPE Hook_SetVolume##N(IDirectSoundBuffer *self, LONG vol);
DECL_PLAY(0) DECL_PLAY(1) DECL_PLAY(2) DECL_PLAY(3)
DECL_PLAY(4) DECL_PLAY(5) DECL_PLAY(6) DECL_PLAY(7)
DECL_VOL(0) DECL_VOL(1) DECL_VOL(2) DECL_VOL(3) DECL_VOL(4) DECL_VOL(5)

static unsigned long long Fnv1a64Cont(unsigned long long h, const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned long long)p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static int PtrInDsound(void *p)
{
    HMODULE h = GetModuleHandleA("dsound.dll");
    MODULEINFO mi;
    if (!h) return -1;
    if (!GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi))) return -1;
    return (p >= mi.lpBaseOfDll && p < (void *)((char *)mi.lpBaseOfDll + mi.SizeOfImage)) ? 1 : 0;
}

static void LogPtr(const char *tag, void *p)
{
    Logf("audio| %s ptr=%p in_dsound=%d", tag, p, PtrInDsound(p));
}

static int FindBuf(IDirectSoundBuffer *b)
{
    int i;
    for (i = 0; i < s_bufCount; i++)
        if (s_bufs[i].buf == b) return i;
    return -1;
}

static int ReadBufferInfo(IDirectSoundBuffer *b, DWORD *bytes, WAVEFORMATEX *wfx)
{
    DSBCAPS caps;
    HRESULT hr;
    ZeroMemory(&caps, sizeof(caps));
    caps.dwSize = sizeof(caps);
    hr = b->GetCaps(&caps);
    if (FAILED(hr)) return 0;
    *bytes = caps.dwBufferBytes;
    ZeroMemory(wfx, sizeof(*wfx));
    if (FAILED(b->GetFormat(wfx, sizeof(*wfx), NULL))) return 0;
    return 1;
}

static int AddBufLocked(IDirectSoundBuffer *b, DWORD bytes, const WAVEFORMATEX *wfx, DWORD flags)
{
    BufRec *r;
    if (s_bufCount >= MAX_TRACKED_BUFS) return -1;
    r = &s_bufs[s_bufCount];
    ZeroMemory(r, sizeof(*r));
    r->buf = b;
    r->bytes = bytes;
    r->rate = wfx->nSamplesPerSec;
    r->ch = wfx->nChannels;
    r->bits = wfx->wBitsPerSample;
    r->capsFlags = flags;
    r->match = -1;
    r->playVariant = -1;
    r->volVariant = -1;
    s_bufCount++;
    return s_bufCount - 1;
}

/* Hash min(buffer bytes, 1 MiB) of PCM starting at offset 0. Using the whole
 * span (not just the first 4 KiB) avoids collisions between samples that all
 * begin with leading silence; the table generator uses the same rule. */
static void ComputeFpLocked(BufRec *r)
{
    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0;
    DWORD want;
    HRESULT hr;
    unsigned long long h = 1469598103934665603ULL;
    if (r->fpDone) return;
    r->fpDone = 1;
    want = r->bytes < FP_MAX ? r->bytes : FP_MAX;
    if (!want) return;
    hr = r->buf->Lock(0, want, &p1, &n1, &p2, &n2, 0);
    if (FAILED(hr)) {
        Logf("audio| lock failed buf=%p hr=0x%08lX bytes=%lu want=%lu", r->buf, (unsigned long)hr, (unsigned long)r->bytes, (unsigned long)want);
        return;
    }
    if (p1 && n1) h = Fnv1a64Cont(h, (const unsigned char *)p1, n1);
    if (p2 && n2) h = Fnv1a64Cont(h, (const unsigned char *)p2, n2);
    r->fp = h;
    r->buf->Unlock(p1, n1, p2, n2);
}

static void MatchLocked(BufRec *r)
{
    int i, byFp = -1, fpCount = 0, found = -1, ambiguous = 0;
    if (r->match >= 0) return;
    if (r->fpDone && r->fp) {
        for (i = 0; i < kSoundCount; i++) {
            if (kSoundTable[i].fp == r->fp) { byFp = i; fpCount++; }
        }
    }
    if (byFp >= 0 && fpCount == 1) { r->match = byFp; r->how = 1; return; }
    if (fpCount > 1) { r->ambiguous = 1; r->match = -1; r->how = 0; return; }
    for (i = 0; i < kSoundCount; i++) {
        if (kSoundTable[i].bytes == r->bytes && kSoundTable[i].rate == r->rate &&
            kSoundTable[i].channels == r->ch && kSoundTable[i].bits == r->bits) {
            if (found >= 0) { ambiguous = 1; break; }
            found = i;
        }
    }
    if (found >= 0 && !ambiguous) { r->match = found; r->how = 2; return; }
    r->ambiguous = ambiguous;
    r->match = -1;
    r->how = 0;
}



/* --- streaming-chunk matching -------------------------------------------------
 * The Fusion runtime never hands the whole sample to DirectSound: it streams
 * samples through fixed ~1.2 s chunk buffers (observed 105840 B for
 * 44100/mono/16). Chunk #0 starts at sample byte 0, later chunks at multiples
 * of the chunk size, so a prefix fingerprint identifies both the sample and the
 * chunk index. See Workspace/docs (P6a) for the dump evidence.
 */
#define MAX_STREAM_CP 320
struct StreamCp { unsigned int len; short cand; signed char chunk; };

static void MatchStreamLocked(BufRec *r)
{
    struct StreamCp cp[MAX_STREAM_CP];
    unsigned long long cpHash[MAX_STREAM_CP];
    int ncp = 0, i, k, best = -1, bestChunk = 0, matches = 0;
    unsigned int maxLen = 0, pos = 0, nextIdx;
    void *p1 = NULL, *p2 = NULL;
    DWORD n1 = 0, n2 = 0;
    unsigned long long h = 1469598103934665603ULL;
    unsigned char *base;
    DWORD baseLen;
    if (r->match >= 0 || !r->bytes || !r->buf) return;
    ComputeFpLocked(r);
    for (i = 0; i < kSoundCount; i++) {
        const SoundDef *s = &kSoundTable[i];
        unsigned int L;
        if (s->rate != r->rate || s->channels != r->ch || s->bits != r->bits) continue;
        if (s->chunkBytes != r->bytes) continue;
        L = s->bytes < r->bytes ? s->bytes : r->bytes;
        if (L >= 64 && ncp < MAX_STREAM_CP) {
            cp[ncp].len = L; cp[ncp].cand = (short)i; cp[ncp].chunk = 0; ncp++;
        }
        if (s->scream) {
            for (k = 1; k < (int)s->nChunks && k < SLMODF_SOUND_MAX_CHUNKS; k++) {
                L = s->chunkLen[k];
                if (L >= 64 && ncp < MAX_STREAM_CP) {
                    cp[ncp].len = L; cp[ncp].cand = (short)i; cp[ncp].chunk = (signed char)k; ncp++;
                }
            }
        }
    }
    if (!ncp) return;
    for (i = 0; i < ncp; i++) if (cp[i].len > maxLen) maxLen = cp[i].len;
    for (i = 1; i < ncp; i++) {
        struct StreamCp v = cp[i];
        int j = i - 1;
        while (j >= 0 && cp[j].len > v.len) { cp[j + 1] = cp[j]; j--; }
        cp[j + 1] = v;
    }
    if (FAILED(r->buf->Lock(0, maxLen, &p1, &n1, &p2, &n2, 0)) || !p1) return;
    base = (unsigned char *)p1;
    baseLen = n1;
    nextIdx = 0;
    for (pos = 0; pos < maxLen; pos++) {
        unsigned char byte;
        if (pos < baseLen) byte = base[pos];
        else if (p2 && (pos - baseLen) < n2) byte = ((unsigned char *)p2)[pos - baseLen];
        else break;
        h = (h ^ byte) * 1099511628211ULL;
        while (nextIdx < ncp && cp[nextIdx].len == pos + 1) {
            cpHash[nextIdx] = h;
            nextIdx++;
        }
    }
    while (nextIdx < ncp) { cpHash[nextIdx] = 0; nextIdx++; }
    for (i = 0; i < ncp; i++) {
        const SoundDef *s = &kSoundTable[cp[i].cand];
        unsigned long long want = (cp[i].chunk == 0) ? s->prefixFp : s->chunkFp[(int)cp[i].chunk];
        if (!want || cpHash[i] != want) continue;
        if (s->scream) { best = cp[i].cand; bestChunk = cp[i].chunk; break; }
        matches++;
        if (best < 0) { best = cp[i].cand; bestChunk = 0; }
        else if (best != cp[i].cand) r->ambiguous = 1;
    }
    r->buf->Unlock(p1, n1, p2, n2);
    if (best >= 0) {
        r->match = best;
        r->how = r->ambiguous ? 5 : 3;
        r->chunk = bestChunk;
        return;
    }
    MatchLocked(r);
}

static int WhitelistAllowsLocked(const char *name)
{
    int i;
    if (s_whitelistCount <= 0) return 1; /* empty list = every identified scream */
    for (i = 0; i < s_whitelistCount; i++)
        if (name && !_stricmp(s_whitelist[i], name)) return 1;
    return 0;
}

static int SoftenOffset(void)
{
    double g = (double)g_fw.audioGainPercent / 100.0;
    if (g <= 0.0) g = 0.01;
    if (g > 1.0) g = 1.0;
    return (int)(2000.0 * log10(g));
}

static void SoftenLocked(BufRec *r)
{
    LONG cur = 0, nv;
    int off;
    if (!g_fw.audioSoften || r->match < 0 || !kSoundTable[r->match].scream || r->volShifted) return;
    if (!WhitelistAllowsLocked(kSoundTable[r->match].name)) return;
    if (FAILED(r->buf->GetVolume(&cur))) {
        Logf("audio| soften: GetVolume failed buf=%p", r->buf);
        return;
    }
    off = SoftenOffset();
    nv = cur + off;
    if (nv < -10000) nv = -10000;
    if (nv > 0) nv = 0;
    if (r->volVariant < 0 || r->volVariant >= s_volVariantCount || !g_volOrig[r->volVariant]) {
        Logf("audio| soften skipped: no original SetVolume for variant %d buf=%p", r->volVariant, r->buf);
        return;
    }
    g_volOrig[r->volVariant](r->buf, nv);
    r->volShifted = 1;
    {
        LONG verify = 0;
        if (SUCCEEDED(r->buf->GetVolume(&verify)))
            Logf("audio| soften buf=%p handle=%u chunk=%d gain=%d%% off_cb=%d volume=%ld -> %ld (readback=%ld)",
                 r->buf, (unsigned)kSoundTable[r->match].handle, r->chunk, g_fw.audioGainPercent,
                 off, (long)cur, (long)nv, (long)verify);
        else
            Logf("audio| soften buf=%p handle=%u chunk=%d gain=%d%% off_cb=%d volume=%ld -> %ld (readback failed)",
                 r->buf, (unsigned)kSoundTable[r->match].handle, r->chunk, g_fw.audioGainPercent,
                 off, (long)cur, (long)nv);
    }
}

static void *PlayDetour(int variant)
{
    switch (variant) {
    case 0: return (void *)Hook_Play0;
    case 1: return (void *)Hook_Play1;
    case 2: return (void *)Hook_Play2;
    case 3: return (void *)Hook_Play3;
    case 4: return (void *)Hook_Play4;
    case 5: return (void *)Hook_Play5;
    case 6: return (void *)Hook_Play6;
    case 7: return (void *)Hook_Play7;
    }
    return NULL;
}

static void *VolDetour(int variant)
{
    switch (variant) {
    case 0: return (void *)Hook_SetVolume0;
    case 1: return (void *)Hook_SetVolume1;
    case 2: return (void *)Hook_SetVolume2;
    case 3: return (void *)Hook_SetVolume3;
    case 4: return (void *)Hook_SetVolume4;
    case 5: return (void *)Hook_SetVolume5;
    }
    return NULL;
}

/* A new ComCtl-style object may expose a different vtable for secondary vs
 * primary buffers (or for IDirectSoundBuffer vs IDirectSoundBuffer8). MinHook
 * cannot tell which trampoline invoked a shared detour, so every distinct
 * target address gets its own thin detour variant + original pointer. */
static int HookPlayTarget(void *target)
{
    int i, variant;
    void *detour;
    MH_STATUS st, en;
    if (!target) return -1;
    for (i = 0; i < s_playVariantCount; i++)
        if (s_playTargets[i] == target) return i;
    if (s_playVariantCount >= MAX_PLAY_VARIANTS) return -2;
    variant = s_playVariantCount;
    detour = PlayDetour(variant);
    st = MH_CreateHook(target, detour, (LPVOID *)&g_playOrig[variant]);
    if (st != MH_OK) {
        Logf("audio| play hook variant %d target=%p failed status=%s", variant, target, MH_StatusToString(st));
        return -1;
    }
    en = MH_EnableHook(target);
    Logf("audio| play hook #%d target=%p in_dsound=%d enable=%s",
         variant, target, PtrInDsound(target), MH_StatusToString(en));
    if (en != MH_OK) return -1;
    s_playTargets[variant] = target;
    s_playVariantCount++;
    return variant;
}

static int HookVolTarget(void *target)
{
    int i, variant;
    void *detour;
    MH_STATUS st, en;
    if (!target) return -1;
    for (i = 0; i < s_volVariantCount; i++)
        if (s_volTargets[i] == target) return i;
    if (s_volVariantCount >= MAX_VOL_VARIANTS) return -2;
    variant = s_volVariantCount;
    detour = VolDetour(variant);
    st = MH_CreateHook(target, detour, (LPVOID *)&g_volOrig[variant]);
    if (st != MH_OK) {
        Logf("audio| setvolume hook variant %d target=%p failed status=%s", variant, target, MH_StatusToString(st));
        return -1;
    }
    en = MH_EnableHook(target);
    Logf("audio| setvolume hook #%d target=%p in_dsound=%d enable=%s",
         variant, target, PtrInDsound(target), MH_StatusToString(en));
    if (en != MH_OK) return -1;
    s_volTargets[variant] = target;
    s_volVariantCount++;
    return variant;
}

static void HookBufferVtable(IDirectSoundBuffer *buf, int idx)
{
    void **vt = *(void ***)buf;
    void *targetPlay, *targetVol;
    int pv, vv;
    if (!vt) return;
    targetPlay = vt[12];
    targetVol = vt[15];
    if (!s_playVariantCount) {
        LogPtr("buffer.Play target", targetPlay);
        LogPtr("buffer.SetVolume target", targetVol);
    }
    if (s_bufCount <= 12)
        Logf("audio| buffer vtable #%d vt=%p play=%p vol=%p", s_bufCount, (void *)vt, targetPlay, targetVol);
    pv = HookPlayTarget(targetPlay);
    vv = HookVolTarget(targetVol);
    if (idx >= 0 && idx < MAX_TRACKED_BUFS) {
        s_bufs[idx].playVariant = pv;
        s_bufs[idx].volVariant = vv;
    }
}

static void TrackCreateLocked(HRESULT hr, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *out)
{
    IDirectSoundBuffer *buf = NULL;
    WAVEFORMATEX wfx;
    DWORD bytes = 0, flags = 0, capsFlags = 0;
    LONG n;
    int have = 0;
    ZeroMemory(&wfx, sizeof(wfx));
    if (SUCCEEDED(hr) && out) buf = *out;
    if (buf) have = ReadBufferInfo(buf, &bytes, &wfx);
    if (!have && desc && desc->lpwfxFormat) {
        const WAVEFORMATEX *wf = desc->lpwfxFormat;
        wfx.wFormatTag = wf->wFormatTag;
        wfx.nChannels = wf->nChannels;
        wfx.nSamplesPerSec = wf->nSamplesPerSec;
        wfx.nAvgBytesPerSec = wf->nAvgBytesPerSec;
        wfx.nBlockAlign = wf->nBlockAlign;
        wfx.wBitsPerSample = wf->wBitsPerSample;
        bytes = desc->dwBufferBytes;
        flags = desc->dwFlags;
        have = 1;
    }
    n = InterlockedIncrement(&s_creates);
    if (n <= LOG_CREATE_LIMIT || (n % 64) == 0) {
        Logf("audio| create #%ld hr=0x%08lX buf=%p bytes=%lu rate=%lu ch=%u bits=%u fmt_tag=%u",
             (long)n, (unsigned long)hr, buf, (unsigned long)bytes, (unsigned long)wfx.nSamplesPerSec,
             (unsigned)wfx.nChannels, (unsigned)wfx.wBitsPerSample, (unsigned)wfx.wFormatTag);
    }
    if (buf) {
        DWORD capsBytes = 0;
        WAVEFORMATEX capsFmt;
        DSBCAPS caps;
        int haveCaps;
        if (!have) {
            ZeroMemory(&capsFmt, sizeof(capsFmt));
            have = ReadBufferInfo(buf, &capsBytes, &capsFmt);
            if (have) { bytes = capsBytes; wfx = capsFmt; }
        }
        ZeroMemory(&caps, sizeof(caps));
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(buf->GetCaps(&caps))) capsFlags = caps.dwFlags;
        haveCaps = (s_bufCount < MAX_TRACKED_BUFS) ? AddBufLocked(buf, bytes, &wfx, capsFlags) : -1;
        if (haveCaps >= 0) HookBufferVtable(buf, haveCaps);
    }
}

static HRESULT STDMETHODCALLTYPE Hook_CreateSoundBuffer(IDirectSound *self, LPCDSBUFFERDESC desc,
                                                        LPDIRECTSOUNDBUFFER *out, LPUNKNOWN outer)
{
    HRESULT hr;
    EnterCriticalSection(&s_cs);
    hr = g_origCreateSoundBuffer(self, desc, out, outer);
    TrackCreateLocked(hr, desc, out);
    LeaveCriticalSection(&s_cs);
    return hr;
}

static HRESULT STDMETHODCALLTYPE Hook_CreateSoundBuffer8(IDirectSound8 *self, LPCDSBUFFERDESC desc,
                                                         LPDIRECTSOUNDBUFFER *out, LPUNKNOWN outer)
{
    HRESULT hr;
    EnterCriticalSection(&s_cs);
    hr = g_origCreateSoundBuffer8(self, desc, out, outer);
    TrackCreateLocked(hr, desc, out);
    LeaveCriticalSection(&s_cs);
    return hr;
}

/* Long sessions create hundreds of DirectSound buffers (music streams, scene
 * one-shots, reused sample buffers). Track only the first MAX_TRACKED_BUFS at
 * create time; when an untracked buffer is played, adopt it here so jumpscare
 * identification (and softening) keeps working for the whole session instead of
 * silently degrading after the table fills. When the table is full, recycle the
 * least-recently-played record: dead buffers never play again, and if one does
 * it will simply be re-adopted. */
static unsigned long s_adoptCount;

static int AdoptOnPlayLocked(IDirectSoundBuffer *b, unsigned long seq)
{
    DWORD bytes = 0;
    WAVEFORMATEX wfx;
    int slot, i;
    BufRec *r;
    if (!b) return -1;
    if (!ReadBufferInfo(b, &bytes, &wfx)) return -1;
    if (!bytes || !wfx.nSamplesPerSec) return -1;
    if (s_bufCount < MAX_TRACKED_BUFS) {
        slot = s_bufCount;
    } else {
        unsigned long bestSeq = ~0UL;
        slot = 0;
        for (i = 0; i < s_bufCount; i++) {
            if (s_bufs[i].lastSeq < bestSeq) {
                bestSeq = s_bufs[i].lastSeq;
                slot = i;
            }
        }
    }
    r = &s_bufs[slot];
    ZeroMemory(r, sizeof(*r));
    r->buf = b;
    r->bytes = bytes;
    r->rate = wfx.nSamplesPerSec;
    r->ch = wfx.nChannels;
    r->bits = wfx.wBitsPerSample;
    {
        DSBCAPS caps;
        ZeroMemory(&caps, sizeof(caps));
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(b->GetCaps(&caps))) r->capsFlags = caps.dwFlags;
    }
    r->match = -1;
    r->playVariant = -1;
    r->volVariant = -1;
    r->lastSeq = seq;
    if (slot == s_bufCount) s_bufCount++;
    HookBufferVtable(b, slot);
    if (s_adoptCount < 12) {
        s_adoptCount++;
        Logf("audio| adopt buf=%p bytes=%lu rate=%lu ch=%u bits=%u tracked=%d",
             b, (unsigned long)bytes, (unsigned long)wfx.nSamplesPerSec,
             (unsigned)wfx.nChannels, (unsigned)wfx.wBitsPerSample, s_bufCount);
    }
    return slot;
}

static void PlayCommon(int variant, IDirectSoundBuffer *self, DWORD r1)
{
    LONG n = InterlockedIncrement(&s_plays);
    EnterCriticalSection(&s_cs);
    {
        int idx = FindBuf(self);
        BufRec *r = NULL;
        int verbose;
        if (idx < 0) idx = AdoptOnPlayLocked(self, (unsigned long)n);
        if (idx >= 0) {
            int zeroPct = -1;
            unsigned long long first8v = 0;
            r = &s_bufs[idx];
            r->plays++;
            r->lastSeq = (unsigned long)n;
            /* streaming buffers are refilled between plays: re-hash every play */
            r->fpDone = 0;
            r->match = -1;
            r->how = 0;
            r->chunk = 0;
            r->ambiguous = 0;
            r->volShifted = 0;
            MatchStreamLocked(r);
            SniffLocked(r, &zeroPct, &first8v);
            if (zeroPct >= 0 && zeroPct < 95) DumpLocked(r, n);
            if (r->match >= 0) {
                InterlockedIncrement(&s_matched);
                if (kSoundTable[r->match].scream) InterlockedIncrement(&s_screams);
                lstrcpynA(s_lastName, kSoundTable[r->match].name, sizeof(s_lastName));
                ZeroMemory(&s_lastMatch, sizeof(s_lastMatch));
                s_lastMatch.valid = 1;
                s_lastMatch.match = r->match;
                s_lastMatch.handle = kSoundTable[r->match].handle;
                lstrcpynA(s_lastMatch.name, kSoundTable[r->match].name, sizeof(s_lastMatch.name));
                s_lastMatch.chunk = r->chunk;
                s_lastMatch.scream = kSoundTable[r->match].scream;
                s_lastMatch.helper = kSoundTable[r->match].helper;
                s_lastMatch.how = r->how;
                s_lastMatch.fp = r->fp;
                s_lastMatch.plays = r->plays;
                s_lastMatch.seq = (unsigned long)n;
            }
            verbose = (r->match >= 0 && (kSoundTable[r->match].scream || kSoundTable[r->match].helper)) ||
                      n <= LOG_PLAY_LIMIT || (n % 40) == 0;
            if (verbose) {
                unsigned long long fp = r->fp;
                Logf("audio| play #%ld buf=%p variant=%d bytes=%lu rate=%lu ch=%u bits=%u fp=%08lX%08lX match=%d handle=%u name=\"%s\" how=%d chunk=%d scream=%d plays=%lu zero=%d first=%016lX",
                     (long)n, self, variant, (unsigned long)r->bytes, (unsigned long)r->rate,
                     (unsigned)r->ch, (unsigned)r->bits,
                     (unsigned long)(fp >> 32), (unsigned long)(fp & 0xFFFFFFFF),
                     r->match, r->match >= 0 ? (unsigned)kSoundTable[r->match].handle : 0u,
                     r->match >= 0 ? kSoundTable[r->match].name : "?",
                     r->how, r->chunk, r->match >= 0 ? (int)kSoundTable[r->match].scream : 0,
                     r->plays, zeroPct, (unsigned long)first8v);
            }
            SoftenLocked(r);
        } else if (n <= LOG_PLAY_LIMIT || (n % 40) == 0) {
            Logf("audio| play #%ld buf=%p variant=%d untracked (created before probe?)", (long)n, self, variant);
        }
    }
    LeaveCriticalSection(&s_cs);
    (void)r1;
}

static LONG VolumeCommon(int variant, IDirectSoundBuffer *self, LONG vol)
{
    LONG eff = vol;
    EnterCriticalSection(&s_cs);
    {
        int idx = FindBuf(self);
        if (idx >= 0) {
            BufRec *r = &s_bufs[idx];
            if (r->match >= 0 && kSoundTable[r->match].scream && g_fw.audioSoften &&
                WhitelistAllowsLocked(kSoundTable[r->match].name)) {
                int off = SoftenOffset();
                eff = vol + off;
                if (eff < -10000) eff = -10000;
                if (eff > 0) eff = 0;
                Logf("audio| setvol softened buf=%p variant=%d handle=%u volume=%ld -> %ld",
                     self, variant, (unsigned)kSoundTable[r->match].handle, (long)vol, (long)eff);
            }
        }
    }
    LeaveCriticalSection(&s_cs);
    return eff;
}

#define DEF_PLAY(N) \
    static HRESULT STDMETHODCALLTYPE Hook_Play##N(IDirectSoundBuffer *self, DWORD r1) \
    { PlayCommon(N, self, r1); return g_playOrig[N](self, r1); }
#define DEF_VOL(N) \
    static HRESULT STDMETHODCALLTYPE Hook_SetVolume##N(IDirectSoundBuffer *self, LONG vol) \
    { LONG eff = VolumeCommon(N, self, vol); return g_volOrig[N](self, eff); }

DEF_PLAY(0) DEF_PLAY(1) DEF_PLAY(2) DEF_PLAY(3)
DEF_PLAY(4) DEF_PLAY(5) DEF_PLAY(6) DEF_PLAY(7)
DEF_VOL(0) DEF_VOL(1) DEF_VOL(2) DEF_VOL(3) DEF_VOL(4) DEF_VOL(5)

static void HookDsObject(IDirectSound *ds)
{
    void **vt = *(void ***)ds;
    void *target;
    MH_STATUS st;
    IDirectSound8 *ds8 = NULL;
    if (!vt) return;
    target = vt[3];
    LogPtr("IDirectSound.CreateSoundBuffer target", target);
    st = MH_CreateHook(target, (LPVOID)Hook_CreateSoundBuffer, (LPVOID *)&g_origCreateSoundBuffer);
    if (st == MH_OK || st == MH_ERROR_ALREADY_CREATED) {
        MH_STATUS en = MH_EnableHook(target);
        Logf("audio| CreateSoundBuffer hooked vt[3]=%p status=%s", target, MH_StatusToString(en));
    } else {
        Logf("audio| CreateSoundBuffer hook failed vt[3]=%p status=%s", target, MH_StatusToString(st));
    }
    if (SUCCEEDED(ds->QueryInterface(IID_IDirectSound8, (void **)&ds8)) && ds8) {
        void *t8 = (*(void ***)ds8)[3];
        if (t8 != target) {
            LogPtr("IDirectSound8.CreateSoundBuffer target", t8);
            st = MH_CreateHook(t8, (LPVOID)Hook_CreateSoundBuffer8, (LPVOID *)&g_origCreateSoundBuffer8);
            if (st == MH_OK || st == MH_ERROR_ALREADY_CREATED) {
                MH_STATUS en = MH_EnableHook(t8);
                Logf("audio| CreateSoundBuffer8 hooked vt[3]=%p status=%s", t8, MH_StatusToString(en));
            } else {
                Logf("audio| CreateSoundBuffer8 hook failed vt[3]=%p status=%s", t8, MH_StatusToString(st));
            }
        } else {
            Logf("audio| IDirectSound8 shares vtable[3] with IDirectSound, single hook");
        }
        ds8->Release();
    }
}

/* --- WINMM / waveOut probes -------------------------------------------------
 * Fusion 2.5 imports only mciSendCommandA/W from WINMM, so samples may be
 * played through MCI or waveOut instead of DirectSound. These hooks are pure
 * observation: they log command/format/bytes and hash waveOutWrite payloads
 * against the sample table (same FNV-1a rule as gen_sound_table.py).
 */
static int MatchPcm(const unsigned char *p, DWORD n)
{
    DWORD hlen;
    unsigned long long fp;
    int i, hit = -1;
    if (!p || !n) return -1;
    hlen = n < FP_MAX ? n : FP_MAX;
    fp = Fnv1a64Cont(1469598103934665603ULL, p, hlen);
    for (i = 0; i < kSoundCount; i++) {
        DWORD elen = kSoundTable[i].bytes < FP_MAX ? kSoundTable[i].bytes : FP_MAX;
        if (elen != hlen) continue;
        if (kSoundTable[i].fp != fp) continue;
        if (hit >= 0) return -1; /* ambiguous */
        hit = i;
    }
    return hit;
}

static DWORD WINAPI Hook_mciSendCommandA(MCIDEVICEID dev, UINT msg, DWORD_PTR p1, DWORD_PTR p2)
{
    if (WinmmShouldLog(WM_CH_MCI_A))
        Logf("winmm| mciSendCommandA dev=%lu msg=0x%04X p1=0x%08lX p2=0x%08lX",
             (unsigned long)dev, (unsigned)msg, (unsigned long)p1, (unsigned long)p2);
    return g_origMciA(dev, msg, p1, p2);
}
static DWORD WINAPI Hook_mciSendCommandW(MCIDEVICEID dev, UINT msg, DWORD_PTR p1, DWORD_PTR p2)
{
    if (WinmmShouldLog(WM_CH_MCI_W))
        Logf("winmm| mciSendCommandW dev=%lu msg=0x%04X p1=0x%08lX p2=0x%08lX",
             (unsigned long)dev, (unsigned)msg, (unsigned long)p1, (unsigned long)p2);
    return g_origMciW(dev, msg, p1, p2);
}
static BOOL WINAPI Hook_PlaySoundA(LPCSTR snd, HMODULE mod, DWORD flags)
{
    if (WinmmShouldLog(WM_CH_PLAY_SOUND_A))
        Logf("winmm| PlaySoundA snd=%p flags=0x%08lX", (const void *)snd, (unsigned long)flags);
    return g_origPlaySoundA(snd, mod, flags);
}
static BOOL WINAPI Hook_PlaySoundW(LPCWSTR snd, HMODULE mod, DWORD flags)
{
    if (WinmmShouldLog(WM_CH_PLAY_SOUND_W))
        Logf("winmm| PlaySoundW snd=%p flags=0x%08lX", (const void *)snd, (unsigned long)flags);
    return g_origPlaySoundW(snd, mod, flags);
}
static MMRESULT WINAPI Hook_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR hdr, UINT sz)
{
    if (hdr && hdr->lpData) {
        int m = MatchPcm((const unsigned char *)hdr->lpData, hdr->dwBufferLength);
        if (m >= 0 || WinmmShouldLog(WM_CH_WAVE_WRITE))
            Logf("winmm| waveOutWrite hwo=%p bytes=%lu match=%d handle=%u name=\"%s\" scream=%d how=%s",
                 (void *)hwo, (unsigned long)hdr->dwBufferLength, m,
                 m >= 0 ? (unsigned)kSoundTable[m].handle : 0u,
                 m >= 0 ? kSoundTable[m].name : "?",
                 m >= 0 ? (int)kSoundTable[m].scream : 0,
                 m >= 0 ? "pcm" : "none");
    } else if (WinmmShouldLog(WM_CH_WAVE_WRITE)) {
        Logf("winmm| waveOutWrite hwo=%p empty", (void *)hwo);
    }
    return g_origWaveOutWrite(hwo, hdr, sz);
}
static MMRESULT WINAPI Hook_waveOutOpen(LPHWAVEOUT phwo, UINT dev, LPCWAVEFORMATEX wfx,
                                        DWORD_PTR cb, DWORD_PTR inst, DWORD flags)
{
    MMRESULT r = g_origWaveOutOpen(phwo, dev, wfx, cb, inst, flags);
    if (WinmmShouldLog(WM_CH_WAVE_OPEN))
        Logf("winmm| waveOutOpen dev=%u hr=%u tag=%u ch=%u rate=%lu bits=%u -> hwo=%p",
             (unsigned)dev, (unsigned)r, (unsigned)(wfx ? wfx->wFormatTag : 0),
             (unsigned)(wfx ? wfx->nChannels : 0),
             (unsigned long)(wfx ? wfx->nSamplesPerSec : 0),
             (unsigned)(wfx ? wfx->wBitsPerSample : 0),
             (void *)(phwo ? *phwo : NULL));
    return r;
}
static MMRESULT WINAPI Hook_waveOutSetVolume(HWAVEOUT hwo, DWORD vol)
{
    if (WinmmShouldLog(WM_CH_WAVE_VOL))
        Logf("winmm| waveOutSetVolume hwo=%p vol=0x%08lX", (void *)hwo, (unsigned long)vol);
    return g_origWaveOutSetVolume(hwo, vol);
}

static void InstallWinmmProbes(void)
{
    HMODULE w = LoadLibraryA("winmm.dll");
    if (!w) { Logf("audio| LoadLibrary(winmm.dll) failed err=%lu", (unsigned long)GetLastError()); return; }
#define WINMM_HOOK(nm, det, orig) do { \
        void *t = (void *)GetProcAddress(w, nm); \
        if (!t) { Logf("audio| winmm: %s not found", nm); } \
        else { \
            MH_STATUS cs = MH_CreateHook(t, (LPVOID)(det), (LPVOID *)(orig)); \
            if (cs == MH_OK || cs == MH_ERROR_ALREADY_CREATED) { \
                MH_STATUS en = MH_EnableHook(t); \
                Logf("audio| winmm hook %s status=%s", nm, MH_StatusToString(en)); \
            } else Logf("audio| winmm hook %s create=%s", nm, MH_StatusToString(cs)); \
        } \
    } while (0)
    WINMM_HOOK("mciSendCommandA", Hook_mciSendCommandA, &g_origMciA);
    WINMM_HOOK("mciSendCommandW", Hook_mciSendCommandW, &g_origMciW);
    WINMM_HOOK("PlaySoundA", Hook_PlaySoundA, &g_origPlaySoundA);
    WINMM_HOOK("PlaySoundW", Hook_PlaySoundW, &g_origPlaySoundW);
    WINMM_HOOK("waveOutWrite", Hook_waveOutWrite, &g_origWaveOutWrite);
    WINMM_HOOK("waveOutOpen", Hook_waveOutOpen, &g_origWaveOutOpen);
    WINMM_HOOK("waveOutSetVolume", Hook_waveOutSetVolume, &g_origWaveOutSetVolume);
#undef WINMM_HOOK
}

static HRESULT WINAPI Hook_DirectSoundCreate(LPCGUID guid, LPDIRECTSOUND *ppDS, LPUNKNOWN outer)
{
    HRESULT hr = g_origDSCreate(guid, ppDS, outer);
    Logf("audio| DirectSoundCreate guid=%p hr=0x%08lX ds=%p", (void *)guid, (unsigned long)hr,
         (ppDS && SUCCEEDED(hr)) ? (void *)*ppDS : NULL);
    if (SUCCEEDED(hr) && ppDS && *ppDS) {
        EnterCriticalSection(&s_cs);
        HookDsObject(*ppDS);
        LeaveCriticalSection(&s_cs);
    }
    return hr;
}

int audio_install_early(void)
{
    HMODULE h;
    void *target;
    MH_STATUS st;
    if (s_installState) return s_installState > 0;
    s_installState = -1;
    s_baseSoften = g_fw.audioSoften;
    s_baseGain = g_fw.audioGainPercent;
    if (!g_fw.audioProbe) {
        Logf("audio| probe disabled (config audio_probe=0)");
        return 0;
    }
    if (!s_csInit) {
        InitializeCriticalSection(&s_cs);
        s_csInit = 1;
    }
    h = LoadLibraryA("dsound.dll");
    if (!h) {
        Logf("audio| LoadLibrary(dsound.dll) failed err=%lu", (unsigned long)GetLastError());
        return 0;
    }
    st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
        Logf("audio| MH_Initialize failed status=%s", MH_StatusToString(st));
        return 0;
    }
    target = (void *)GetProcAddress(h, "DirectSoundCreate");
    if (!target) {
        Logf("audio| DirectSoundCreate export not found in dsound.dll");
        return 0;
    }
    st = MH_CreateHook(target, (LPVOID)Hook_DirectSoundCreate, (LPVOID *)&g_origDSCreate);
    if (st != MH_OK) {
        Logf("audio| hook DirectSoundCreate failed status=%s target=%p", MH_StatusToString(st), target);
        return 0;
    }
    st = MH_EnableHook(target);
    if (st != MH_OK) {
        Logf("audio| enable DirectSoundCreate hook failed status=%s", MH_StatusToString(st));
        return 0;
    }
    InstallWinmmProbes();
    Logf("audio| probe installed dsound=%p DirectSoundCreate=%p orig=%p table=%d screams=%d soften=%d gain=%d%%",
         (void *)h, target, (void *)g_origDSCreate, kSoundCount, kScreamCount,
         g_fw.audioSoften, g_fw.audioGainPercent);
    s_installState = 1;
    return 1;
}

void audio_shutdown(void)
{
    /* Hooks are intentionally left installed: the process is exiting and the
     * game's own WM_CLOSE path can crash (known vanilla behaviour). */
}

void audio_get_stats(unsigned long *creates, unsigned long *plays,
                     unsigned long *matched, unsigned long *screams)
{
    if (creates) *creates = (unsigned long)InterlockedCompareExchange(&s_creates, 0, 0);
    if (plays) *plays = (unsigned long)InterlockedCompareExchange(&s_plays, 0, 0);
    if (matched) *matched = (unsigned long)InterlockedCompareExchange(&s_matched, 0, 0);
    if (screams) *screams = (unsigned long)InterlockedCompareExchange(&s_screams, 0, 0);
}

const char *audio_last_match_name(void)
{
    return s_lastName;
}

/* --- plugin-facing runtime API ------------------------------------------------- */

int audio_is_probe_installed(void)
{
    return s_installState > 0 ? 1 : 0;
}

void audio_set_soften(int enabled, int gainPercent)
{
    int g = gainPercent;
    if (g < 1) g = 1;
    if (g > 100) g = 100;
    if (s_csInit) EnterCriticalSection(&s_cs);
    g_fw.audioSoften = enabled ? 1 : 0;
    g_fw.audioGainPercent = g;
    if (s_csInit) LeaveCriticalSection(&s_cs);
    Logf("audio| plugin set_soften=%d gain=%d%%", g_fw.audioSoften, g_fw.audioGainPercent);
}

void audio_get_soften(int *enabled, int *gainPercent)
{
    if (s_csInit) EnterCriticalSection(&s_cs);
    if (enabled) *enabled = g_fw.audioSoften;
    if (gainPercent) *gainPercent = g_fw.audioGainPercent;
    if (s_csInit) LeaveCriticalSection(&s_cs);
}

void audio_reset_soften(void)
{
    if (s_csInit) EnterCriticalSection(&s_cs);
    g_fw.audioSoften = s_baseSoften;
    g_fw.audioGainPercent = s_baseGain;
    if (s_csInit) LeaveCriticalSection(&s_cs);
    Logf("audio| plugin reset_soften=%d gain=%d%%", g_fw.audioSoften, g_fw.audioGainPercent);
}

int audio_set_whitelist(const char *const *names, int count)
{
    int i, n = 0;
    if (count < 0) count = 0;
    if (s_csInit) EnterCriticalSection(&s_cs);
    s_whitelistCount = 0;
    for (i = 0; i < count && n < AUDIO_MAX_WHITELIST; i++) {
        const char *s = names ? names[i] : NULL;
        if (!s || !s[0]) continue;
        lstrcpynA(s_whitelist[n], s, (int)sizeof(s_whitelist[0]));
        n++;
    }
    s_whitelistCount = n;
    if (s_csInit) LeaveCriticalSection(&s_cs);
    Logf("audio| plugin whitelist count=%d", n);
    return n;
}

int audio_get_last_match(SlmodfAudioMatch *out)
{
    int valid;
    if (!out) return 0;
    if (s_csInit) EnterCriticalSection(&s_cs);
    *out = s_lastMatch;
    valid = s_lastMatch.valid;
    if (s_csInit) LeaveCriticalSection(&s_cs);
    return valid;
}
