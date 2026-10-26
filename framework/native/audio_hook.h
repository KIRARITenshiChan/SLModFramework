/* audio_hook.h - Phase 6 read-only DirectSound probe (P6a).
 * Hooks DirectSoundCreate + IDirectSound::CreateSoundBuffer, then the shared
 * IDirectSoundBuffer vtable (Play/SetVolume) to identify which sample a buffer
 * plays (PCM fingerprint from framework/native/sound_table.h).
 * Read-only: never writes game memory, never replaces assets.
 */
#ifndef SLMODF_AUDIO_HOOK_H
#define SLMODF_AUDIO_HOOK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Install probe hooks. Returns 1 on success, 0 when disabled/failed (never fatal). */
int  audio_install_early(void);
void audio_shutdown(void);
void audio_get_stats(unsigned long *creates, unsigned long *plays,
                     unsigned long *matched, unsigned long *screams);
const char *audio_last_match_name(void);

/* --- Plugin-facing runtime API (Phase 6b, used by ctx.audio in plugin_host) --- */

/* 1 when the DirectSound probe was installed at startup (config audio_probe=1). */
int  audio_is_probe_installed(void);

/* Enable/disable scream attenuation at runtime. gainPercent is 1..100.
 * Values are clamped; the framework config value remains the startup default. */
void audio_set_soften(int enabled, int gainPercent);
void audio_get_soften(int *enabled, int *gainPercent);
void audio_reset_soften(void); /* back to the values captured at probe install */

/* Whitelist of scream names that may be attenuated (exact, case-insensitive).
 * count==0 clears the list (all identified screams). Returns the stored count. */
int  audio_set_whitelist(const char *const *names, int count);

/* Last identified play event (matched sample only). Returns 1 when valid. */
typedef struct SlmodfAudioMatch {
    int  valid;
    int  match;          /* index into the sound table */
    unsigned handle;     /* Fusion sample handle used by event programs */
    char name[80];
    int  chunk;          /* stream chunk index (0 = first 1.2 s chunk) */
    int  scream;         /* 1 = jumpscare scream sample */
    int  helper;         /* 1 = helper sample inside a jumpscare chain */
    int  how;            /* 1 = fingerprint, 2 = size+format, 3/5 = stream chunk */
    unsigned long long fp;
    unsigned long plays; /* plays of that DirectSound buffer */
    unsigned long seq;   /* global play sequence number */
} SlmodfAudioMatch;

int  audio_get_last_match(SlmodfAudioMatch *out);

#ifdef __cplusplus
}
#endif
#endif /* SLMODF_AUDIO_HOOK_H */
