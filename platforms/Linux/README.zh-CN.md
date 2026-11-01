# HiFace Linux 桌面 Demo

[English](README.md) | 简体中文

Linux x86_64、ARM64，CPU 推理，公共 C ABI v1。提供桌面摄像头程序、图片/人脸库命令行及完整 Demo 源码；依赖独立 Linux SDK。
运行基线为 Ubuntu 22.04（glibc 2.35），兼容目标为 Ubuntu 24.04；桌面使用 X11 或 Wayland 中的 XWayland。

**当前模型不支持12周岁以下儿童人脸识别，不能用于金融相关场景。** SDK正在Beta 迭代公测中，请及时升级SDK版本。

## HelloFace 中直接运行

从 HelloFace 仓库进入 `platforms/Linux`，执行 `./self-test.sh`、`./run-demo.sh`。启动脚本根据 `uname -m` 自动选择 `runtimes/x86_64` 或 `runtimes/arm64`。
图形界面请用 `./run-demo.sh` 启动，SDK 加载失败时也能显示可操作的错误页。
首页和错误页显示 Demo、实际 SDK 版本及联系方式；无法读取 SDK 版本时明确标注预期版本。
复制诊断只含版本、错误码、时间与联系方式，不包含应用数据。
SDK 二进制、公共头文件和模型在 `runtimes/{arch}/sdk`；`src` 是集成 Demo 源码。SDK 接入见对应 SDK 的 `README.zh-CN.md`。
编译 Demo 时 CMake 同样自动选择宿主架构，也可传 `-DHIFACE_SDK_DIR`。开发和 CLI 命令中的 `bin`、`sdk` 路径应加 `runtimes/{arch}/` 前缀。

## 单架构压缩包直接运行

在 Linux 执行 `uname -m`：`x86_64` 选择 x86_64 包，`aarch64` 选择 arm64 包。
复制完整 `.tar.gz` 到 Linux 本地目录，在 Linux 内解压。包可使用 `runtimes/{arch}/` 或直接 `bin/`、`sdk/` 布局；请保留完整目录和启动脚本的相对位置。

```bash
# 解压下载的完整压缩包后，进入其根目录：
runtime=runtimes/x86_64  # ARM64 改为 runtimes/arm64
[ -d "$runtime" ] || runtime=.
sha256sum -c FILES.sha256
./self-test.sh
./run-demo.sh
# 指定摄像头：
./run-demo.sh /dev/video2

# 无需 X11 的摄像头链路检查：
"$runtime/bin/hiface_desktop" --camera-test /dev/video2

# 预览和人脸分析前将摄像头像素逆时针旋转 90°：
HIFACE_CAMERA_ROTATE=left ./run-demo.sh /dev/video2

# Xorg 保持原生横屏，HiFace 内部以逆时针 90° 绘制竖屏全屏界面：
HIFACE_DISPLAY_ROTATE=left ./run-demo.sh /dev/video2

# 自助机/服务模式：SDK 初始化后进入 Face search 并启动连续搜索：
HIFACE_START_SEARCH=1 ./run-demo.sh /dev/video2
```

无需安装编译器、Qt 或 OpenCV。Ubuntu 缺少桌面运行库时：

```bash
sudo apt update
sudo apt install -y libx11-6 xwayland v4l-utils
v4l2-ctl --list-devices
```

从 Linux 图形桌面终端启动；纯 SSH/无桌面环境可运行自检和图片/数据库命令。
摄像头需支持 V4L2 单平面或多平面 MMAP，以及 YUYV、MJPEG、NV12 或 NV21（单缓冲区平面打包）。
默认请求 640×360、25 FPS，实际参数由驱动协商；Rockchip ISP 的 NV12 多平面采集接口已支持。
设置 `HIFACE_CAMERA_ROTATE=left`（也可写 `ccw` 或 `90ccw`），可在显示和人脸分析前将横屏帧转为 360×640；反方向使用 `right`（也可写 `cw` 或 `90cw`）。
在 **SDK参数设置（SDK Settings）** 中选择顺时针 **UI 旋转**角度（0°、90°、180°、270°），选择后自动保存。完整界面、摄像头预览、人脸框和关键点同步旋转，鼠标/触摸坐标使用同一变换。角度保存到 `settings.conf`，重启后恢复。未保存角度时沿用 `HIFACE_DISPLAY_ROTATE` 的已有启动别名，也支持数字 `0`、`90`、`180`、`270`。运行时旋转需安装 `libXrender.so.1`。

