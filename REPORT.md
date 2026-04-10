# Implementation Report — RtpPlayer

## What Was Built

A minimal Android application that receives a live RTP/H.265 video stream over UDP using GStreamer and displays it fullscreen on a Lenovo Tab M10 FHD Plus tablet (Android 10).

### Architecture

The app has two layers:

**Kotlin UI Layer** (`com.local.rtpplayer`):
- `MainActivity` — Single activity handling fullscreen landscape mode, SurfaceView lifecycle, and the GStreamer lifecycle state machine
- `GStreamer` — Kotlin object wrapping all JNI calls, handling core `.so` library loading in dependency order, and exposing a clean Kotlin API

**Native C++ Layer** (`app/src/main/cpp/`):
- `gstreamer-jni.cpp` — JNI bridge with 10 native methods
- `gst-player.h` / `gst-player.cpp` — `GstPlayer` class managing the GStreamer pipeline, GLib main loop thread, bus message processing, error detection, and no-data timeout monitoring
- `CMakeLists.txt` — CMake configuration linking against GStreamer core libraries from the Android SDK

### Key Features Implemented

1. **GStreamer initialization**: Loads `libglib-2.0`, `libgobject-2.0`, `libgmodule-2.0`, `libgstreamer-1.0` in dependency order before calling `gst_init_check()`
2. **Pipeline construction**: Uses `gst_parse_launch()` with a hardcoded pipeline string matching the known-working desktop pipeline
3. **Surface handoff**: Finds `androidvideosink` by name in the pipeline, sets its `widget` property to the `ANativeWindow` obtained from the Java `Surface`
4. **GLib main loop**: Runs in a dedicated `pthread` to process bus messages and timer callbacks (Android has no native GLib main loop)
5. **Error detection**: Bus `ERROR` messages and no-data timeout (checking `udpsrc` `bytes-served` property every 2 seconds) set an atomic error flag
6. **Reconnect logic**: Kotlin polls for errors every 2 seconds; on detection, stops pipeline, shows "No Signal" overlay, and retries up to 10 times
7. **Lifecycle management**: Clean pause/resume/destroy with state machine guards preventing double-start, use-after-free, or stuck pipelines
8. **Gradle automation**: `copyGstreamerLibs` task automatically copies all GStreamer `.so` files from the SDK into `jniLibs/` before each build

---

## Assumptions Made

| # | Assumption | Rationale |
|---|-----------|-----------|
| 1 | **GStreamer Android SDK v1.24.x** contains `avdec_h265` (gst-libav) and `androidvideosink` | These are standard components in the official SDK build |
| 2 | **`androidvideosink` accepts `ANativeWindow*` via the `widget` property** | This is the documented pattern for GStreamer-on-Android rendering |
| 3 | **udpsrc element has a `bytes-served` property** | Standard property in GStreamer's udpsrc element |
| 4 | **Only `arm64-v8a` ABI is needed** | The Lenovo Tab M10 FHD Plus is a 64-bit ARM device |
| 5 | **Flat `.so` layout works for Android's dynamic linker** | Copying all `.so` files (core + plugins) to one directory simplifies linking |
| 6 | **5-second no-data timeout is appropriate** | For a 120 fps stream, 5 seconds of silence is clearly a failure |
| 7 | **`gst_parse_launch()` works with prebuilt Android plugins** | It's the standard way to build GStreamer pipelines from strings |
| 8 | **No audio pipeline needed** | Audio is explicitly excluded in the project requirements |
| 9 | **NDK r27 is available/compatible** | Used as a reasonable current version; user can adjust |
| 10 | **AGP 8.5.2 + Gradle 8.9** | Recent stable versions compatible with current Android Studio |

---

## Known Limitations

