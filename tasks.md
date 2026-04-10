# tasks.md

## Project direction

We are using **Variant A**:

- keep GStreamer only for **network ingest + RTP jitter + H.265 depay + H.265 parse**
- do **not** use GStreamer for Android decode or video rendering
- decode with **Android MediaCodec**
- render with **Android SurfaceView / Surface**

### Fixed architectural decision

The playback path must be:

`udpsrc -> rtpjitterbuffer -> rtph265depay -> h265parse -> appsink/native bridge -> MediaCodec -> Surface`

Anything that attempts Android video decode/render through GStreamer must be considered **out of scope** for this branch.

---

## Global constraints

- Do not spend time trying to revive `androidmedia`, `androidvideosink`, `glimagesink`, `glsinkbin`, `autovideosink`, `decodebin`, `playbin`, or `gstlibav` for Android playback.
- RTP dynamic payload type **96** is expected and must be treated as valid.
- The current goal is **first visible video on screen**.
- Low latency, packet-loss recovery, frame pacing, and advanced resilience are **secondary tasks** after first image appears.
- Prefer minimal invasive changes to the current Android/GStreamer project.

---

## Stage 1 — Freeze the playback architecture

### Task 1.1 — Remove GStreamer decode/render attempts from active code path

**Goal:** Ensure the app no longer tries to build or use any GStreamer-based decode/render path.

**Actions:**
- inspect current pipeline builder code
- remove or disable any branches that attempt to append:
  - video decoders
  - `decodebin`
  - `playbin`
  - video sinks
  - Android media helper paths
  - GL sinks
- keep only the ingest path up to `h265parse`

**Deliverable:**
- active pipeline builder produces only ingest/parse pipeline
- logs clearly show that decoder/sink discovery is no longer part of the startup path

**Acceptance criteria:**
- pipeline creation is deterministic
- there is no runtime branch trying to find or instantiate video decoder or video sink through GStreamer

---

## Stage 2 — Build stable ingest pipeline

### Task 2.1 — Lock the pipeline to parse-only mode

**Goal:** Have one stable pipeline that reaches `h265parse` successfully.

**Target pipeline:**

`udpsrc port=5600 caps=application/x-rtp,media=video,encoding-name=H265,payload=96 ! rtpjitterbuffer ! rtph265depay ! h265parse ! appsink`

**Notes:**
- exact caps syntax may vary depending on current codebase and helper functions
- payload type 96 must be explicit where helpful
- `appsink` may be replaced with a custom native sink if that is cleaner in the current project

**Actions:**
- harden pipeline construction
- add state-change logging
- add bus error logging
- add caps logging at critical points if possible

**Deliverable:**
- parse-only pipeline builds and runs reliably

**Acceptance criteria:**
- no `gst_parse_launch failed`
- pipeline reaches PLAYING state
- samples/data continue to arrive after startup

### Task 2.2 — Add appsink/native output logging

**Goal:** Prove that parsed H.265 data exits the pipeline.

**Actions:**
- attach callback for appsink sample reception
- log for each received sample:
  - buffer size
  - pts/dts if available
  - keyframe/config flags if available
  - caps string on first sample
- keep logs rate-limited if necessary

**Deliverable:**
- runtime logs showing parsed H.265 payload delivery beyond `h265parse`

**Acceptance criteria:**
- repeated samples received in PLAYING state
- sample sizes are non-zero

---

## Stage 3 — Inspect what exactly comes out of h265parse

### Task 3.1 — Determine stream format at appsink boundary

**Goal:** Understand whether the bridge receives Annex B NAL units, AU-aligned buffers, codec config blocks, or another format.

**Actions:**
- inspect appsink caps and parser output format
- log whether buffers appear AU-aligned
- inspect first bytes of several buffers to detect start codes and NAL structure
- verify presence of VPS / SPS / PPS before first decodable frame

**Deliverable:**
- written note in code comments or project notes describing actual parser output format

**Acceptance criteria:**
- team can answer all of the following:
  - are buffers NAL-aligned or AU-aligned?
  - are VPS/SPS/PPS present in the stream?
  - do buffers contain Annex B start codes?
  - what should be passed into MediaCodec?

### Task 3.2 — Add optional debug dump mode

**Goal:** Make parser output inspectable outside the live app.

**Actions:**
- add optional debug flag to dump first N buffers to file
- add optional hex summary of first bytes for logs
- keep feature disabled by default

**Deliverable:**
- reproducible debug mode for offline inspection

**Acceptance criteria:**
- first parser outputs can be saved and later inspected without changing the live path

---

## Stage 4 — Create Android render surface

### Task 4.1 — Add fullscreen SurfaceView

**Goal:** Provide a stable Android-native rendering target.

**Actions:**
- add a fullscreen `SurfaceView` in the main activity or playback screen
- implement `SurfaceHolder.Callback`
- log:
  - surface created
  - surface changed
  - surface destroyed
- expose current `Surface` to decoder layer

**Deliverable:**
- a visible fullscreen rendering surface with clear lifecycle logs

**Acceptance criteria:**
- app starts with fullscreen SurfaceView
- logs confirm surface creation and destruction events

---

## Stage 5 — Implement MediaCodec HEVC decoder wrapper

### Task 5.1 — Create HevcDecoder abstraction

**Goal:** Isolate all Android decode logic into one focused component.

**Suggested class:**
- `HevcDecoder.kt`

**Required methods:**
- `start(surface: Surface)`
- `queueAccessUnit(data: ByteArray, ptsUs: Long, isKeyframe: Boolean)`
- `stop()`
- `release()`

**Actions:**
- create MediaFormat for `video/hevc`
- use `MediaCodecList.findDecoderForFormat(...)` to locate compatible decoder
- instantiate codec by name
- configure codec to decode to provided `Surface`
- start codec