`HIFACE_DISPLAY_ROTATE=left` 仅在 HiFace 窗口内部旋转完整界面并反向映射鼠标/触摸坐标；请保持 Xorg 输出为 `normal`，避免 DRM 全屏旋转引起 `flip timeout`。

Rockchip 设备可用独立的全分辨率 ISP 通道显示高清预览，同时保持识别流为 640×360：

```sh
HIFACE_PREVIEW_DEVICE=/dev/video32 \
HIFACE_PREVIEW_WIDTH=1280 HIFACE_PREVIEW_HEIGHT=720 \
HIFACE_PREVIEW_OUTPUT_WIDTH=576 HIFACE_PREVIEW_OUTPUT_HEIGHT=1024 \
./run-demo.sh /dev/video31
```

可选预览通道通过设备上的 `librga.so.2` 一次完成 NV12/NV21 缩放、颜色转换和摄像头旋转。RGA 直接输出 X11 原生 BGRX 排列，本地 X Server 支持时使用 MIT-SHM 上传，避免整帧 CPU 通道交换和套接字复制；两项优化均保留通用回退路径。第二路 ISP 或 RGA 不可用时，Demo 会自动回退到识别流，不影响启动。

T92P 的 J3 白光与 J19 红外灯分开控制，J4 三色灯用于状态提示。RGB 模式采用白光自动补光，红外保持关闭。板级启动脚本以 root 运行 `boards/t92p-lighting.sh prepare`，再为 Demo 设置 `HIFACE_LIGHT_PWM_PATH=/sys/class/pwm/pwmchip0/pwm0` 与 `HIFACE_LIGHT_SENSOR_DEVICE=/dev/v4l-subdev5`（需按 `cv2002_720` 传感器名称确认）。应用只获得白光 duty/enable 的权限，不以 root 运行；服务停止时运行同一脚本的 `off`。

自动策略沿用厂商 1 ms PWM 周期，以 CV2002 的曝光比例、0.3 dB/单位模拟增益及中央画面亮度估计暗光需求，并非独立照度计的 lux。持续暗光 2 秒后以每 0.5 秒 5% 渐亮至 25%；亮暗阈值分离，至少点亮 15 秒且持续明亮 6 秒才渐灭。相机停止、帧中断或传感器读数失败时关闭白光，避免补光反馈造成频繁闪烁。未配置环境变量的设备不启用板级灯控。
权限不足时检查 `/dev/video*` 的访问权限；加入 `video` 组后需完整注销再登录，不必用 root 启动 Demo。

## 桌面功能

Live Detection 仅绘制检测五点以及嘴唇、双眼、脸部外轮廓、鼻部连线；连线索引及双眼 1.4 倍显示比例与 Android 相同。不绘制眉毛、完整 468 点点云、人脸框、置信度或头姿标签。人脸搜索和图片搜索保留各自的识别框与结果标签。

首页对齐 Android，仅展示八个纵向功能入口：检测、人脸采集、动态验证、活体、人脸搜索、图片比对、图片搜索和 SDK参数设置。录入、人脸库从人脸搜索页进入，图片搜索页也可进入人脸库。每个独立页面顶部都有 Back 返回按钮；子页返回打开它的功能页，主功能返回首页。滚轮或 Page Up/Down 可滚动首页。进入相机功能页时自动连接所选设备并启动本页流程；离页会关闭相机并取消本页流程。启动时不自动打开相机。

| 入口 | 使用方法 |
|---|---|
| F1 Face search | 连续 1:N/M:N；其余相机功能由首页各自进入 |
| F2 Photo comparison | 两张本地图片比对与输入预览；图片检测、设置参考和图片录入分别在对应功能页的 Menu 中选图 |
| F3 Face Capture | 人脸采集与参考特征；录入和人脸库管理从人脸搜索页进入 |
| F5 Image search | 一图最多 10 脸，逐脸匹配 ID/分数、框与对齐缩略图，分页选择高亮 |
| F4 SDK Settings | 摄像头尺寸/FPS/格式、镜像与 UI 旋转；各功能参数在对应功能页的 Settings 中设置 |

