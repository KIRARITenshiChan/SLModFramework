#pragma once
#include <stddef.h>

/* Phase 5 game state adapter v0 -- read-only counter/object inspection.
 *
 * Reads (never writes) the Fusion runtime structures of the running game:
 *   current CRunFrame (from the scene module) -> RunHeader via frame+0x7C
 *   -> OI table (rh+0x58, stride 146, UTF-16LE name at entry+0x5A)
 *   -> live objects found by scanning for dword == rh (object base = hit-8)
 *   -> counter current value encoded as value = ~(int16)*(uint16_t*)(obj+0x20A)
 *   -> Active alterable values: obj + hoOffsetValue (0x80) holds an inline rVal;
 *      rVal.rvpValues -> CValue[26], 16 bytes each (type@0, value@8, type 0=long)
 *
 * Alterable values are polled only for a small watch list (currently "Ennard"
 * and "flip it") so the per-tick read budget stays trivial. Other objects are
 * listed by name but report no values. See docs/RUNTIME_STATE.md.
 *
 * Everything is derived from live dumps of SL v1.121 (Fusion build 286); see
 * docs/RUNTIME_STATE.md for the evidence table. When any validation step
 * fails the status degrades to "unknown" and no values are reported, so a
 * stale pointer can never surface as a confident counter reading.
 *
 * Threading: all parsing + value polling runs on the framework worker thread.
 * The overlay/plugin layer only reads the published snapshot (double buffer,
 * benign staleness by design).
 */
namespace state {

void Init(void);                        /* reset all cached state (worker thread) */
int Poll(void);                         /* refresh frame map / counter values */
int Status(void);                       /* 0 init, 1 ok, 2 no RunHeader, 3 scene lost, 4 gate */
const char *StatusName(void);
int SceneIndex(void);                   /* frame index reported by the scene module, -1 unknown */
int MapScene(void);                     /* scene index of the published object map, -1 unknown */
int ValuesReady(void);                  /* 1 = counter values polled for the published map */
int ObjectCount(void);                  /* validated live objects in this frame */
int CounterCount(void);                 /* counters in the published map */
const char *CounterName(int i);         /* NULL when out of range */
int CounterValue(int i);                /* last polled value (0 when invalid) */
int CounterValid(int i);                /* 1 = this poll read succeeded */
int FindCounter(const char *name, int *outValue);  /* case-insensitive; 1 found */

/* P8 QoL: live counter read/write. The published snapshot is up to one poll
 * old, which is too slow for input guards (prompt/answer change in the same
 * game tick), so the QoL layer uses these direct accesses. */
int ReadCounterLive(const char *name, int *outValue);
int WriteCounter(const char *name, int value);           /* raw = (short)~value */
/* Write one Active alterable value (CValue type 0). Only objects that are in
 * the published map can be written; returns 0 when the value block does not
 * validate (wrong scene, stale map, non-Active object). */
int WriteAlterable(const char *name, int index, int value);

/* Force an Active object's animation sequence, the same memory effect as the
 * Fusion action ACT_EXTFORCEANIM (act #17): writes rAni.raAnimForced = anim+1
 * and clears the forced direction/speed. The next runtime tick starts the
 * sequence and fires the "animation ended" condition (cond #-2) when it is
 * done, which is what keeps the door visual and the door logic in sync.
 * Returns 0 when the object/map/animation block does not validate. */
int WriteAnimation(const char *name, int anim);

/* P8 QoL: object geometry + direct alterable read.
 * ReadObjectRect returns the object's frame-space x/y/w/h (hoX @0x4C,
 * hoY @0x54, hoImgWidth @0x60, hoImgHeight @0x64). WriteObjectPosition moves
 * an object (used for the frame-27 "trigger cursor" so the game's own
 * flip-trigger event chain runs). ReadAlterableLive reads one CValue of an
 * Active object (type 0 only); unlike FindObjectAlterable it is not limited
 * to the state watch list. All return 1 on success. */
int ReadObjectRect(const char *name, int *outX, int *outY, int *outW, int *outH);
int WriteObjectPosition(const char *name, int x, int y);
int ReadAlterableLive(const char *name, int index, int *outValue);

/* Object table (all validated objects in the frame, not only counters). */
int ObjectInfoCount(void);
const char *ObjectInfoName(int i);                 /* NULL when out of range */
int ObjectInfoType(int i);                         /* Fusion OI type (2 = Active, 7 = Counter) */
int FindObjectInfo(const char *name);              /* case-insensitive index or -1 */

/* Active alterable values, only for the state watch list ("Ennard", "flip it").
 * index is the 0-based alterable slot. Returns 1 when the value was read on
 * the most recent poll; 0 means unknown (wrong object/index, not watched, or
 * the frame map is stale). */
int ObjectInfoAlterable(int i, int index, int *outValue);
int FindObjectAlterable(const char *name, int index, int *outValue);

void FormatSummary(char *out, size_t n);           /* one-line watch-list summary for logs */

}
