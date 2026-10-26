/* recorder.h -- optional JSONL state snapshots for debugging and evidence capture.
 * Phase 5: writes <game>\Workspace\logs\state\state-<ts>-<pid>.jsonl while
 * enabled (config record=1 / GUI checkbox). Records scene changes immediately
 * and every record_interval_ms otherwise. Read-only w.r.t. game memory.
 */
#pragma once

namespace recorder {
void Init(void);
void Poll(void);
void SetEnabled(int on, const char *source);
int Enabled(void);
const char *Path(void);
}