相机快捷键选择独立页面：`D` 检测、`C` 抓拍、`L` 活体、`V` 验证、`E` 录入、`S` 搜索、`P` 暂停/恢复搜索、
鼠标操作：左键点击功能卡片、Back、按钮、开关和输入框；滚轮可滚动首页或选择图片搜索结果。点击不依赖先收到鼠标移动事件，窗口缩放和 UI 旋转后也按当前布局定位。左键点击同时取得键盘焦点，无窗口管理器的嵌入式会话也可点击输入框后键入。

嵌入式小屏默认提供内置软键盘：点击 Face ID、分组或其他输入框，用鼠标/触屏输入字母、数字及常用符号。`A/a` 切换大小写，`Symbols` 切换符号，`Delete` 删除，`Clear` 清空，`Space` 输入空格。点击键盘的 `Done` 只收起键盘，再点击页面的保存/确认按钮提交；Esc 先收起键盘，不丢弃采集草稿。旋转后键盘与点击坐标同步。`HIFACE_SOFT_KEYBOARD=1` 可在普通桌面强制开启，`0` 关闭，默认 `auto`。不内置中文拼音输入，中文仍需系统字体和 XIM 输入法；实体键盘可继续使用。

Demo 内置黑白箭头，不依赖系统的光标主题或字体。`HIFACE_MOUSE_CURSOR=auto`（默认）在小屏/嵌入式显示器上使用软件光标，在常规桌面上使用 X11 光标。软件光标在 Demo 窗口内绘制，即使 X Server 使用 `-nocursor` 或硬件光标不可用仍能显示；四个 UI 旋转角度下热点均与点击坐标对齐。鼠标移出窗口后由桌面接管。

箭头采用左右镜像外观并随 UI 方向旋转，尖端保持为点击热点；软件光标和原生 X11 光标共用同一旋转像素与热点定义。此设置不交换左右键，也不重复旋转或镜像鼠标坐标。

```sh
# 强制使用适合嵌入式设备的软件光标；system 则强制使用原生 X11 光标。
HIFACE_MOUSE_CURSOR=software ./run-demo.sh /dev/video0
```

`X` 取消流程；Back 返回上一级并停止本页流程。`Esc` 优先关闭弹窗、输入焦点或相机菜单，再返回上一级。相机 Menu 仅保留设备和当前功能选项。`M` 镜像、`N` 切换/重连摄像头、`Q` 退出。输入框获得焦点后按键用于编辑，
`Ctrl+V` 粘贴、`Ctrl+A` 全选、`Ctrl+C` 复制、`Tab` / `Shift+Tab` 切换控件；`Esc` 先退出编辑。

先在 Face Capture 完成引导采集，检查预览并填写 Face ID，点击 Done 保存。该参考列表独立于搜索人脸库，不自动保存人脸照片。打开验证先选择采集的 Face ID，再自动开始验证；没有参考时提示先采集。
检测、活体、验证和搜索的 Settings 只显示并保存本功能参数。验证页通过 Choose Face ID 重新选择采集的参考；图片搜索进入人脸库后返回会恢复原照片和结果。搜索中点击 Back 先取消请求；导入中返回会先确认停止并保留已提交批次。

参考保存与选择保留原始 HF1，不重新量化。搜索的相机/单图录入先采集并预览，填写 ID/group/tag 后确认保存；重复 ID 需再次确认 Replace，也可保留草稿修改。重拍、取消或离页不会入库，也不会替换验证参考。Face Capture 保存已有 ID 时更新该参考，行为与 Android 一致。库中批量导入有覆盖确认。
抓拍、活体、验证和搜索共用一个相机流程槽。切页取消当前相机流程；暂停搜索释放范围缓存，恢复时重新预热。

活体每次默认随机选择三个不同动作，验证每次默认随机选择两个不同动作，重试重新选择。两页使用独立配置；可在 Settings 关闭随机选择，用按钮添加 blink / nod / shake / mouth，最多四步，允许重复。Undo / Clear 删除步骤，动作留空运行静默活体。动作提示保持到步骤变化，居中显示在圆框上方，不显示剩余秒数；完成或失败均提供重试与关闭。
Verify 默认启用静默活体，可在验证页的 Settings 关闭；Liveness 始终经过 Core 活体流程。
1:N 搜索默认开启同帧静默活体；M:N 自动关闭活体。搜索命中但活体拒绝时显示拒绝，不显示通过绿框。
所有结论、动作判定和阈值算法来自公共 Core。