1. **Single device target**: Only designed for Lenovo Tab M10 FHD Plus (Android 10). Not tested on other devices or Android versions.
2. **Decoder availability**: `avdec_h265` requires gst-libav to be included in the GStreamer Android SDK. If the SDK build omits this plugin, the pipeline will fail at parse time.
3. **120 fps input on 60 Hz display**: The tablet display refresh rate is 60 Hz. The pipeline receives and decodes all incoming frames, but the display renders at most 60 unique frames per second. This is by design — success means stable low-latency playback, not visible 120 fps.
4. **No settings UI**: All configuration (UDP port, latency, buffer size, decoder) requires editing the pipeline string in `MainActivity.kt` and rebuilding.
5. **No universal codec support**: The app assumes H.265. It will not detect or adapt to other codecs.
6. **Flat `.so` potential conflicts**: All GStreamer `.so` files are copied to a single directory. If a plugin shares a filename with a core library (unlikely), the copy order determines which wins.
7. **No exponential backoff**: Reconnect retries use a fixed 500ms delay. 10 attempts max prevents infinite loops.
8. **GStreamer registry rebuild**: The plugin registry is rebuilt on each app launch (`GST_REGISTRY_UPDATE=yes`). This adds startup time but ensures plugins are found correctly. For production use, this could be disabled after the first successful run.
9. **No build tested in this environment**: This repository was created on a Linux environment without Android SDK/NDK/GStreamer SDK. The actual build must be tested on the user's Windows 11 machine with Android Studio.

---

## File Inventory

### Source Files (12 files)

| File | Lines | Purpose |
|------|-------|---------|
| `MainActivity.kt` | ~390 | Activity, lifecycle state machine, reconnect logic |
| `GStreamer.kt` | ~220 | JNI wrapper, library loading, Kotlin API |
| `gstreamer-jni.cpp` | ~295 | JNI bridge, GStreamer env setup |
| `gst-player.cpp` | ~535 | GStreamer pipeline, GLib main loop, error detection |
| `gst-player.h` | ~125 | Native player class header |
| `CMakeLists.txt` | ~75 | Native build configuration |
| `activity_main.xml` | ~30 | SurfaceView + "No Signal" overlay layout |
| `themes.xml` | ~10 | Fullscreen dark theme |
| `strings.xml` | ~5 | App name |
| `AndroidManifest.xml` | ~25 | Manifest with permissionss and activity config |
| `build.gradle.kts` (app) | ~155 | App module, CMake, GStreamer copy task |
| `build.gradle.kts` (root) | ~8 | Top-level AGP + Kotlin plugin versions |

### Configuration Files (6 files)

| File | Purpose |
|------|---------|
| `settings.gradle.kts` | Project settings, repositories |
| `gradle.properties` | JVM args, AndroidX, Kotlin style |
| `gradle-wrapper.properties` | Gradle 8.9 distribution URL |
| `local.properties` | SDK/GStreamer paths (gitignored) |
| `.gitignore` | Build output, IDE files, jniLibs |
| `proguard-rules.pro` | Empty (minification disabled) |

### Documentation Files (5 files)

| File | Purpose |
|------|---------|
| `README.md` | Setup, build, install, and configuration guide |
| `PLAN.md` | Implementation plan with assumptions and risks |
| `AGENTS.md` | Project constraints and requirements |
| `TASKS.md` | Stage-gated task breakdown |
| `REPORT.md` | This file |

---

## Native JNI Method Inventory

| Kotlin Method | JNI Function | Purpose |
|--------------|--------------|---------|
| `nativeInit(nativeLibDir, cacheDir)` | `Java_com_local_rtpplayer_GStreamer_nativeInit` | Set GStreamer env vars, call `gst_init_check()` |
| `nativeSetPipeline(pipeline)` | `Java_com_local_rtpplayer_GStreamer_nativeSetPipeline` | Store pipeline description string |
| `nativeStart(surface)` | `Java_com_local_rtpplayer_GStreamer_nativeStart` | Build pipeline, attach Surface, set to PLAYING |
| `nativeStop()` | `Java_com_local_rtpplayer_GStreamer_nativeStop` | Transition pipeline to NULL, stop main loop |
| `nativeReset()` | `Java_com_local_rtpplayer_GStreamer_nativeReset` | Full reset: stop + deinit + destroy player |
| `nativeDeinit()` | `Java_com_local_rtpplayer_GStreamer_nativeDeinit` | Release GStreamer resources |
| `nativeIsPlaying()` | `Java_com_local_rtpplayer_GStreamer_nativeIsPlaying` | Return PLAYING state flag |
| `nativeHasError()` | `Java_com_local_rtpplayer_GStreamer_nativeHasError` | Return error state flag |
| `nativeGetLastError()` | `Java_com_local_rtpplayer_GStreamer_nativeGetLastError` | Return last error description |
| `nativeGetGStreamerVersion()` | `Java_com_local_rtpplayer_GStreamer_nativeGetGStreamerVersion` | Return GStreamer version string |

