# Android 端

把控制台搬到 Android：**SDL2 + OpenGL ES 3 + Dear ImGui**（原生界面）+ **WebView 网页界面**（Vue3），
`core/` 与 `ui/` 与桌面**共用同一份源码**，差异只在窗口、渲染后端、输入与生命周期。

启动流程：原生侧起本机网页服务（127.0.0.1:8123）→ 自动打开 `WebUiActivity`（WebView 显示 Vue3 界面）；
**返回键回到 ImGui 界面**，两套界面共存。

```
client/apps/android/
├── setup.sh                  # 一键装配：下载 SDL2/ImGui → 展开 Gradle 工程 → 注入配置
├── _run_gradle.bat           # Windows 前台构建（日志 _build.log）
├── build_deps_arm64.ps1      # 现场编译 arm64 依赖（libdatachannel/OpenSSL）
├── native/
│   ├── CMakeLists.txt        # 原生构建：go2_core + go2_ui(含 web_bridge) + main_android.cpp + SDL2 + ImGui
│   └── main_android.cpp      # SDL2 入口：GLES3 + ImGui 主循环 + 触屏双摇杆 + 网页界面启动
├── build.gradle / settings.gradle / gradlew     # Gradle 顶层（由 SDL 模板展开）
└── app/                      # Gradle 模块
    ├── build.gradle          # applicationId=com.go2.remote, minSdk=26, 走 CMake
    ├── jni/CMakeLists.txt    # → include(native/CMakeLists.txt)
    └── src/main/
        ├── AndroidManifest.xml            # 网络/组播/WiFi 权限；四方向；usesCleartextTraffic（WebView 访问 127.0.0.1）
        ├── java/org/libsdl/app/*.java     # SDL 模板 Java 类
        │   └── MainActivity.java          # + MulticastLock（组播发现必需）
        ├── java/com/go2/remote/WebUiActivity.java   # 网页界面（WebView 外壳）
        └── assets/
            ├── fonts/                     # 中文字体（否则中文显示为方块）+ 图标字体
            └── web/                       # ★ 网页前端副本（源在 client/assets/web，改前端要两处同步）
```

## 1. 前置条件

| 项 | 要求 |
|---|---|
| Android Studio | Ladybug (2024.2) 或更新（自带 SDK/Gradle/NDK 管理） |
| SDK | compileSdk 34、**NDK 26.x**、CMake ≥ 3.22 |
| 设备 | Android 8.0（API 26）以上，与机器狗同一 WiFi |
| 其他 | 手机开 USB 调试；或直接用 Android Studio 的设备模拟器（需 `x86_64` ABI） |

## 2. 装配工程（已执行过，可重复跑）

```bash
cd client/apps/android
./setup.sh            # 下载 SDL2 + ImGui，展开 Gradle 工程并注入配置
./setup.sh --check    # 只检查环境，不做修改
```

`setup.sh` 做的事：① 克隆 SDL2（`client/third_party/SDL`）与 ImGui；② 把 SDL 的 `android-project`
模板展开到本目录；③ 写入 `app/jni/CMakeLists.txt` 指向 `native/CMakeLists.txt`；
④ 注入权限、横屏、包名、minSdk、MulticastLock 的 `MainActivity`。

## 3. 原生依赖（**首次构建的主要卡点**）

`core/` 需要 Android 版 **OpenSSL / libdatachannel / nlohmann-json**，二选一：

**A. vcpkg 交叉编译（推荐）**

```bash
git clone https://github.com/microsoft/vcpkg && ./vcpkg/bootstrap-vcpkg.sh
export ANDROID_NDK_HOME=$ANDROID_SDK_ROOT/ndk/26.1.10909125
./vcpkg/vcpkg install openssl libdatachannel nlohmann-json --triplet arm64-android
```

然后在 `app/build.gradle` 的 `externalNativeBuild.cmake.arguments` 里追加：

```groovy
arguments "-DANDROID_STL=c++_shared",
          "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake",
          "-DVCPKG_TARGET_TRIPLET=arm64-android",
          "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${System.getenv('ANDROID_NDK_HOME')}/build/cmake/android.toolchain.cmake"
```

