# HiFace Android 集成 Demo

[English](README.md) | 简体中文

SDK / Demo：**0.8.3 Beta**

支持检测、采集、活体、1:1 比对、1:N/M:N 搜索和人脸库管理，算法在设备端运行。

**模型适用范围：当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。**

指南：[SDK](docs/SDK.zh-CN.md) · [Demo](docs/DEMO.zh-CN.md)。

## 运行

用 Android Studio 打开本目录，使用 JDK 17、Android SDK 36 / Build Tools 36.0.0，在 ARM 手机运行 `demo`。最低 Android 6.0 / API 23；支持 `arm64-v8a`、`armeabi-v7a`，构建 Demo 无需 NDK。

```sh
./gradlew :demo:assembleDebug
```

APK 位于 `demo/build/outputs/apk/debug/`。

## 集成到自己的 App

复制交付包的 `maven-repository/` 并配置仓库：

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

宿主 App 模块添加依赖和模型打包配置：

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

保留 consumer R8 规则，动态申请 `CAMERA`；网络导入需 `INTERNET`。

相机示例：[CameraPreviewFragment](demo/src/main/java/com/hiface/sdk/demo/camera/CameraPreviewFragment.java)。帧与生命周期要求见 SDK 指南。

## 应用签名

Debug 签名仅供测试；Release APK 使用自己的证书。保留 [LICENSE](LICENSE) 和交付信息。

SDK正在Beta 迭代公测中，请及时升级SDK版本。[更新与说明](https://github.com/FaceSDKPro/) · [问题反馈](https://github.com/FaceSDKPro/HelloFace/issues)。