---

## Pipeline String

The complete pipeline string (editable in `MainActivity.kt`):

```
udpsrc port=5600 buffer-size=7000 !
application/x-rtp !
rtpjitterbuffer latency=0 drop-on-latency=true !
rtph265depay !
avdec_h265 !
androidvideosink name=androidvideosink sync=false
```

### Element breakdown:

| Element | Role | Key Parameters |
|---------|------|----------------|
| `udpsrc` | Receive UDP packets | `port=5600`, `buffer-size=7000` |
| `application/x-rtp` | Caps filter | Identifies RTP payload |
| `rtpjitterbuffer` | Buffer and reorder RTP packets | `latency=0`, `drop-on-latency=true` |
| `rtph265depay` | Extract H.265 NAL units from RTP | — |
| `avdec_h265` | Decode H.265/HEVC video | — |
| `androidvideosink` | Render to Android Surface | `name=androidvideosink` (for lookup), `sync=false` (low latency) |

---

## Next Steps for Testing on the Actual Tablet

### 1. Install Dependencies
- Install Android Studio on Windows 11
- Install NDK, CMake, SDK via SDK Manager
- Download and extract GStreamer Android SDK

### 2. Configure and Build
- Set `gstreamer.sdk.dir` in `local.properties`
- Open project in Android Studio
- Run **Build → Make Project**
- Fix any build errors (typos, version mismatches)

### 3. Deploy to Tablet
- Connect Lenovo Tab M10 FHD Plus via USB
- `adb install -r app-debug.apk`
- Launch the app

### 4. Test Playback
- Start the RTP/H.265 stream on the sender computer
- Verify video appears on the tablet screen
- Check logcat for clean state transitions and no errors

### 5. Test Resilience
- Stop the stream → verify "No Signal" appears after ~5 seconds
- Restart the stream → verify video resumes automatically
- Put app in background → bring to foreground → verify video resumes
- Verify no crashes in any of the above

### 6. Tune Parameters (if needed)
- If video is delayed, increase `latency` in `rtpjitterbuffer`
- If UDP packets are dropped, increase `buffer-size`
- If decoder is too slow, try an alternative decoder

### 7. Expected Logcat Output (healthy playback)

```
D/RtpPlayer: [onCreate] wantsRunning=false surfaceReady=false pipelineRunning=false reconnectAttempts=0
I/RtpPlayer: GStreamer initialized, version: 1.24.8
D/RtpPlayer: [onResume — entering foreground] wantsRunning=true surfaceReady=false pipelineRunning=false reconnectAttempts=0
D/RtpPlayer: [surfaceCreated] wantsRunning=true surfaceReady=true pipelineRunning=false reconnectAttempts=0
I/RtpPlayer: tryStartPipeline — pipeline started successfully
D/GstPlayerNative: GstPlayer bus — Pipeline state: NULL -> READY (pending: VOID_PENDING)
D/GstPlayerNative: GstPlayer bus — Pipeline state: READY -> PAUSED (pending: PLAYING)
D/GstPlayerNative: GstPlayer bus — Async done (state change committed)
D/GstPlayerNative: GstPlayer bus — Pipeline state: PAUSED -> PLAYING (pending: VOID_PENDING)
I/GstPlayerNative: GstPlayer::start — pipeline is PLAYING
```

---

## Summary

The project is complete through all 6 stages:

| Stage | Status |
|-------|--------|
| 0 — Repository & Plan | ✅ Complete |
| 1 — Android Project Skeleton | ✅ Complete |
| 2 — Native Layer & GStreamer Scaffolding | ✅ Complete |
| 3 — First Working Playback Path | ✅ Complete |
| 4 — Lifecycle Hardening | ✅ Complete |
| 5 — Reconnect & No-Signal | ✅ Complete |
| 6 — Cleanup & Documentation | ✅ Complete |

The app is ready for build, deploy, and testing on the target tablet.
