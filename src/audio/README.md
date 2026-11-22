# src/audio

The mixer and the procedural synth (SPEC §14.2). **Not implemented yet**: milestone M7, tasks 1B-20 (mixer, `mixer.{h,c}`) and 1B-21 (synth, `synth.c`); the WASAPI backend is `src/platform/win32/win32_audio.c` (1B-22) and the web backend 1B-43. Today `bmf_sfx_play` (core) queues events per tick and the engine counts and drops them after the tick (`core_deliver_events`, `stats.sfx_played`).

## Invariants (to keep)
- Audio is presentation, not simulation: the game's tick asks for sounds with `bmf_sfx_play` (queued per tick, delivered after the tick, at most `BMF_CORE_MAX_SFX_PER_TICK` = 256 per tick, extra ones dropped and counted in `stats.sfx_dropped`); the mixer never touches the state, so replays and hashes do not depend on audio. `bmf_fast_*` math is allowed here.
- The simulation-to-audio path is a single-producer single-consumer lock-free command ring (play, stop, set-param with voice handles); the audio thread never waits on the main thread.
- No clicks: bus gains ramp smoothly; the master bus ends in a soft clipper.
- Procedural sounds are data: a `bmf_sfx_desc` POD (oscillator, ADSR, pitch slide, vibrato, noise, filters, duty, repeat) baked to a sample buffer at load time by `bmf_sfx_bake` (into an arena).

## Data layout (planned)
64 voices (source buffer, 32.32 fixed-point position, rate, gain, pan, loop, bus); buses master, music, sfx; f32 stereo at 48 kHz. Sampled audio is 16-bit PCM from the pak (1B-31/1B-32).

## Hot paths (planned)
The mix loop on the audio thread (about 10 ms periods on WASAPI shared mode, event driven, `AvSetMmThreadCharacteristics`); linear-interpolation resampler per voice.

## Limits (planned)
Voice stealing takes the oldest lowest-priority voice. Streaming music (own ADPCM-style codec) is Phase 2. Web audio (AudioWorklet fed by messages, about 40 ms) until a SharedArrayBuffer ring is available.
