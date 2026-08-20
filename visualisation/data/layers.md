# Translex 依赖分层清单

> 日期：2026-08-20（v3 深度增强：Qt 源码内嵌第三方库 + 系统组件细化 + 运行时插件补齐）· 数据源：`dependencies.json`（递归展开后）
> 共 **178 个节点**、**384 条边**，覆盖 9 个层（L0–L8）；每个节点带「实现主体」(owner) 属性（11 个主体）
> v3 增补：网络大范围调研（Qt 官方 licenses-used-in-qt 清单 + GitHub qt 仓库源码）+ 本地 Qt 6.11.1 `*Dependencies.cmake` 矩阵 + 部署目录真实 DLL 清单

## L0 应用产物

| 节点 | 说明 |
| --- | --- |
| `translex.exe` | Translex QML 主程序（v1.1.0-qml，Qt 6.11.1 + FluentUI 1.7.7） |
| `example_translation_plugin.dll` | 示例插件（回显后端 `translation.echo` + 侧边栏面板），部署到 `<exe>/plugins/` |

## L1 源码

**入口 / 基础设施**

| 节点 | 说明 |
| --- | --- |
| `src/main_qml.cpp` | QML 版入口：注册全部 service 到 `ServiceRegistry`，`setContextProperty` 暴露给 QML |
| `src/driver_service.*` | UI 驱动桥（测试钩子，`TRANSLEX_UI_DRIVER=1` 启用） |
| `qml/`（界面层） | `Main.qml`（Outlook 式主窗口）+ `TranslateHomePage` + `TranslateSettingsPage` + `panels/*`，10+ 文件，FluentUI |
| `config/*.json` | 配置 schema：`ui.json`/`translation.json`/`translation.ollama.json`/`translation.network_model.json`/`textToSpeech.json` |

**服务层（src/services/，19 个）**