Face library 右上角 Menu 集中提供刷新、导入万条特征、本地图片目录导入及 HF1 交换。Import synthetic features (10k) 使用随包的已审查合成演示数据，可能是随机向量或从 AI 合成人脸图片提取的特征，均不是正式录入的真人底库。`generate-demo-features` 生成的随机向量只用于导入、分页和性能测试；图片提取样例可用对应的 AI 图片验证检索链路，两类样例都不能用于衡量真实场景识别准确率。JSON 结构与 Android 兼容。
Import HF1 JSON 校验格式和特征空间，100 条一批提交；Stop import 保留已提交批次。
Import image folder 逐张提取本地目录的 JPEG/PNG/BMP/PPM，文件名去扩展名作为 ID，按 group 筛选框的值录入；
无脸、损坏图片会计为失败并跳过，写库失败终止并保留此前批次。导入会新增或覆盖同 ID。
Export HF1 JSON 使用当前已加载的 group/tag 筛选，保持原始 Q7 payload；已有同名输出文件会拒绝覆盖。
同一数据库导出时应暂停其他进程的写入。SDK 数据库最多 50000 条。

全屏人脸搜索页由 V4L2 采集线程持续发布最新帧，预览以 25 FPS 刷新；模型/数据库工作线程只取最新帧，以 8 FPS 为上限异步识别，实际速率随单帧推理耗时变化。
识别阻塞不会阻塞采集和预览；捕获队列丢弃旧帧，界面分别显示 Preview FPS、Recognition FPS 和最近一次推理耗时。
搜索连续 10 秒成功检测均无人脸后，将预览转换与绘制降至 10 FPS，分析调度仍保持最高 5 FPS，并尝试在不重开相机的情况下请求低采集帧率；驱动不支持时仍对处理节流。检出任何人脸（包括未匹配或被拒绝的人脸）即恢复活跃速率。存在唯一可写 sysfs 背光设备时，活跃搜索使用 90% 亮度，待机使用 10%；暂停、退出搜索、错误及正常关闭时恢复原亮度。无权限或存在多个背光设备时不修改亮度。其他相机流程保持原有速率，实际整机节电取决于相机、屏幕及平台。
搜索在本帧处理完成后一次性发布人脸框、身份和活体状态，避免每帧“搜索中”与最终结果交替闪烁；等待、拒绝/未知、确认匹配分别绘制灰、红、绿框，活体拒绝优先于身份相似度。分析结果超过 1.2 秒或预览断流超过 0.5 秒即清除旧框，切换/重连相机也使旧框失效。
暂停停止识别调度但保持实时预览，恢复时处理最新帧；SDK 跳过的帧不计入 Recognition FPS。预览快照共享不可变叠加层，X11 合并相邻同色同裁剪的人脸框边、轮廓线与关键点，保持原绘制顺序。
无人值守的自助机或服务部署可设置 `HIFACE_START_SEARCH=1`，使 Demo 在 SDK 初始化完成后进入 Face search 并立即启动连续搜索；默认仍停在首页且不打开摄像头。

## 图片人脸搜索

打开 **F5 Image search** → Browse 选择照片，选择后自动开始；也可输入路径后按 Enter 或点击 Search；搜索中按 Esc 可取消。
照片按比例显示，紧凑人脸编号与结果卡对应，ID/分数在卡片和选中详情中显示，点击原图中的人脸或结果卡高亮对应人脸；方向键切换，Home / End 跳到首尾，Page Up / Page Down 翻页，右侧滚轮逐脸切换。
选中人脸始终显示在当前页，缩放窗口后保留选择；预览下方展示所选标签及 group/tag，卡片区分匹配、小脸、未匹配和技术错误。
图片中的小脸、提取失败、未匹配分别保留框和提示；无人脸或库为空有单独说明，只有成功匹配才显示 ID 与分数。
结果卡显示 333×333 对齐人脸，小脸使用占位图，不伪造特征或分数。

