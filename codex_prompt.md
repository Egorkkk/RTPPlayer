# codex_prompt.md

You are working on an existing Android project that currently uses GStreamer.

Before making any changes, read and follow these files strictly:

- `tasks.md`
- `agents.md`

They define both:
- **what must be built**
- **how you must behave while building it**

Do not ignore them.

---

## Mission

Implement **Variant A** only:

- keep GStreamer only for:
  - UDP ingest
  - RTP jitter handling
  - H.265 depayloading
  - H.265 parsing
- move Android video decode and rendering to:
  - `MediaCodec`
  - `SurfaceView` / `Surface`

The required playback path for this branch is:

`udpsrc -> rtpjitterbuffer -> rtph265depay -> h265parse -> appsink/native bridge -> MediaCodec -> Surface`

This architecture is fixed for this branch.

---

## What you must not do

Do **not** spend time trying to revive or reintroduce any GStreamer-based Android decode/render path, including but not limited to:

- `androidmedia`
- `androidvideosink`
- `glimagesink`
- `glsinkbin`
- `autovideosink`
- `decodebin`
- `playbin`
- `gstlibav`

Do not broaden the scope.

Do not replace the whole app with a different architecture.

Do not do unrelated refactors.

---

## Stream assumptions

Assume the incoming stream is:

- UDP on port `5600`
- RTP
- H.265 / HEVC
- dynamic payload type `96`

Treat payload type `96` as valid and expected.

Do not treat PT=96 as an error.

The key issue is not RTP discovery anymore.  
The key issue is getting a reliable path from parsed HEVC output into Android `MediaCodec`.

---

## Immediate objective

Your first success target is:

**Get the first visible live video frame on Android.**

That matters more than:
- polished architecture
- advanced latency tuning
- packet-loss recovery
- elegant abstractions
- UI improvements

---

## Required working style

Work in small, verifiable steps.

For each step:

1. inspect current code
2. make one targeted change
3. verify with logs or observable behavior
4. report what is proven
5. state what remains unproven
6. choose the next smallest sensible step

Do not make many unrelated changes at once.

Do not guess when you can instrument and verify.

---

## Required milestone order

Follow this order unless a hard blocker forces otherwise:

1. remove all active GStreamer decode/render attempts
2. lock the active pipeline to parse-only mode
3. prove data arrives after `h265parse`
4. add or verify fullscreen `SurfaceView`
5. implement `MediaCodec` HEVC decoder wrapper
6. implement native-to-Java/Kotlin bridge
7. feed parser output into decoder
8. get first visible frame
9. only then improve timing, resilience, and latency

---

## Evidence-first debugging rule

Whenever playback fails, identify the exact boundary where evidence stops.

Use these boundaries:

1. pipeline builds
2. pipeline reaches PLAYING
3. samples appear after `h265parse`
4. appsink/native callback receives data
5. JNI bridge receives data
6. `MediaCodec` initializes successfully
7. decoder input buffers are queued
8. decoder output frames appear
9. frames render to surface

Do not rewrite earlier stages if the failure is not proven there.

---

## Expected deliverables

You should leave the project in a state where:

- active playback no longer depends on GStreamer decode/render
- parsed HEVC data is observable at the native bridge boundary
- `MediaCodec` is configured against a real `Surface`
- decoder input/output activity is visible in logs
- first visible playback is either working, or the exact blocking boundary is clearly identified

---

## Reporting format

After each meaningful implementation step, report in this format:

### Step summary
- What changed
- Why it changed
- What is now proven
- What is still unproven
- Next smallest step

Keep reports short, technical, and concrete.

---

## Final instruction

Do not chase alternate architectures in this branch.

Stay on the fixed path:

`GStreamer ingest/parse -> native bridge -> MediaCodec -> Surface`

Your job is to make that path work, step by step, until first live image appears.
