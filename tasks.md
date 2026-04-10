# TASKS.md

## Execution rule

**Important:** after completing each stage, stop and wait for explicit human approval.

Do not proceed to the next stage automatically.

For every completed stage, provide:

1. a short summary of what was done
2. the exact files added or changed
3. the exact commands run
4. any assumptions made
5. any blockers, risks, or open questions

Then stop and ask for approval.

---

## Stage 0 — repository and implementation plan

### Goal

Set up the repository skeleton and produce a concrete execution plan before implementation begins.

### Tasks

* Inspect the repository state
* Create a short implementation plan
* Create a detailed task breakdown
* Record assumptions about:

  * target tablet
  * Android version
  * receiver UDP port
  * expected pipeline behavior
  * 120 fps input vs 60 Hz display limitation
* Create initial project documentation files if missing
* Do **not** start Android implementation yet

### Expected output

* initial repository structure if needed
* written plan
* written assumptions
* no heavy implementation yet

### Human approval gate

Stop after this stage and ask for approval before creating the Android project.

---

## Stage 1 — create Android project skeleton

### Goal

Create the minimal Android Studio project structure.

### Tasks

* Create Android Studio app project
* Configure Gradle
* Configure app module
* Add minimal MainActivity
* Set up fullscreen landscape app shell
* Add SurfaceView placeholder in UI
* Add keep-screen-on behavior
* Ensure the project structure is clean and minimal
* Ensure the project can at least sync/open in Android Studio

### Expected output

* Android project exists
* app module exists
* MainActivity exists
* SurfaceView placeholder exists
* basic app shell ready

### Human approval gate

Stop after this stage and ask for approval before adding native/GStreamer integration.

---

## Stage 2 — add native layer and GStreamer integration scaffolding

### Goal

Integrate the native layer and prepare the project for GStreamer on Android.

### Tasks

* Add NDK/CMake configuration
* Add JNI/native bridge files
* Wire native library loading into the app
* Add GStreamer Android integration scaffolding
* Add placeholders for pipeline creation/start/stop
* Ensure the project still syncs/builds structurally
* Add basic logcat logging for native init path

### Expected output

* CMakeLists.txt created
* native source files created
* JNI bridge connected
* app prepared for GStreamer SDK integration

### Human approval gate

Stop after this stage and ask for approval before implementing actual playback.

---

## Stage 3 — implement first working playback path

### Goal

Make video appear on screen.

### Tasks

* Implement SurfaceView/native surface handoff
* Initialize GStreamer correctly
* Build the first working Android pipeline for the known RTP/H.265 stream
* Listen on UDP port `5600`
* Start playback automatically on launch or resume
* Render video to the screen
* Add essential logs for:

  * pipeline creation
  * state changes
  * playback errors
* Keep implementation as direct as possible

### Expected output

* first real playback attempt implemented
* app should attempt to show live video
* logs should help diagnose pipeline issues

### Human approval gate

Stop after this stage and ask for approval before adding lifecycle hardening and reconnect behavior.

---

## Stage 4 — lifecycle hardening

### Goal

Make pause/resume/destroy behavior safe and predictable.

### Tasks

* Handle onResume / onPause / onDestroy cleanly
* Ensure the pipeline stops or pauses correctly
* Ensure surface recreation is handled safely
* Prevent broken pipeline state after app background/foreground transitions
* Improve logs around lifecycle transitions

### Expected output

* basic lifecycle stability
* reduced risk of stuck or invalid pipeline state

### Human approval gate

Stop after this stage and ask for approval before adding reconnect and no-signal handling.

---

## Stage 5 — reconnect and no-signal behavior

### Goal

Improve resilience when the stream is missing or interrupted.

### Tasks

* Add minimal reconnect or restart behavior if appropriate
* Ensure app does not crash when stream is absent
* Add optional “no signal” / status indication only if simple
* Keep this lightweight
* Do not add complex UI

### Expected output

* better behavior on missing/interrupted stream
* lightweight failure handling

### Human approval gate

Stop after this stage and ask for approval before cleanup and documentation finalization.

---

## Stage 6 — cleanup, documentation, and handoff

### Goal

Make the project usable by a human without reverse-engineering it.

### Tasks

* Clean up filenames and project structure if needed
* Remove unnecessary code and dead scaffolding
* Finalize README.md
* Document:

  * prerequisites
  * Android Studio setup
  * NDK/CMake setup
  * GStreamer SDK setup
  * build steps
  * adb install steps
  * where to edit UDP port and pipeline
* Write final implementation report
* Summarize known limitations and suggested next tests on the real tablet

### Expected output

* buildable documented project
* README complete
* clear handoff

### Human approval gate

Stop after this stage and wait for the human to accept completion or request follow-up changes.

---

## Stage reporting template

Use this format at the end of every stage:

### Stage completed

`<stage number and name>`

### What was done

* ...

### Files added

* ...

### Files changed

* ...

### Commands run

* ...

### Assumptions

* ...

### Open questions / risks

* ...

### Approval request

Please review this stage. I will not proceed to the next stage until you explicitly approve it.
