# Android 端（P3）

把控制台搬到 Android：**SDL2 + OpenGL ES 3 + Dear ImGui**，`core/` 与 `ui/` 与桌面**共用同一份源码**，
差异只在窗口、渲染后端、输入与生命周期。

```
client/apps/android/
├── setup.sh                  # 一键装配：下载 SDL2/ImGui → 展开 Gradle 工程 → 注入配置
├── native/
│   ├── CMakeLists.txt        # 原生构建：go2_core + go2_ui + main_android.cpp + SDL2 + ImGui
│   └── main_android.cpp      # SDL2 入口：GLES3 上下文 + ImGui(SDL2/OpenGL3) + drawUi() 主循环
├── build.gradle / settings.gradle / gradlew     # Gradle 顶层（由 SDL 模板展开）
└── app/                      # Gradle 模块
    ├── build.gradle          # applicationId=com.go2.remote, minSdk=26, 走 CMake
    ├── jni/CMakeLists.txt    # → include(native/CMakeLists.txt)
    └── src/main/
        ├── AndroidManifest.xml          # 网络/组播/WiFi 权限，横屏
        ├── java/org/libsdl/app/*.java   # SDL 模板 Java 类
        │   └── MainActivity.java        # + MulticastLock（组播发现必需）
        └── assets/fonts/                # 放中文字体（否则中文显示为方块）
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
| 触摸操作 | 触摸被 ImGui 的 SDL2 后端映射成鼠标；双摇杆需要**两点同时按** —— 当前版本先用"单摇杆 + 转向按钮"，多点触控在后续版本做（见方案 P3 后续） |
| 切后台 | **自动停车并断开**（`SDL_APP_WILLENTERBACKGROUND`），回前台自动重连 —— iOS/Android 都不允许后台持续遥控 |
| 组播发现 | 由 `MainActivity` 持有 `WifiManager.MulticastLock`，否则 SN 多播（231.1.1.1:10131）收不到回包 |
| 返回键 | 急停未锁定时提示"请先急停再退出"，已锁定才允许退出 |
| 组播锁 | 原生侧通过 `SDL_AndroidGetActivity()` 反射调用 `acquireMulticastLock()` |

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
