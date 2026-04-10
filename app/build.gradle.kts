import java.util.Properties
import java.io.File

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// ── Load local.properties for GStreamer SDK path ─────────────────────
val localProperties = Properties()
val localPropsFile = rootProject.file("local.properties")
if (localPropsFile.exists()) {
    localPropsFile.inputStream().use { localProperties.load(it) }
}

val gstreamerSdkDir = localProperties.getProperty("gstreamer.sdk.dir", "")
val gstreamerSdkPath = if (gstreamerSdkDir.isNotEmpty()) file(gstreamerSdkDir) else null

// ABI we target
val TARGET_ABI = "arm64-v8a"

// ── Task: Copy GStreamer .so files into jniLibs ──────────────────────
// This task copies the required GStreamer core and plugin .so files from
// the SDK into src/main/jniLibs/ so they are packaged into the APK.
//
// All .so files are copied to a FLAT directory structure (no subdirectories)
// so the Android dynamic linker can find them all without extra path config.
val copyGstreamerLibs = tasks.register("copyGstreamerLibs") {
    description = "Copy GStreamer .so files from SDK into jniLibs"
    group = "build"

    doLast {
        if (gstreamerSdkPath == null || !gstreamerSdkPath.exists()) {
            logger.warn("WARNING: GStreamer SDK directory not found. " +
                "Set gstreamer.sdk.dir in local.properties.")
            return@doLast
        }

        val gstAbi = when (TARGET_ABI) {
            "arm64-v8a" -> "arm64"
            "armeabi-v7a" -> "armv7"
            else -> TARGET_ABI
        }
        val abiLibDir = File(gstreamerSdkPath, "$gstAbi/lib")
        if (!abiLibDir.exists()) {
            logger.error("ERROR: GStreamer SDK lib directory not found: ${abiLibDir.path}")
            return@doLast
        }

        val jniLibsBase = File(projectDir, "src/main/jniLibs/$TARGET_ABI")
        jniLibsBase.mkdirs()

        var copied = 0

        // Copy core .so files (everything directly in lib/arm64-v8a/)
        abiLibDir.listFiles { f ->
            f.isFile && f.name.endsWith(".so")
        }?.forEach { src ->
            val dst = File(jniLibsBase, src.name)
            if (!dst.exists() || src.lastModified() > dst.lastModified()) {
                project.copy {
                    from(src)
                    into(jniLibsBase)
                }
                copied++
                logger.info("  Copied core .so: ${src.name}")
            }
        }

        // Also copy plugin .so files from lib/arm64-v8a/gstreamer-1.0/
        // into the SAME flat directory so the dynamic linker finds them.
        val sdkPluginsDir = File(abiLibDir, "gstreamer-1.0")
        if (sdkPluginsDir.exists()) {
            sdkPluginsDir.listFiles { f ->
                f.isFile && f.name.endsWith(".so")
            }?.forEach { src ->
                val dst = File(jniLibsBase, src.name)
                if (!dst.exists() || src.lastModified() > dst.lastModified()) {
                    project.copy {
                        from(src)
                        into(jniLibsBase)
                    }
                    copied++
                    logger.info("  Copied plugin .so: ${src.name}")
                }
            }
        }

        logger.quiet("GStreamer libs copied: $copied files to $jniLibsBase")
    }
}

// Hook: run copy task before preBuild
tasks.named("preBuild") {
    dependsOn(copyGstreamerLibs)
}

android {
    namespace = "com.local.rtpplayer"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.local.rtpplayer"
        minSdk = 29       // Android 10 — matches target tablet
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a")
        }

        // Pass GStreamer SDK path to CMake
        externalNativeBuild {
            cmake {
                if (gstreamerSdkDir.isNotEmpty()) {
                    arguments("-DGSTREAMER_SDK_DIR=${gstreamerSdkDir}")
                }
            }
        }
    }

    buildTypes {
        debug {
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    // ── Native build configuration ────────────────────────────────────
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    ndkVersion = "26.1.10909125"
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
}
