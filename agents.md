# agents.md

## Purpose

This file defines how an autonomous coding agent must behave while working on this Android playback branch.

This project branch is **not** a general media-player exploration branch.

It has one specific purpose:

- keep GStreamer only for **UDP/RTP ingest + jitter + H.265 depay + H.265 parse**
- move **decode + render** to the Android system stack
- render through **MediaCodec + SurfaceView/Surface**

The target architecture is fixed for this branch.

---

## Fixed architectural decision

The active playback path must be:

`udpsrc -> rtpjitterbuffer -> rtph265depay -> h265parse -> appsink/native bridge -> MediaCodec -> Surface`

This is not optional.

The agent must treat this as a hard constraint, not as a suggestion.

---

## Branch objective

The objective of this branch is:

**Get first visible live video on Android from the incoming RTP/H.265 stream by using GStreamer only up to `h265parse` and using Android MediaCodec for decode/render.**

This branch is successful when:

- GStreamer no longer handles Android video decode/render
- parsed H.265 data exits the native pipeline
- that data crosses a native-to-Java/Kotlin bridge
- MediaCodec decodes to a Surface
- live video becomes visible on screen

---

## What the agent must optimize for

The agent must optimize for the following, in order:

1. **Correct architectural direction**
2. **Minimal working path to first visible image**
3. **Small, verifiable changes**
4. **Fast localization of failures**
5. **Low risk of side effects**
6. **Clean logs and useful diagnostics**

The agent must **not** optimize for elegance, generality, or ambitious refactoring unless directly required for the branch goal.

---

## Hard constraints

The agent must obey all of the following:

- Do not reintroduce GStreamer-based video decode in the active path.
- Do not reintroduce GStreamer-based video sinks in the active path.
- Do not spend time reviving `androidmedia`, `androidvideosink`, `glimagesink`, `glsinkbin`, `autovideosink`, `decodebin`, `playbin`, or `gstlibav` for this branch.
- Do not attempt broad media-framework redesign.
- Do not replace the whole project with a different app unless explicitly instructed.
- Do not remove useful diagnostics just to make code look cleaner.
- Do not perform unrelated refactors.
- Do not touch unrelated modules unless necessary for the active milestone.
- Treat RTP payload type **96** as valid and expected.
- Assume the stream is H.265 over RTP and validate that path rather than questioning PT=96 itself.

---

## Allowed scope

The agent may work in the following areas when necessary:

- current GStreamer pipeline builder code
- native appsink or native output bridge code
- JNI bridge code
- Android Activity / Fragment / playback screen
- `SurfaceView` and `SurfaceHolder.Callback`
- MediaCodec wrapper code
- logging and debug counters
- small utility classes directly needed for the playback path

The agent may also add:

- a small bounded queue between native ingest and decoder
- optional parser output dump mode
- temporary debug logs and counters
- compact developer notes in code comments where needed

---

## Explicit non-goals

The following are not goals for this branch unless the user explicitly expands scope after first image is working:

- fixing `androidmedia`
- making `gstlibav` static linking work
- OpenGL video sink experiments
- hardware-accelerated rendering through GStreamer
- RTSP output
- SRT output
- file recording
- audio support
- player controls
- polished UI
- adaptive bitrate logic
- generalized codec abstraction for many formats
- production-grade packet loss concealment
- performance tuning beyond what is needed for first visible playback

---

## Required work style

The agent must work in **small, atomic steps**.

Each step should:

- change as little as possible
- have one clear intent
- produce a visible result, log, or diagnostic improvement
- be easy to review and revert

The agent must not bundle many unrelated changes into one step.

The preferred rhythm is:

1. inspect current state
2. make one targeted change
3. verify with logs or observable behavior
4. summarize result
5. only then proceed

---

## Required milestone sequence

The agent must follow this order unless blocked by a hard technical reason:

1. remove GStreamer decode/render attempts from active path
2. lock the pipeline to ingest/depay/parse only
3. prove parsed H.265 data arrives at appsink/native boundary
4. add fullscreen `SurfaceView`
5. implement MediaCodec wrapper
6. implement native-to-Java/Kotlin bridge
7. feed parsed output into decoder
8. obtain first visible frame
9. only after that: improve timing, recovery, and latency

The agent must not skip directly to “improvements” before proving the previous milestone.

---

## Required debugging discipline

When playback does not work, the agent must localize the failure to one boundary before making broader changes.

The main boundaries are:

1. UDP/RTP ingress into GStreamer
2. output after `rtph265depay`
3. output after `h265parse`
4. handoff through appsink/native callback
5. handoff across JNI bridge
6. MediaCodec startup/configure
7. MediaCodec input queue
8. MediaCodec output/render

The agent must always ask:

**Which exact boundary is currently proven, and which one is not yet proven?**

The agent must not rewrite upstream layers if the failure is not localized there.

---

## Required diagnostics

The agent should preserve or add enough diagnostics to answer all of the following:

- Did the pipeline build successfully?
- Did it reach PLAYING?
- Are samples arriving after `h265parse`?
- What sizes are those samples?
- Are VPS/SPS/PPS present?
- Are parser outputs NAL-aligned or AU-aligned?
- Did the bridge receive data?
- Did MediaCodec initialize?
- Which decoder name was selected?
- Were input buffers queued?
- Were output frames produced?
- Were output buffers released to render?

If one of these answers is missing, the next change should usually improve diagnostics rather than expand scope.

---

## Handling unknown parser output details

The agent must not assume the exact format emitted by `h265parse`.

It must verify:

- caps on parser output
- whether buffers contain Annex B start codes
- whether buffers are AU-aligned
- whether codec config NAL units are present
- whether first decodable frame has required VPS/SPS/PPS context

If uncertain, the agent must inspect actual buffers and log findings before redesigning the bridge.

---

## MediaCodec expectations

The agent should treat MediaCodec as the authoritative Android decode path for this branch.

The agent must:

- configure decode against a valid `Surface`
- select a decoder compatible with `video/hevc`
- feed coherent HEVC units rather than arbitrary fragments
- log initialization, input queue activity, output activity, and exceptions

The agent must not assume that arbitrary partial buffers will decode correctly.

The agent should prefer a minimal, stable wrapper over clever abstractions.

---

## Bridge rules

The agent must not forward tiny RTP fragments one-by-one into Java/Kotlin if parser-level units are available.

The bridge should operate at the level of:

- complete parser output buffers
- preferably complete access units
- or at minimum complete HEVC NAL-oriented buffers suitable for decoder input analysis

The agent should add a small bounded queue if needed for stability.

The queue policy must be explicit and logged when drops occur.

---

## Logging policy

Logging is required for this branch.

The agent should:

- keep logs concise but informative
- log state transitions and first-occurrence details
- add counters for repeated events
- rate-limit noisy logs if needed
- avoid deleting useful logs until first visible playback is achieved

The agent should not flood logs with giant hex dumps by default.

Optional debug dump modes are allowed, but must be off by default.

---

## Change policy

Before modifying code, the agent should identify:

- which file owns the current responsibility
- whether the change is local or cross-layer
- whether a smaller change can prove the same point

The agent should prefer adding small wrappers or adapters over invasive rewrites.

The agent should not rename many files or restructure the whole module tree unless absolutely necessary.

---

## Reporting format

After each meaningful work step, the agent should produce a short structured report containing:

1. **What was changed**
2. **Why it was changed**
3. **What was verified**
4. **What remains unproven**
5. **Next smallest sensible step**

The report should be short, technical, and specific.

Example style:

- Changed pipeline builder to remove all decoder/sink branches.
- Locked active pipeline to `udpsrc -> rtpjitterbuffer -> rtph265depay -> h265parse -> appsink`.
- Verified pipeline reaches PLAYING and appsink callback fires.
- Still unproven: exact parser output format and MediaCodec path.
- Next: inspect first parser buffers for Annex B/AU alignment and VPS/SPS/PPS.

---

## Stop conditions

The agent must stop and report instead of thrashing when:

- a boundary cannot be proven with current diagnostics
- a proposed next step would require broad unrelated refactoring
- parser output format is unclear and needs actual inspection
- MediaCodec fails in a way that requires exact logs before proceeding
- multiple competing solutions exist and none can be justified from evidence

In such cases, the correct behavior is:

- summarize what is proven
- summarize what is not proven
- show the exact blocker
- propose the smallest evidence-gathering step

---

## Success criteria for this branch

The branch is successful only when all of the following are true:

- active playback path is `udpsrc -> rtpjitterbuffer -> rtph265depay -> h265parse -> appsink/native bridge -> MediaCodec -> Surface`
- no GStreamer decoder/sink is needed for playback
- parsed HEVC data is observable at the bridge boundary
- MediaCodec starts successfully on the target Android device
- decoder input queue receives actual stream data
- decoded video becomes visible on screen

---

## One-line directive for the agent

**Do not try to make GStreamer render video on Android in this branch. Prove the path step by step from `h265parse` output into Android MediaCodec until first live image appears.**
