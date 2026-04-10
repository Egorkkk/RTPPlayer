# Implementation Plan — RTP/H.265 Android Player

## Overview

Build a minimal Android app that receives a live RTP/H.265 video stream over UDP using GStreamer and displays it fullscreen on a Lenovo Tab M10 FHD Plus (Android 10).

## Execution Stages

| Stage | Name | Goal |
|-------|------|------|
| 0 | Repository & Plan | Skeleton + detailed plan (current stage) |
| 1 | Android Project Skeleton | Minimal Android Studio project, MainActivity, SurfaceView placeholder |
| 2 | Native Layer & GStreamer Scaffolding | CMake, JNI bridge, GStreamer SDK integration prep |
| 3 | First Working Playback | Real GStreamer pipeline, video on screen |
| 4 | Lifecycle Hardening | Safe pause/resume/destroy |
| 5 | Reconnect & No-Signal | Graceful missing-stream behavior |
| 6 | Cleanup & Documentation | README, report, handoff |

---

## Detailed Task Breakdown

### Stage 1 — Android Project Skeleton
1. Create Android Studio project structure manually (no IDE needed for file creation)
2. Write top-level `build.gradle.kts` and `settings.gradle.kts`
3. Write app module `build.gradle.kts` with minimal config
4. Write `AndroidManifest.xml` — landscape, immersive, keep-screen-on
5. Write `MainActivity.kt` — single activity, fullscreen, SurfaceView placeholder
6. Write `activity_main.xml` layout with SurfaceView
7. Write `gradle.properties`, `local.properties` placeholder

### Stage 2 — Native Layer & GStreamer Scaffolding
1. Create `src/main/cpp/` directory
2. Write `CMakeLists.txt` — configure native library build
3. Write `gstreamer-jni.cpp` — JNI bridge for init/start/stop/setSurface
4. Write `gst-player.h` / `gst-player.cpp` — native GStreamer player wrapper
5. Update app `build.gradle.kts` — add externalNativeBuild, ndk config
6. Add GStreamer Android SDK location config (local.properties or gradle property)
7. Add basic logcat logging in native code
8. Load native library in Kotlin (`System.loadLibrary`)

### Stage 3 — First Working Playback
1. Implement GStreamer pipeline string for Android:
   ```
   udpsrc port=5600 buffer-size=7000 ! application/x-rtp !
   rtpjitterbuffer latency=0 drop-on-latency=true !
   rtph265depay !
   avdec_h265 !
   androidvideosink target=surface sync=false
   ```
2. Pass Surface from Kotlin to native via JNI
3. Initialize GStreamer on app start (call `GStreamer.init()`)
4. Build and start pipeline in `onResume`
5. Stop pipeline in `onPause`
6. Add logcat output for every state transition
7. Test build (debug APK)

### Stage 4 — Lifecycle Hardening
1. Clean pipeline teardown on `onPause` / `onDestroy`
2. Handle Surface lifecycle changes (`surfaceCreated` / `surfaceDestroyed`)
3. Prevent double-start or use-after-free
4. Validate pipeline state transitions
5. Add clearer logcat diagnostics

### Stage 5 — Reconnect & No-Signal
1. Detect when no data arrives (GStreamer bus messages / timeout)
2. Optionally restart pipeline on error
3. Show simple overlay text when no signal (or just keep trying silently)
4. Ensure no crashes when stream is absent

### Stage 6 — Cleanup & Documentation
1. Remove dead code and comments
2. Write comprehensive `README.md`
3. Write implementation report
4. Final review of all files

---

## Assumptions

### Target Tablet
- **Device**: Lenovo Tab M10 FHD Plus
- **Android version**: Android 10 (API 29)
- **Screen**: 60 Hz refresh rate
- **Orientation**: Landscape only, fullscreen
- We are targeting ONE device, so no compatibility shims for other devices

### Network / Stream
- **Transport**: UDP multicast or unicast (app just listens on port 5600)
- **Protocol**: RTP/H.265
- **Resolution**: 1280x720
- **Framerate**: 120 fps target from source
- **Audio**: Not needed, ignored
- **UDP port**: 5600 (hardcoded in pipeline)
- No hardcoded sender IP needed — the tablet just listens

### 120 fps Input vs 60 Hz Display
- The tablet display is 60 Hz, so it cannot show 120 unique frames per second
- This is acceptable — GStreamer/androidvideosink will naturally drop frames
- The pipeline will still receive and decode all incoming data
- Success = stable low-latency playback, not visible 120 fps

### GStreamer on Android
- Will use official GStreamer Android SDK from https://gstreamer.freedesktop.org/data/pkg/android/
- The SDK provides prebuilt `.so` files and headers
- `androidvideosink` is the standard video sink for Android Surface rendering
- Decoder: `avdec_h265` (libav-based) should be available in the GStreamer Android build
  - If not available, may need `openh264` or another H.265 decoder from the SDK
- Pipeline will stay close to the known-working desktop pipeline

### Project Build
- Host: Windows 11 with Android Studio
- We create project files manually but they must be fully compatible with Android Studio
- Gradle version: use recent stable (8.x)
- AGP (Android Gradle Plugin): 8.x
- NDK: r27 or whatever is current/stable
- CMake: 3.22.1+ (bundled with Android Studio)

### UI / UX
- Single activity, no fragments, no navigation
- Immersive fullscreen (hide system bars)
- `FLAG_KEEP_SCREEN_ON` or `View.keepScreenOn`
- No settings UI for MVP
- No audio controls, no stream selector

---

## Key Technical Decisions

1. **SurfaceView over TextureView**: Simpler, lower latency, works well with GStreamer's `androidvideosink`
2. **JNI bridge in C++**: Direct control over GStreamer lifecycle, no Java wrapper library needed
3. **Pipeline string hardcoded**: No runtime configuration for MVP
4. **No audio pipeline**: Audio is explicitly out of scope
5. **Manual project file creation**: We create all files by hand so they're clean and minimal, but fully Android Studio compatible
6. **GStreamer SDK as prebuilt**: We reference the official prebuilt Android binaries, no need to build GStreamer from source

---

## Known Risks / Open Questions

1. **GStreamer Android SDK version**: Which exact version to use? Latest 1.24.x recommended. Need to confirm the user has or can download it.
2. **H.265 decoder availability**: `avdec_h265` must be in the GStreamer Android build. If the `ffmpeg` plugin is not included in the standard Android SDK, we may need `d3d11h265dec` equivalent or MediaCodec-based decoder. The GStreamer Android SDK typically includes a decent set of plugins.
3. **`androidvideosink` behavior**: May need specific properties set (e.g., `target=surface` or passing ANativeWindow via JNI). The exact JNI integration pattern needs verification.
4. **Buffer size on Android**: `buffer-size=7000` from desktop may need tuning on Android.
5. **Latency tuning**: `rtpjitterbuffer latency=0` may be too aggressive on Android. May need a small buffer (e.g., 50-200ms).
6. **Threading**: GStreamer will run on its own threads. Need to ensure JNI calls are thread-safe and the GL/Surface context is handled correctly.
