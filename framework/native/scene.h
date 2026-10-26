#pragma once
#include <windows.h>

/* Runtime scene (Fusion frame) identification -- read-only, no hooks.
 *
 * Layout of CRunFrame derived empirically from live dumps of SL v1.121
 * (Fusion 2.5 runtime build 286, Unicode runtime):
 *   +0x00 LONG leWidth   +0x04 LONG leHeight   +0x08 COLORREF background
 *   +0x0C DWORD leFlags  +0x10 LPWSTR m_name (UTF-16LE)
 *   +0x2C int m_nLayers  +0x44 int m_loMaxIndex (instance count)
 * A candidate is reported only when the frame name matches the static frame
 * table (frames_table.h) AND width/height/layer/instance counts all match,
 * so a stale or freed object degrades to "unknown" instead of a wrong scene.
 * All reads use ReadProcessMemory on our own process, so a racing free/reprotect
 * cannot raise an access violation.
 *
 * The primary anchor is the private heap block that stores the PAMU pointer at
 * +0x34, the current CRunFrame pointer at +0x38 and the current RunHeader
 * pointer at +0x3C (observed stable across title/Extras/etc. in three dumps);
 * a table-signature scan is the fallback when the anchor cannot be found.
 */
struct SceneInfo {
    int index;              /* frame index in frames.json, -1 = unknown */
    int valid;              /* 1 = signature validated on the last poll */
    char name[64];          /* frame name; "" when unknown */
    unsigned w, h, layers, instances;
    unsigned long base;     /* CRunFrame address; 0 when unknown */
};

namespace scene {
void Init(void);                        /* reset cache + precomputed tables (worker thread) */
int Poll(void);                         /* poll current frame; 1 when a new frame was reported */
const SceneInfo *Current(void);         /* last reported state (never NULL) */
int FrameCount(void);                   /* size of the static frame table */
const char *FrameName(int index);       /* NULL when out of range */
unsigned long RunHeaderPtr(void);       /* current RunHeader address (0 when unknown) */

/* Development-only frame warp (P5.0b "Jump to frame" protocol, plan v1.6).
 * The handle->index map is read live from CRunApp each call, never hard-coded.
 * HandleCount() is the number of handles (36 for this build).
 * WarpToHandle() returns 1 when the request was written, <0 when rejected
 * (no CRunApp, no live RunHeader, missing map, write failure). */
int HandleCount(void);
int HandleToFrameIndex(int handle);     /* -1 when unknown */
const char *HandleFrameName(int handle);/* NULL when unknown */
int WarpToHandle(int handle);
} /* namespace scene */