与 Android 图片搜索保持一致：最多 10 张脸、全库 Top-1、共享默认阈值 0.80；不读取相机搜索的 group/tag、人数或活体开关。
静态图片不做活体检测；相似度表示 SDK 身份相似度，不代表身份概率。
JPEG 自动处理 EXIF 旋转/镜像，JPEG/PNG/BMP 超过 4096 的图片先等比缩小，再裁掉奇数末行/列；不放大小图。
照片文件限制 64 MiB，解码前限制 64 百万像素和 16384 单边；PPM 沿用 4096 单边限制。

正在搜索时仍可换图、再次 Search 或 Cancel；请求序号使旧结果失效，队列只保留最新照片。
当前 SDK 同步调用自然结束后才释放缓冲或退出，界面保持响应。
照片搜索不写人脸库；先去 Face library 导入特征，或使用相机/图片录入建立库。

## 数据与诊断

数据默认在 `${XDG_DATA_HOME:-$HOME/.local/share}/hiface-demo`，可用 `HIFACE_DATA_DIR` 覆盖。
包括搜索库 `hiface-search.db`、采集参考库 `captured-faces.db`、当前选择 `reference.hf1` 和 `settings.conf`。引导采集预览不落盘；旧版本主动保存的 `captures/*.ppm` 不自动删除。
SQLite 与本地 HF1 未加密，HF1 是编码，不是加密；宿主需自行实现业务数据保护和清理。
界面使用英文标签，文件路径、ID 和分组支持 UTF-8；中文显示与输入依赖桌面字体及输入法。

```bash
./diagnose.sh > linux-diagnostic.txt 2>&1
./run-demo.sh /dev/video0 2>&1 | tee desktop-run.log
```

构建信息与验证报告见随包元数据及说明；`FILES.sha256` 校验包内文件，压缩包同目录提供 SHA-256 清单。诊断脚本不导出人脸库或参考特征。

## 独立编译 Demo

以下命令从已解压 Demo 根目录执行。CMake 自动选择宿主架构的 SDK；模型已内置，运行时传 `--builtin`，不要指定外部模型目录。

Demo 与 SDK 的依赖方向：`Demo → HiFace::HiFace（公开 C ABI）→ Core`。
`src/desktop/workflow.cpp` 中的 `FaceWorkflow` 是 Demo 的流程编排类；X11 渲染、V4L2、图片解码、HF1 JSON 和本地设置均属于 Demo。
SDK 不依赖 Demo。对应 `runtimes/{arch}/sdk/README.zh-CN.md`（或直接布局的 `sdk/README.zh-CN.md`）提供公开 SDK 接入说明。