| 服务 | 职责 | 内部引用 |
| --- | --- | --- |
| `appguard` | 稳定性：日志 + 崩溃诊断 | — |
| `documentmodel` | 懒加载文档行模型（虚拟化核心），应用级单例 | → commentservice |
| `securestorage` | 敏感信息加密（机器指纹 + 盐 + 校验） | — |
| `serviceregistry` | 注册/查询/健康度聚合/插件扫描 | → translex_sdk 接口 |
| `translationbackend` | 三翻译后端（Ollama/云/OpenAI 风格 API） | → serviceregistry |
| `translationcache` | 翻译缓存（内存+磁盘，SHA-256） | — |
| `translationservice` | 翻译编排（上下文/分块/降级/质量门） | → configservice, serviceregistry, translationbackend, translationcache |
| `termglossary` | 术语表 | — |
| `qualitygate` | 质量自检（回显拦截/长度/token/术语） | — |
| `configservice` | schema 驱动配置，持久化 + 加密 | → securestorage, config/*.json |
| `commentservice` | 批注单一数据源 | — |
| `documentmanager` | 文档打开/保存/最近文件，格式分发 | → commentservice, configservice, documentmodel, docxparser, pdfparser, trxparser |
| `chapterservice` | 章节识别与跳转 | → documentmodel |
| `findservice` | 全文查找/替换 | → documentmodel |
| `trxparser` | .trx 格式读写（显示层往返） | → commentservice, documentmodel |
| `docxparser` | .docx 导入（图文混排） | → commentservice, documentmodel, **quazip** |
| `pdfparser` | .pdf 导入/导出 | → commentservice, documentmodel, **qt_pdf, qt_printsupport, qt_gui** |
| `texttospeechservice` | TTS 朗读（可选降级） | → configservice, **qt_texttospeech** |
| `translationhistoryservice` | 翻译历史（环形缓冲） | — |

**测试**

| 节点 | 说明 |
| --- | --- |
| `tests/`（16 个） | documentmodel / securestorage / performance(perf) / translation / quality / configservice / comment / documentmanager / chapter / find / trx / docx / pdf / texttospeech / history / registry |
| `translex_services` | 测试静态库（全部 service 源码抽一次编译，16 测试共享） |

## L2 三方库

| 库 | 版本 | 许可 | 说明 |
| --- | --- | --- | --- |
| `FluentUI` | 1.7.7 | BSD-3-Clause | Fluent 设计语言 QML 组件库；git 子模块 + **本地补丁**（FluWindow appBar z:1 等） |
| `QuaZip` | 1.7.2 | LGPL-2.1 | Qt 的 ZIP 包装（docx 导入）；git 子模块 + 本地补丁 |
| `zlib` | 1.3.2.1 | zlib License | 无损压缩（QuaZip 依赖）；静态编译；git 子模块 + 本地补丁 |
| `Translex_sdk` | 1.0 | MIT | 插件化公共接口（iservice/itranslationbackend/itranslationplugin/serviceregistry.h） |

## L3 Qt 模块（6.11.1）

**直接依赖（CMakeLists find_package）**：Core / Gui / Widgets / Concurrent / Network / Qml / Quick / QuickWidgets / Svg / ShaderTools / PrintSupport / Pdf / TextToSpeech(可选)

**测试额外**：Test

**递归传递依赖**（来自 `*Dependencies.cmake` 的 `MODULE_DEPENDENCIES`）：

```
Qml → QmlIntegration, Network
Quick → Core, Gui, Qml, QmlModels, QmlMeta, Network, OpenGL
QuickWidgets → Quick, Widgets
QuickControls2 → QuickTemplates2
QuickTemplates2 → QmlModels
QuickDialogs2 → QuickControls2Impl, QuickDialogs2Utils, QuickDialogs2QuickImpl, QuickTemplates2
QuickDialogs2QuickImpl → QuickControls2Impl, QuickDialogs2Utils, QuickTemplates2
QuickShapes / QuickEffects / QuickLayouts / Lottie / VirtualKeyboard / Quick3DUtils → Quick 系
QuickVectorImage → Svg
Pdf → Core, Gui, Network（运行期还拉入 Multimedia）
TextToSpeech → Multimedia
Multimedia → Concurrent, Network, Gui
Core5Compat → Core（QuaZip QTextCodec 需要）
Gui → Core；Widgets → Core, Gui；PrintSupport → Core, Gui, Widgets；Svg/ShaderTools/OpenGL → Core, Gui
```

**windeployqt 部署的附加模块**：LabsFolderListModel / Lottie / LottieVectorImageGenerator / Quick3DUtils / VirtualKeyboard / QuickVectorImageGenerator / **QuickControls2 全样式**（Basic/Fusion/Material/Universal/Imagine/FluentWinUI3/WindowsStyle，13 个节点）

**Qt 运行时插件**（部署在 exe 目录，v3 补齐）：

| 节点 | 内容 |
| --- | --- |
| `qt_qwindows` | 平台插件 `platforms/qwindows.dll`（GUI 必需；链接 gdi32/dwrite/d3d9/imm32 等 17 个系统库） |
| `qt_schannel` | TLS 后端 `tls/qschannelbackend.dll`（HTTPS 用 Windows Schannel） |
| `qt_certonly` | TLS 证书校验后端 `tls/qcertonlybackend.dll` |
| `qt_ffmpeg_plugin` | 多媒体后端 `multimedia/ffmpegmediaplugin.dll` |
| `qt_wmf_plugin` | 多媒体后端 `multimedia/windowsmediaplugin.dll`（Media Foundation） |
| `qt_imageformats` | 图片格式 `imageformats/`（gif/icns/ico/jpeg/pdf/svg/tga/tiff/wbmp/webp） |
| `qt_windowsstyle` | Windows 11 现代样式 `styles/qmodernwindowsstyled.dll` |
| `qt_networkinfo` | 网络信息后端 `networkinformation/qnetworklistmanagerd.dll` |
| `qt_tts_sapi` / `qt_tts_winrt` / `qt_tts_mock` | TTS 后端 `texttospeech/`（SAPI5 / WinRT / Mock） |
| `qt_svgicon` | SVG 图标引擎 `iconengines/qsvgicond.dll` |
| `qt_uiotouch` | UIO 触摸输入 `generic/qtuiotouchplugind.dll` |
| `qt_vkeyboard_plugin` | 虚拟键盘输入法 `platforminputcontexts/qtvirtualkeyboardplugind.dll` |
| `qt_lottie_vectorimage` | Lottie 矢量图格式 `vectorimageformats/qlottievectorimaged.dll` |
| `qt_qmltooling` | QML 调试工具集 `qmltooling/`（qmldbg_server/tcp/debugger/inspector/profiler 等 11+） |

## L4 内嵌子库（FluentUI + Qt 源码内嵌第三方库）

**FluentUI 内嵌**

| 子库 | 说明 |
| --- | --- |
| `qhotkey` | 全局快捷键（Win 用 `RegisterHotKey`，user32） |
| `qrcode`（qrencode） | 二维码生成（内嵌 C 库） |
| `qmlcustomplot`（qcustomplot） | 图表绘制（内嵌） |
| `fluentui_assets` | FluentIcons.ttf 图标字体 + fluentui_en_US/zh_CN.qm 翻译资源（内嵌 qrc） |

**Qt 源码内嵌第三方库**（v3 深度层，来自 Qt 6.11.0 `licenses-used-in-qt.html`）

| 内嵌库 | 归属模块 | 版本 / 许可 |
| --- | --- | --- |
| PCRE2 | QtCore（正则 QRegularExpression） | 10.47 / BSD-2+3 |
| zlib（Qt 自备） | QtCore（数据压缩） | 1.3.2 / zlib |
| TinyCBOR | QtCore（CBOR） | 7.0 / MIT |
| double-conversion | QtCore（浮点转换） | 3.4.0 / BSD-3 |
| 哈希算法集（BLAKE2/MD4/MD5/SHA-1/SHA-3/SipHash/SHA-384/512） | QtCore（QCryptographicHash） | CC0/PD/BSD |
| Easing Equations | QtCore（QEasingCurve） | BSD-3 |
| Unicode CLDR + UCD | QtCore（QLocale） | v48.1 / Unicode |
| FreeType | QtGui（字体渲染） | 2.14.1 / FTL |
| HarfBuzz-NG | QtGui（文字整形） | 12.3.2 / MIT |
| LibPNG | QtGui（PNG） | 1.6.55 / Libpng |
| LibJPEG-turbo | QtGui（JPEG） | 3.1.3 / IJG |
| MD4C | QtGui（Markdown） | 0.5.2 / MIT |
| Pixman | QtGui（光栅化） | 0.17.12 / MIT |
| D3D12 Memory Allocator / Vulkan Memory Allocator | QtGui（RHI） | MIT |
| libtiff / libwebp | Qt 图片格式插件 | 4.7.1 / 1.6.0 |
| DR Libs / Signalsmith Stretch / Two-Level Segregated Fit | QtMultimedia | 0.14.4 / 1.0.0 / v3.1 |
| libpsl + Public Suffix List | QtNetwork | BSD-3 / MPL-2 |
| PDFium（Chromium 引擎） | QtPdf | BSD-3 |
| JavaScriptCore Macro Assembler | QtQml | BSD-2 |
| Yoga | QtQuick（Flexbox 布局） | 2.0.1 / MIT |
| SPIRV-Cross / glslang | QtShaderTools | Apache-2 / 16.1.0 |
| OpenWnn / PinyinIME / tcime | QtVirtualKeyboard（日/拼/注音） | Apache-2 |
| Catch2 | QtTest | 2.13.10 / BSL-1.0 |

## L5 编译工具链

| 工具 | 说明 |
| --- | --- |
| CMake 3.21+ | 构建系统（`qt_add_executable` / `qt_add_qml_module` / CPack） |
| MSVC（VS 2026 x64） | 主编译器（`build-vs2026-x64`，Visual Studio 18 2026 生成器） |
| clang-cl + Ninja | 交叉编译验证（`build-clang`） |
| MinGW g++ + Ninja | 交叉编译验证（`build-mingw`） |
| Ninja | clang-cl/MinGW 生成器 |
| windeployqt | post-build 部署 Qt 运行时 + QML 模块 |
| moc / rcc / uic / lupdate / lrelease | Qt 构建工具（AUTOMOC/AUTORCC/AUTOUIC；FluentUI i18n） |
| vswhere | 探测 VS 路径（消除 windeployqt 警告） |

## L6 测试 / L7 打包

| 工具 | 说明 |
| --- | --- |
| CTest | `ctest --test-dir build-vs2026-x64 -C Debug` |
| Qt Test | 16 个测试目标的框架 |
| NSIS（makensis） | Windows 安装包（缺失时回退 ZIP） |
| CPack / ZIP | 打包 |

## L8 运行时系统依赖（Windows，v3 细化拆分 33 节点）

**基础 API / CRT / 运行时**

| 分组 | 内容 |
| --- | --- |
| Windows 系统 DLL | kernel32 / advapi32 / shell32 / ole32 / oleaut32 / comdlg32 / winspool / crypt32 / ws2_32 / bcrypt / secur32（聚合节点） |
| user32 / gdi32 / uxtheme | FluentUI 无边框（HTCAPTION）+ qhotkey 快捷键；GDI 图形；主题引擎 |
| dwmapi | FluentUI 无边框窗口（`DwmExtendFrameIntoClientArea`） |
| MSVC CRT | msvcp140 / vcruntime140 / concrt140 / vccorlib140（Debug 带 d 后缀） |
| FFmpeg | avcodec-61 / avformat-61 / avutil-59 / swresample-5 / swscale-8（QtMultimedia 后端，7.1.3） |

**图形 / DirectX 族（9 个独立节点）**

| 节点 | 说明 |
| --- | --- |
| d3d11 | Direct3D 11（RHI 主后端 + WMF/EVR 呈现 + FFmpeg 硬解） |
| d3d12 + dxcompiler + dxil | Direct3D 12 + HLSL→DXIL 编译器 |
| dxgi + dxguid | 图形基础设施（适配器/交换链） |
| dwrite | DirectWrite 文字排版 |
| d2d1 | Direct2D（qwindows 可选路径） |
| d3d9 | Direct3D 9（qwindows 回退） |
| dcomp | DirectComposition 桌面合成 |
| opengl32 + opengl32sw | OpenGL + 软件渲染回退 |
| D3Dcompiler_47 | HLSL 着色器编译器 |

**媒体 / 语音 / 系统服务**

| 节点 | 说明 |
| --- | --- |
| Media Foundation | Mfplat / Mf / Mfreadwrite（WMF 多媒体后端核心） |
| WASAPI | mmdeviceapi / audioclient（音频会话） |
| EVR | evr.dll 增强视频渲染器 |
| wmcodecdsp | 媒体编解码 DSP |
| SAPI | sapi.dll（TTS SAPI5 后端） |
| Windows Runtime | runtimeobject + Windows.Media.SpeechSynthesis（WinRT TTS 后端） |
| schannel | 安全通道（Schannel TLS 底层） |

**qwindows 平台插件系统库**：wintab32（数位板）/ imm32（IME）/ setupapi / shlwapi / version / winmm / wtsapi32 / shcore（DPI）/ mscms（颜色管理）

## 实现主体（owner）维度

每个节点归属一个实现主体，可视化支持**按主体着色**（顶栏「按实现主体」切换）与**按主体过滤**（过滤面板「实现主体过滤」区）。

| 主体 | 颜色 | 覆盖节点 |
| --- | --- | --- |
| 本项目 (Translex) | `#4FC1FF` | L0 应用产物、L1 全部源码、Translex_sdk、测试 |
| Qt (The Qt Company) | `#744DA9` | L3 全部 Qt 模块/运行时/插件 |
| FluentUI (zhuzichu520) | `#E6008D` | FluentUI + 内嵌子库 + 图标/i18n 资源 |
| QuaZip (S. Tachenov) | `#C586C0` | QuaZip |
| zlib (madler) | `#B5CEA8` | zlib |
| Kitware | `#CE9178` | CMake / CTest / CPack / Ninja |
| Microsoft | `#0078D4` | MSVC / vswhere / Windows 系统 DLL / DirectX / CRT / WMF |
| LLVM | `#9CDCFE` | clang-cl |
| GCC (MinGW) | `#F48771` | MinGW g++ |
| FFmpeg 项目 | `#6CCB5F` | FFmpeg 库（avcodec/avformat/avutil/swresample/swscale） |
| NSIS 项目 | `#DCDCAA` | NSIS (makensis) |

## 边类型统计

| 类型 | 含义 | 示例 |
| --- | --- | --- |
| `source` | 源码引用（#include / import） | docxparser → quazip；translationservice → translationbackend |
| `build` | 编译期链接 | translex → fluentui；quazip → zlib |
| `runtime` | 运行期加载 | fluentui → user32/dwmapi；qt_multimedia → ffmpeg |
| `tool` | 工具链 | cmake → msvc；translex → windeployqt |
| `test` | 测试关系 | tests → ctest |
| `package` | 打包/部署 | translex → nsis；translex → example_plugin |

## 叶子节点核查（2026-08-20，v4）

全图 178 节点 / 511 边，逐一排查后**仅剩 6 个真叶子**（均确认为无依赖终点）：

| 叶子 | 类型 | 为何是「真叶子」 |
| --- | --- | --- |
| `config_schemas` | 数据文件 | config/*.json 配置 schema，无代码依赖 |
| `fluentui_assets` | 资源 | FluentIcons.ttf 字体 + .qm 翻译，纯资源 |
| `qt_emb_vulkanheaders` | 纯头文件 | Vulkan API Registry 头，无编译产物 |
| `qt_emb_openglheaders` | 纯头文件 | OpenGL/GLES2 头，无编译产物 |
| `qt_qmlintegration` | 接口模块 | QtQml 的集成接口，无独立二进制 |
| `win_system` | OS 边界 | kernel32/advapi32/ole32/ws2_32/bcrypt/secur32 等 Windows 基础层聚合（36 个节点依赖它），OS 最底层不再深挖 |

**本轮补齐的依赖（127 条边）**：

- **服务层 → Qt Core**：9 个叶子 service（appguard/translationcache/termglossary/qualitygate/commentservice/trxparser/translationhistoryservice/driver_service）补齐 `→ qt_core`（源码 `#include` 验证）；`translex_sdk → qt_core`、`plugin_src → translex_sdk`
- **FFmpeg → 系统库（导入表实测）**：解析 `avcodec/avformat/avutil/swresample/swscale` 的 PE 导入表 → `ffmpeg → win_system`（kernel32/ole32/secur32/ws2_32/bcrypt）+ `win_user32`（user32）
- **Qt 内嵌库 → CRT**：30 个 `qt_emb_*` 编译产物补 `→ msvc_runtime`（纯头文件 vulkanheaders/openglheaders 除外）；内部依赖补 `libpng→zlib`、`libtiff→zlib/libjpeg`、`pdfium→freetype/libjpeg/libpng/zlib/win_system`
- **qcustomplot 是 Qt 库**：`fluentui_qmlcustomplot → qt_core/qt_gui/qt_widgets`（此前误为无依赖叶子）；`fluentui_qrcode → msvc_runtime`
- **9 个 Qt 运行时插件**补齐 `→ 对应 Qt 模块 + 系统库`（如 `qt_vkeyboard_plugin → qt_virtualkeyboard/qt_qml/qt_quick/win_imm32`）
- **Windows 系统库底层依赖**：17 个 `win_*` 补 `→ win_system`（及 gdi32/user32 相互依赖），`win_schannel_sys → win_system`（crypt32/secur32/ncrypt）
- **工具链**：msvc/ninja/vswhere/ctest/nsis → `msvc_runtime`；qt_tools → `qt_core + msvc_runtime`

**核查工具**：`parse-imports.mjs`（PE 导入表解析，验证二进制真实依赖）、`complete-leaves.mjs`（幂等补边）。
