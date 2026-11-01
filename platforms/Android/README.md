# HiFace Android Integration Demo

English | [简体中文](README.zh-CN.md)

SDK / Demo: **0.8.3 Beta**.

On-device detection, capture, liveness, 1:1 comparison, 1:N/M:N search and face-library management.

**Model scope: The current model does not support face recognition for people under 12 and must not be used in financial scenarios.**

Guides: [SDK](docs/SDK.md) · [Demo](docs/DEMO.md).

## Build and run

Use Android Studio, JDK 17, Android SDK 36 / Build Tools 36.0.0, and Java source level 11. Minimum Android version is 6.0 / API 23. The AAR includes native libraries for `arm64-v8a` and `armeabi-v7a`; this Demo does not require an NDK installation. Camera validation requires an ARM phone.

Open this directory, sync dependencies, select `demo`, and run it on a phone. Grant camera access when requested. Application ID: `com.hiface.sdk.public`.

```sh
./gradlew :demo:assembleDebug
```

APK output: `demo/build/outputs/apk/debug/`.

## Integrating in your App

If this export contains `maven-repository/`, copy it into your project and add:

```kotlin
dependencyResolutionManagement {
    repositories {
        google()
        maven {
            url = uri("maven-repository")
            content { includeModule("io.github.facesdkpro", "hiface-android") }
        }
        mavenCentral()
    }
}
```

Use the SDK dependency and model compression configuration in the host App module:

```kotlin
dependencies {
    implementation("io.github.facesdkpro:hiface-android:0.8.3")
}
android {
    defaultConfig {
        minSdk = 23
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    androidResources { noCompress += "bphfm" }
}
```

Retain the AAR consumer R8 rules. Request `CAMERA` at runtime; online image import needs `INTERNET`.

Camera example: [CameraPreviewFragment](demo/src/main/java/com/hiface/sdk/demo/camera/CameraPreviewFragment.java). See the SDK guide for frame ownership and lifecycle.

## App signing

Sign Release APKs with your certificate. Retain [LICENSE](LICENSE).

The SDK is in iterative public Beta testing. Please keep the SDK version up to date. [Updates](https://github.com/FaceSDKPro/) · [Issues](https://github.com/FaceSDKPro/HelloFace/issues).