从 Demo 包自身编译，无需仓库或 Core 源码：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
./build/hiface_demo self-test --builtin
./build/hiface_desktop --builtin "$HOME/.local/share/hiface-demo/hiface-search.db" /dev/video0
```

可传 `-DHIFACE_SDK_DIR=/absolute/path/to/standalone-sdk` 使用单独 SDK 包；
`-DHIFACE_BUILD_LINUX_DESKTOP=OFF` 只编译 CLI；`-DBUILD_TESTING=ON` 编译 Demo 回归测试后运行 `ctest --test-dir build`。
源码支持 GNU C++17，CMake 3.22.1+；SDK 已自带算法运行库，不需要再构建 NCNN。

界面采用 Android 式首页和独立功能页，各页顶部提供 Back；检测、活体、验证、搜索的参数从所属页面进入，SDK参数设置单独进入。
人脸录入先用相机或图片采集，再在预览确认框填写非空 ID / group / tag；点击确认才写库。重复 ID 保留草稿；重拍、取消、换页不会写库，也不会替换 1:1 基准。
人脸库采用全宽卡片列表，每批加载 100 条，支持滚轮与按住拖动，接近末尾时自动预取。顶部按完整 Face ID 查询，清空查询恢复全库；每条卡片显示分组/标签并提供编辑弹窗与删除确认。右上角 Menu 提供刷新、万条特征、本地图片目录导入及 HF1 交换；目录导入先选目录再确认，导入显示进度并可停止，保留已提交批次。HF1 交换页的 Back 返回人脸库，导出为全库 JSON。
输入支持 UTF-8、鼠标定位光标、全选/复制/粘贴；过长粘贴会保留原输入。
Enter 可提交图片路径、相机设备、人脸库筛选和设置；F1..F5 在编辑框中也能切页，弹窗内保持键盘焦点。
点击当前页面保留草稿；设置错误会定位到对应分组和输入框，弹窗取消后恢复原按钮焦点。
文件选择器的目录错误与录入校验在弹窗内显示，便于直接修正。按钮支持悬停、禁用、处理中反馈；未匹配/空库使用提示色，技术错误单独显示。
窗口初始尺寸适配当前屏幕，最小 960×700，适用于 VMware 常见的 1024×768 桌面。

## CLI

从 Demo 根目录执行，按实际布局选择运行目录：

```bash
runtime=runtimes/x86_64  # ARM64 改为 runtimes/arm64
[ -d "$runtime" ] || runtime=.
"$runtime/bin/hiface_demo" --help
"$runtime/bin/hiface_demo" detect --builtin face.jpg
"$runtime/bin/hiface_demo" compare --builtin first.jpg second.png
"$runtime/bin/hiface_demo" capture --builtin face.jpg aligned.ppm
"$runtime/bin/hiface_demo" enroll --builtin hiface-search.db person-001 face.jpg
"$runtime/bin/hiface_demo" search --builtin hiface-search.db query.jpg
# 多人图片搜索，输出每张脸的框、状态、匹配 ID/分数的 JSON：
"$runtime/bin/hiface_demo" search-faces --builtin hiface-search.db group-photo.jpg
"$runtime/bin/hiface_demo" gallery-import hiface-search.db assets/features_10000.json
"$runtime/bin/hiface_demo" gallery-export hiface-search.db export.json
```

CLI enroll 为 upsert；桌面单条录入重复 ID 拒绝。CLI gallery-import 是覆盖导入，逐批提交；
失败打印此前已保存数。导出文件已存在时拒绝覆盖。

## 验证范围

摄像头以有界帧池复用图像内存，只复用已无 UI/SDK 引用的帧；积压时丢弃旧帧，不增长队列。
ISP 预览缓冲优先通过 V4L2 DMA-BUF 交给 RGA；不支持导出或转换失败时回退到映射内存。
可用 `HIFACE_PREVIEW_DMA=0` 强制兼容路径进行对照，不影响独立的 I420 识别输入。
V4L2 使用最长 10ms 的就绪等待，SDK 工作线程按事件唤醒；UI 在新帧、新状态或输入事件到来时重绘，
静态页面不再持续进行整屏合成。原有 X11/Xrender 竖屏后端保留，未切换 DRM/KMS。
`HIFACE_CPU_THREADS=2` 可为显示和采集保留 CPU 余量，`4` 优先单次推理延迟；不设置时沿用 SDK 自动策略。
线程数为进程级设置，修改后重启 Demo，不改变模型、特征、搜索阈值或活体检查。
`HIFACE_PERF_LOG=1` 每 5 秒记录预览/识别帧率、末次分析和平均绘制耗时，仅在排查时启用。

UI 自动测试复用实际绘制指令，检查 960×700、1024×768、1440×900 布局、控件碰撞、
禁用/忙碌状态、UTF-8 编辑、录入弹窗、设置草稿、删除确认与导入进度。
Linux CI 配置了 Xvfb 首页、八个主功能及子页窗口渲染和 Ubuntu 22.04 → 24.04 独立消费；未执行的 CI 不算已通过。
**交叉编译与宿主机逻辑测试不代表 Linux 窗口、USB 摄像头、真人识别或活体攻击已经验收，需在目标设备实测。**

SDK参数设置的默认/高清、镜像和 UI 旋转选择自动保存；精确尺寸、FPS 和格式收进高级设置，手工输入用 Apply typed values 保存。应用/重置不会改动动作与搜索参数。
Photo comparison 点击任一照片区域选图后即提取并缓存特征，两侧就绪后比对只显示相似度百分比；无有效结果时不占用分数区域。静态图片检测、图片基准和图片录入分别在对应功能页的 Menu 选择照片。
选图后关闭实时相机，避免覆盖静态结果。功能切换释放旧图片/预览/搜索结果；窄竖屏首页可滚轮或 Page Up/Down 滚动。
