# AGENTS.md

## Project purpose

This repository contains a **minimal Android application** for **personal use only**.

The application must:

* receive a live **RTP/H.265 (HEVC)** video stream over **UDP**
* use **GStreamer on Android** as the media engine
* display the video **fullscreen** on **one specific Android tablet**
* prioritize **first working video** and **minimal latency**
* avoid unnecessary architecture, abstraction, and compatibility work

This is **not** a general-purpose media player.

---

## Scope

### In scope

* Android app for a known tablet
* Android Studio project
* Gradle build
* Android NDK + CMake integration
* GStreamer Android integration
* fullscreen landscape playback
* SurfaceView-based rendering
* fixed UDP port and fixed known stream profile for MVP
* lifecycle-safe start/stop/pause/resume
* logcat diagnostics
* simple reconnect / no-signal handling if needed after MVP playback works

### Out of scope

Do **not** add any of the following unless explicitly requested by the user:

* ExoPlayer / Media3 / VLC / FFmpeg-player replacements
* support for many Android devices
* protocol abstraction
* multiple stream profiles
* RTSP, SRT, NDI
* recording
* audio playback
* Google Play publishing requirements
* background playback
* Picture-in-Picture
* advanced settings UI
* analytics / telemetry / crash reporting
* unnecessary third-party libraries
* “future-proof” architecture layers

---

## Hard technical requirements

* **GStreamer is mandatory**
* Use **SurfaceView** unless the user explicitly asks otherwise
* Prefer the **simplest working solution**
* Optimize for **one known device**, not broad compatibility
* Keep the codebase easy to understand and modify
* Favor directness over elegance
* Document assumptions clearly

---

## Target device

* Tablet model: `Lenovo Tab M10 FHD Plus`
* Android version: `Android 10`
* Display refresh rate: `60 Hz`

Important:

* The input stream may be `1280x720 @ 120 fps`
* The tablet display is `60 Hz`
* Therefore, success does **not** mean showing 120 unique frames per second on screen
* Success means receiving the known stream reliably and displaying it with minimal latency on this device

---

## Known stream assumptions

* Transport: `UDP + RTP`
* Codec: `H.265 / HEVC`
* Resolution target: `1280x720`
* Source framerate target: `120 fps`
* Audio: `not used`
* Receiver port on tablet: `5600`
* For MVP, the app should listen on the required UDP port
* A fixed sender IP is **not required** for MVP, because the stream is pushed to the receiver device and port directly

Reference desktop pipeline:

```text id="d3j56e"
gst-launch-1.0 -v udpsrc port=5600 buffer-size=7000 caps="application/x-rtp" ! rtpjitterbuffer latency=0 drop-on-latency=true ! rtph265depay ! avdec_h265 max-threads=8 ! fpsdisplaysink video-sink=d3d11videosink sync=true text-overlay=true
```

Notes:

* `rtph265depay` remains the correct depayloader conceptually on Android as well
* The Android implementation may need a different decoder and sink path
* Stay as close as practical to the known working desktop pipeline
* Do not redesign the ingest path unless necessary

---

## Implementation priorities

Work in this order:

1. Project builds successfully
2. App launches successfully
3. GStreamer initializes successfully
4. Video appears on screen
5. Lifecycle is stable
6. Reconnect / no-signal behavior
7. README / cleanup / polish

Do **not** optimize or polish before video is working.

---

## Required workflow discipline

The repository includes a `TASKS.md` file with staged work.

You must follow these rules:

* Work strictly **stage by stage**
* At the end of each stage, stop and wait for **explicit human approval**
* Do **not** start the next stage automatically
* Do **not** silently expand scope
* Do **not** merge multiple stages into one unless explicitly told to do so
* For each stage, produce:

  * a short implementation summary
  * exact files created/changed
  * exact commands run
  * any assumptions made
  * any blockers or uncertainties
* Then stop and ask for approval to continue

---

## Expected deliverables

By the end of the project, provide:

* complete Android Studio project
* working Gradle configuration
* NDK/CMake integration
* native GStreamer bridge
* fullscreen playback UI
* logcat logging
* README.md with setup/build/install instructions
* concise final implementation report

---

## Build expectations

Target environment:

* Windows 11 development machine
* Android Studio
* Android SDK
* Android NDK
* CMake
* GStreamer Android binaries

The project should be buildable from Android Studio with minimal manual setup beyond documented SDK/GStreamer path configuration.

---

## Code style expectations

* Keep files small and readable
* Use descriptive names
* Avoid clever abstractions
* Minimize indirection
* Add comments only where they help explain integration decisions
* Prefer predictable lifecycle handling over aggressive optimization
* Log important state transitions

---

## Decision policy

When unsure, choose the option that is:

1. simpler
2. easier to debug
3. closer to the user's stated goal
4. less abstract
5. less broad in scope

If a requirement is ambiguous, make the smallest reasonable assumption, document it, and stop at the stage boundary for review.

---

## Definition of success

Success for this project means:

* the app builds
* the app installs on the target tablet
* the app launches directly into playback mode
* it attempts playback automatically
* it listens on UDP port `5600`
* it displays the RTP/H.265 live stream on screen
* it does not immediately fall apart on pause/resume
* the repository documents how to rebuild and modify it

Not required for success:

* universal Android support
* store-ready polish
* feature richness
* protocol flexibility
* actual 120 fps visible presentation on a 60 Hz screen