**Deliverable:**
- reusable decoder wrapper with logs around initialization and shutdown

**Acceptance criteria:**
- logs show selected decoder name
- decoder config succeeds without exception on target device

### Task 5.2 — Implement input queue logic

**Goal:** Feed parsed HEVC data into MediaCodec.

**Actions:**
- implement input buffer dequeue/queue flow
- assign monotonic `ptsUs`
- log queued input count and buffer sizes
- handle temporary backpressure cleanly

**Deliverable:**
- decoder accepts input units from bridge layer

**Acceptance criteria:**
- no immediate codec failure when input starts
- input queue activity visible in logs

### Task 5.3 — Implement output drain / render logic

**Goal:** Confirm that decoder outputs frames and renders them to Surface.

**Actions:**
- implement synchronous drain loop or callback-based mode
- log:
  - output format changed
  - rendered frame count
  - codec exceptions
- release output buffers for rendering to surface

**Deliverable:**
- decoder output path with visible diagnostics

**Acceptance criteria:**
- output callbacks or output buffer events appear in logs
- successful render path is observable

---

## Stage 6 — Build the native-to-Java bridge

### Task 6.1 — Define the bridge contract

**Goal:** Make data handoff between native parser output and Android decoder explicit and stable.

**Bridge payload should include:**
- parsed HEVC data buffer
- `ptsUs`
- `isKeyframe`
- optional `isConfig`

**Actions:**
- define JNI method(s) or callback interface
- avoid calling Java for tiny RTP fragments
- pass parser output at the level of complete parser buffers, ideally complete access units

**Deliverable:**
- documented bridge interface between native code and Kotlin/Java

**Acceptance criteria:**
- one clear code path exists from appsink callback to `HevcDecoder.queueAccessUnit(...)`

### Task 6.2 — Add bounded buffering between native and decoder

**Goal:** Avoid UI-thread or JNI-induced instability.

**Actions:**
- add a small bounded queue between native callback and decoder thread
- drop oldest or newest items according to chosen policy when queue is full
- log queue pressure events

**Deliverable:**
- simple bridge buffering with predictable behavior under load

**Acceptance criteria:**
- no unbounded growth in memory or queued samples

---

## Stage 7 — Get first picture on screen

### Task 7.1 — Run end-to-end path

**Goal:** Validate full hybrid playback chain.

**Path:**

`GStreamer ingest/parsing -> native bridge -> MediaCodec -> SurfaceView`

**Actions:**
- start pipeline only after surface is ready, or hold decoder startup until surface exists
- start decoder before first sample is pushed
- observe logs at every boundary

**Deliverable:**
- first visible decoded video frame on the Android screen

**Acceptance criteria:**
- visible moving image appears on screen
- logs show samples entering the bridge and frames being rendered

### Task 7.2 — Add explicit failure diagnostics

**Goal:** Make the first failure easy to localize if no picture appears.

**Actions:**
- for startup, print a compact diagnostic block containing:
  - pipeline state
  - appsink sample count
  - first sample size
  - decoder selected
  - decoder started yes/no
  - input queued count
  - output rendered count
- on failure, print the first stage where counters stop increasing

**Deliverable:**
- one concise startup/runtime diagnostic view in logs

**Acceptance criteria:**
- if playback fails, it is immediately obvious whether failure is in ingest, parser output, bridge, decoder input, or decoder output

---

## Stage 8 — Post-first-image stabilization

These tasks are intentionally lower priority and should begin only after first visible playback works.

### Task 8.1 — Improve timestamp handling

**Goal:** Replace rough monotonic PTS with better timing derived from parser/RTP data where practical.

### Task 8.2 — Handle packet loss / corruption recovery

**Goal:** Reset or resync decoder cleanly after severe corruption and wait for next IDR.

### Task 8.3 — Tune latency

**Goal:** Minimize delay by reducing unnecessary buffering in:
- rtpjitterbuffer
- bridge queue
- decoder feed logic

### Task 8.4 — Add optional stats overlay or debug panel

**Goal:** Show live counters for:
- RTP/appsink samples
- bridge queue depth
- decoder input count
- rendered frame count

---

## Explicit non-goals for this branch

The following are **not** goals for the current branch unless first-image playback is already complete:

- GStreamer-based Android rendering
- `androidmedia` recovery experiments
- `gstlibav` static-link fixes
- OpenGL sink experiments
- RTSP/SRT output
- recording to file
- audio support
- fancy UI
- adaptive streaming behavior

---

## Suggested execution order for Codex

1. Remove GStreamer decode/render branches from active path.
2. Lock parse-only ingest pipeline with appsink.
3. Add reliable sample logging at appsink boundary.
4. Add fullscreen SurfaceView.
5. Implement `HevcDecoder` wrapper around MediaCodec.
6. Implement JNI/native bridge from appsink to decoder.
7. Feed parser output into decoder.
8. Get first visible frame.
9. Only after that: debug format edge cases, timing, recovery, latency.

---

## Done definition

This branch is considered successful when all of the following are true:

- the Android app no longer depends on GStreamer for decode/render
- GStreamer is used only for UDP/RTP/H.265 ingest and parse
- parsed HEVC data crosses the native bridge into Android code
- MediaCodec successfully decodes to a SurfaceView
- live video is visible on screen from the incoming RTP/H.265 stream

---

## Short task summary

**Primary objective:**
Get first visible live video on Android by keeping GStreamer only for `udpsrc -> rtpjitterbuffer -> rtph265depay -> h265parse` and moving decode/render to Android `MediaCodec` + `SurfaceView`.

**Immediate next milestone:**
Appsink receives parsed H.265 buffers and those buffers are successfully fed into `MediaCodec`.
