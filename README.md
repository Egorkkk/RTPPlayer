# RtpPlayer — Minimal RTP/H.265 Android Player

A minimal Android app that receives a live **RTP/H.265 (HEVC)** video stream over **UDP** using **GStreamer** and displays it fullscreen on a specific Android tablet.

This is a **personal-use tool**, not a general-purpose media player.

---

## Quick Overview

| Property | Value |
|----------|-------|
| **Transport** | UDP + RTP |
| **Codec** | H.265 / HEVC |
| **Resolution** | 1280×720 |
| **Source FPS** | 120 (target) |
| **UDP Port** | 5600 |
| **Target Device** | Lenovo Tab M10 FHD Plus (Android 10) |
| **Media Engine** | GStreamer |
| **Render Target** | SurfaceView + androidvideosink |

---

## Prerequisites

### 1. Android Studio (Windows 11)

Install [Android Studio](https://developer.android.com/studio) with the following SDK components:

| Component | Version | Notes |
|-----------|---------|-------|
| Android SDK | API 34 (compileSdk) | Available via SDK Manager |
| Android SDK Build-Tools | 34.x | |
| Android NDK | r27 (27.0.12077973) | Or update `ndkVersion` in `app/build.gradle.kts` |
| CMake | 3.22.1+ | Available via SDK Manager |

Install these via: **Tools → SDK Manager → SDK Tools tab**

### 2. GStreamer Android SDK

Download the **GStreamer Android SDK** from:

<https://gstreamer.freedesktop.org/data/pkg/android/>

Choose the latest **1.24.x** universal package. For example:
```
gstreamer-1.0-android-universal-1.24.8.tar.xz
```

Extract it to a known location on your Windows machine, for example:
```
C:\gstreamer-1.0-android-universal-1.24.8\
```

> **Important:** This SDK contains prebuilt `.so` files for `arm64-v8a` (and other ABIs). The app's Gradle task automatically copies these into the project during build.

### 3. Java JDK 17

Android Studio bundles JDK 17 by default. Verify in:
**File → Settings → Build, Execution, Deployment → Build Tools → Gradle → Gradle JDK**

---

## Project Setup

### Step 1: Configure local.properties

Open (or create) `local.properties` in the project root and set:

```properties
# GStreamer Android SDK path — REQUIRED
gstreamer.sdk.dir=C\:\\gstreamer-1.0-android-universal-1.24.8
```

> Replace the path with your actual extraction location.
> Note the double backslashes for Windows paths in `.properties` files.

The Android SDK path is usually set automatically by Android Studio. If not, add:
```properties
sdk.dir=C\:\\Users\\<YourName>\\AppData\\Local\\Android\\Sdk
```

### Step 2: Open in Android Studio

1. Launch Android Studio
2. **File → Open** → select the project root directory
3. Wait for Gradle sync to complete

### Step 3: Verify NDK Version

Open `app/build.gradle.kts` and check the `ndkVersion` line:

```kotlin
ndkVersion = "27.0.12077973"  // Update to match your installed NDK
```

If you have a different NDK version installed, update this value. You can find installed NDK versions in:
`<Android SDK>/ndk/`

---

## Building

### From Android Studio

1. **Build → Make Project** (or `Ctrl+F9`)
2. Or **Run → Run 'app'** to build and install on a connected device

### From Command Line (Windows)

Open a terminal in the project root and run:

```powershell
gradlew.bat assembleDebug
```

The debug APK will be output to:
```
app\build\outputs\apk\debug\app-debug.apk
```

> The `copyGstreamerLibs` Gradle task runs automatically before the build, copying all required GStreamer `.so` files from the SDK into the project.

---

## Installing on the Tablet

### Via adb

1. Connect the tablet via USB
2. Enable **Developer Options** and **USB Debugging** on the tablet
3. Run:

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

### Verify Installation

```powershell
adb shell pm list packages | findstr rtpplayer
# Expected output: package:com.local.rtpplayer
```

### Launch the App

```powershell
adb shell am start -n com.local.rtpplayer/.MainActivity
```

---

## Changing Configuration

### Changing the UDP Port

Open `app/src/main/java/com/local/rtpplayer/MainActivity.kt` and edit the `defaultPipeline` string:

```kotlin
private val defaultPipeline = buildString {
    append("udpsrc port=5600 buffer-size=7000 ! ")   // ← change port here
    append("application/x-rtp ! ")
    append("rtpjitterbuffer latency=0 drop-on-latency=true ! ")
    append("rtph265depay ! ")
    append("avdec_h265 ! ")
    append("androidvideosink name=androidvideosink sync=false")
}
```

### Changing the Decoder

If `avdec_h265` is not available in your GStreamer build, try:

```kotlin
append("openh265dec ! ")   // Alternative H.265 decoder
```

Or use hardware-accelerated decoding if available:

```kotlin
append("msdkh265dec ! ")   // Intel MediaSDK (if supported on device)
```

### Changing the Video Sink

For testing without display (e.g., to verify pipeline builds):

```kotlin
append("fakesink")   // Drops all frames, useful for debugging
```

### Adjusting Latency

If the stream is unstable with `latency=0`, try a small buffer:

```kotlin
append("rtpjitterbuffer latency=100 drop-on-latency=true ! ")  // 100ms buffer
```

### Adjusting Buffer Size

If you see UDP packet drops:

```kotlin
append("udpsrc port=5600 buffer-size=14000 ! ")   // Larger OS buffer
```

---

## Project Structure

```
rtpplayer/
├── app/
│   ├── build.gradle.kts          # App module config, GStreamer .so copy task
│   ├── proguard-rules.pro
│   └── src/main/
│       ├── AndroidManifest.xml   # Permissions, landscape orientation, fullscreen
│       ├── cpp/
│       │   ├── CMakeLists.txt    # Native build config, GStreamer SDK linkage
│       │   ├── gstreamer-jni.cpp # JNI bridge (10 native methods)
│       │   ├── gst-player.h      # Native player class header
│       │   └── gst-player.cpp    # Native player implementation
│       ├── java/com/local/rtpplayer/
│       │   ├── MainActivity.kt   # Single activity, fullscreen, lifecycle
│       │   └── GStreamer.kt      # Kotlin facade for GStreamer JNI calls
│       ├── res/
│       │   ├── layout/
│       │   │   └── activity_main.xml   # SurfaceView + "No Signal" overlay
│       │   └── values/
│       │       ├── strings.xml
│       │       └── themes.xml
│       └── jniLibs/              # Auto-populated by Gradle (gitignored)
├── build.gradle.kts              # Top-level build, AGP + Kotlin versions
├── settings.gradle.kts           # Project settings, repositories
├── gradle.properties             # Gradle JVM args, AndroidX
├── local.properties              # SDK/NDK/GStreamer paths (gitignored)
├── README.md                     # This file
├── PLAN.md                       # Implementation plan with assumptions
└── AGENTS.md                     # Project constraints and requirements
```

---

## Logcat Diagnostics

Filter logcat output to see app and GStreamer messages:

```powershell
adb logcat -s RtpPlayer:* RtpPlayer_GStreamer:* RtpPlayer_JNI:* GstPlayerNative:*
```

Key log messages to watch for:

| Message | Meaning |
|---------|---------|
| `GStreamer initialized, version: X.Y.Z` | GStreamer loaded successfully |
| `pipeline is PLAYING` | Pipeline started, expecting video |
| `no UDP data received for N seconds` | Stream not arriving |
| `ERROR: <message>` | GStreamer pipeline error |
| `surfaceCreated / surfaceDestroyed` | Surface lifecycle |
| `tryStartPipeline — pipeline started successfully` | Everything working |
| `No Signal` overlay shown | Stream absent, reconnecting |

---

## Troubleshooting

### Build fails with "GSTREAMER_SDK_DIR is not defined"

Set `gstreamer.sdk.dir` in `local.properties` to the path where you extracted the GStreamer Android SDK.

### Build fails with "GStreamer SDK lib directory not found"

Verify the SDK path. It should contain `lib/arm64-v8a/` with `.so` files inside.

### App crashes on launch with UnsatisfiedLinkError

Check that the `copyGstreamerLibs` Gradle task ran successfully. Look for:
```
GStreamer libs copied: N files
```
in the build output. If the SDK path is wrong, fix it in `local.properties` and rebuild.

### App launches but shows "No Signal"

1. Verify the sender computer is pushing RTP/H.265 to the tablet's IP on UDP port 5600
2. Check the tablet's network connection
3. Check logcat for GStreamer errors
4. Try increasing `buffer-size` and `latency` in the pipeline string

### Video appears but is choppy or delayed

1. Try `latency=100` or `latency=200` in the `rtpjitterbuffer` element
2. Increase `buffer-size` to `14000` or higher
3. Check that `avdec_h265` is not falling back to software decoding (if hardware decoder available)

### App works on emulator but not on device

The emulator may use different codec availability. Always test on the actual target tablet. The app is built specifically for `arm64-v8a` — ensure the tablet has a 64-bit ARM processor.

---

## Known Limitations

1. **Single device only**: Built for Lenovo Tab M10 FHD Plus (Android 10). Not tested on other devices.
2. **No audio**: Audio is explicitly excluded from the pipeline.
3. **No settings UI**: All configuration is done by editing the pipeline string in `MainActivity.kt`.
4. **H.265 decoder availability**: `avdec_h265` requires the `gst-libav` plugin in the GStreamer Android SDK. If unavailable, an alternative decoder must be used.
5. **No universal codec adaptation**: The app assumes H.265. It will not auto-detect or adapt to other codecs.
6. **Flat .so layout**: All GStreamer `.so` files (core + plugins) are copied to a single directory. This works for the Android dynamic linker but may cause naming conflicts if a plugin has the same name as a core library (unlikely in practice).
7. **120 fps on 60 Hz display**: The tablet display is 60 Hz. The pipeline receives and decodes all frames, but the display shows at most 60 unique frames per second.

---

## Reference Desktop Pipeline

The Android pipeline is modeled after this known-working desktop pipeline:

```
gst-launch-1.0 -v udpsrc port=5600 buffer-size=7000 ! application/x-rtp ! \
  rtpjitterbuffer latency=0 drop-on-latency=true ! rtph265depay ! \
  avdec_h265 max-threads=8 ! fpsdisplaysink video-sink=d3d11videosink sync=true text-overlay=true
```

Android adjustments:
- `fpsdisplaysink` → removed (no overlay on Android)
- `d3d11videosink` → `androidvideosink`
- `sync=true` → `sync=false` (no audio clock, minimal latency)
- `max-threads=8` → removed (decoder auto-negotiates threads)

---

## Development Notes

- **No Google Play**: This app is not intended for publication. No ProGuard, no app bundle, no store metadata.
- **Debug only**: The release build is minimally configured (no minification, no signing config).
- **Kotlin**: Used for the UI layer. No Jetpack Compose, no fragments, no navigation.
- **C++**: Used for the native GStreamer layer. No additional C++ libraries.
- **No third-party dependencies**: Only AndroidX core and appcompat.