**B. 已有预编译前缀目录**

```groovy
arguments "-DANDROID_STL=c++_shared", "-DGO2_ANDROID_DEPS=/path/to/prefix"   // 含 include/ 与 lib/
```

> 另需 `cpp-httplib`（信令 HTTP 客户端，单头文件）：从桌面构建缓存复制
> `client/build/_deps/cpphttplib-src/httplib.h` 到 `client/third_party/httplib.h` 即可
> （`native/CMakeLists.txt` 已把 `third_party/` 放进 include 路径）。

## 4. 中文字体（强烈建议）

Android 没有中文系统字体，界面会显示方块。把一个 OFL 授权的字体放进
`app/src/main/assets/fonts/`（文件名用 `NotoSansSC-Regular.otf` / `NotoSansSC-Regular.ttf` /
`SourceHanSansSC-Regular.otf` / `wqy-microhei.ttc` 之一）：

```bash
curl -L -o app/src/main/assets/fonts/NotoSansSC-Regular.otf \
  https://github.com/notofonts/noto-cjk/raw/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf
```

## 5. 构建与安装

```bash
cd client/apps/android
./gradlew assembleDebug                 # 产物 app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

或用 Android Studio：`File → Open →` 选择 `client/apps/android`（顶层目录，不是 `app/`）→ Run ▶。

## 6. Android 特有的行为（与桌面不同之处）

| 项 | 说明 |
|---|---|
| 双界面 | 启动后自动打开 **WebView 网页界面**（Vue3）；返回键回到 ImGui 界面，两套共存随时可切 |
| 触摸双摇杆 | ImGui 只有一个指针 → `main_android.cpp::TouchSticks` 自己接管 `SDL_FINGERDOWN/MOTION/UP`，**双杆可同时拖**；急停/阻尼等安全区登记为"摇杆抓取互斥区"（`ui.safetyRects` 多矩形） |
| 切后台 | **自动停车并断开**（`SDL_APP_WILLENTERBACKGROUND`），回前台自动重连 —— WebView 盖在上面**不算**切后台（连接保持，网页界面才能控狗） |
| 组播发现 | 由 `MainActivity` 持有 `WifiManager.MulticastLock`，否则 SN 多播（231.1.1.1:10131）收不到回包 |
| 返回键 | ImGui 界面：急停未锁定时提示"请先急停再退出"，已锁定才允许退出；WebView：直接回到 ImGui 界面 |
| 图标字体 | `assets/fonts/Phosphor.ttf` 启动时导出到应用目录（`extractIconFont()` + `GO2_ICON_FONT`）—— ImGui 的字体合并只认磁盘路径 |
| 网页资源 | `assets/web/**` 启动时导出到应用目录（`extractWebAssets()`，按文件大小增量覆盖）；**新增前端文件要同步加进 `kWebFiles[]` 清单** |
| 钥匙目录 | 应用专属外部目录 `/sdcard/Android/data/com.go2.remote/files`（chdir 过去 + `GO2_KEYS_FILE`），adb push 无需任何权限 |

## 7. 首次构建常见报错

| 报错 | 处理 |
|---|---|
| `找不到 SDL2 源码` | 先跑 `./setup.sh`（会下载到 `client/third_party/SDL`） |
| `Could not find nlohmann_json / OpenSSL` | 见第 3 节，依赖要用 **arm64-android** 版本 |
| `undefined reference to rtc::...` | libdatachannel 没链上：检查 `LibDataChannel_FOUND` 或 `GO2_ANDROID_DEPS` |
| 中文显示方块 | 放字体（第 4 节） |
| `multicast` 无回包 | 检查 `MainActivity.acquireMulticastLock()` 是否被调用（logcat 关键字 `go2`） |
| NDK 版本不匹配 | `app/build.gradle` 里加 `ndkVersion "26.1.10909125"` 与本地一致 |

> 首次构建必然要按实际报错迭代几轮（NDK 版本 / 依赖路径 / ABI 差异），把日志发出来即可定位。
