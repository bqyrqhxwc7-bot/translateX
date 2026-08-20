#!/usr/bin/env node
// ============================================================================
// augment-deep.mjs — 深度依赖增强（v3）
// 目标：补足「广度 + 深度」的依赖递归——
//   1) Qt 模块传递依赖矩阵补全（QuickControls2 全样式 / 生成器模块）
//   2) Qt 运行时插件补齐（TLS 证书后端 / Windows 样式 / 网络信息 / TTS 后端 /
//      qmltooling 调试插件 / 虚拟键盘输入法 / Lottie 矢量图 …）
//   3) 新增「Qt 源码内嵌第三方库」深度层（L4，Qt 官方 licenses-used-in-qt 清单）
//   4) 系统组件（L8）细化拆分（D3D 族 / Media Foundation / WASAPI / SAPI /
//      WinRT / DirectWrite / 平台插件系统库 …）
// 数据源：本地 Qt 6.11.1 *Dependencies.cmake 矩阵 + Qt 官方第三方清单 +
//         GitHub qt/qtbase、qt/qtmultimedia、qt/qtspeech 源码 + 部署目录真实 DLL。
// 用法：node augment-deep.mjs   （在 visualisation/ 目录执行，原地更新 dependencies.json）
// ============================================================================
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataPath = join(__dirname, 'data', 'dependencies.json');
const data = JSON.parse(readFileSync(dataPath, 'utf8'));

const have = (id) => data.nodes.some(n => n.id === id);
const edgeHas = (s, t, ty) => data.edges.some(e => e.source === s && e.target === t && (ty === undefined || e.type === ty));

function addNode(n) { if (!have(n.id)) data.nodes.push(n); }
function addEdge(s, t, ty) { if (!edgeHas(s, t)) data.edges.push({ source: s, target: t, type: ty }); }
function setDesc(id, desc) { const n = data.nodes.find(n => n.id === id); if (n) n.desc = desc; }
function setVersion(id, v) { const n = data.nodes.find(n => n.id === id); if (n && v) n.version = v; }

/* ========================================================================
 * 1) 层定义更新：L4 扩展为「内嵌子库（FluentUI + Qt 源码内嵌第三方库）」
 *    L8 扩展为「Windows 系统组件 / 图形 / 媒体 / CRT / FFmpeg」
 * ====================================================================== */
data.layers.find(l => l.id === 4).desc =
  '内嵌子库：FluentUI 内嵌（qhotkey/qrencode/qcustomplot）+ Qt 源码内嵌第三方库（PCRE2/FreeType/HarfBuzz/PDFium…）';
data.layers.find(l => l.id === 8).desc =
  'Windows 系统组件（D3D 族 / Media Foundation / WASAPI / SAPI / DirectWrite / 平台插件系统库）+ CRT + FFmpeg';

/* ========================================================================
 * 2) Qt 模块补全（L3）：QuickControls2 全样式 + 矢量图生成器
 *    （部署目录真实存在的 DLL；依赖矩阵来自 *Dependencies.cmake）
 * ====================================================================== */
const qtModules = [
  { id: "qt_quickcontrols2basic", label: "Qt6 QuickControls2 Basic", desc: "QuickControls2 基础样式（Basic Style），windeployqt 全量部署" },
  { id: "qt_quickcontrols2basicstyleimpl", label: "Qt6 QuickControls2 BasicStyleImpl", desc: "Basic 样式实现（StyleImpl，样式基座）" },
  { id: "qt_quickcontrols2fusion", label: "Qt6 QuickControls2 Fusion", desc: "Fusion 样式（跨平台桌面风格）" },
  { id: "qt_quickcontrols2fusionstyleimpl", label: "Qt6 QuickControls2 FusionStyleImpl", desc: "Fusion 样式实现" },
  { id: "qt_quickcontrols2imagine", label: "Qt6 QuickControls2 Imagine", desc: "Imagine 样式（资源驱动皮肤）" },
  { id: "qt_quickcontrols2imaginestyleimpl", label: "Qt6 QuickControls2 ImagineStyleImpl", desc: "Imagine 样式实现" },
  { id: "qt_quickcontrols2material", label: "Qt6 QuickControls2 Material", desc: "Material 样式（Google Material 设计）" },
  { id: "qt_quickcontrols2materialstyleimpl", label: "Qt6 QuickControls2 MaterialStyleImpl", desc: "Material 样式实现" },
  { id: "qt_quickcontrols2universal", label: "Qt6 QuickControls2 Universal", desc: "Universal 样式（Windows 通用设计）" },
  { id: "qt_quickcontrols2universalstyleimpl", label: "Qt6 QuickControls2 UniversalStyleImpl", desc: "Universal 样式实现" },
  { id: "qt_quickcontrols2windowsstyleimpl", label: "Qt6 QuickControls2 WindowsStyleImpl", desc: "Windows 原生样式实现（qmodernwindowsstyled.dll 依赖）" },
  { id: "qt_quickvectorimagegenerator", label: "Qt6 QuickVectorImageGenerator", desc: "QtQuick.VectorImage 模块的矢量图生成器（依赖 QuickVectorImage + Svg）" },
  { id: "qt_lottievectorimagegenerator", label: "Qt6 LottieVectorImageGenerator", desc: "Lottie 矢量图生成器（依赖 Lottie + LottieVectorImageHelpers，vectorimageformats 插件载体）" },
];
qtModules.forEach(m => addNode({ id: m.id, label: m.label, layer: 3, kind: "qt", version: "6.11.1", owner: "qt", desc: m.desc }));

// 样式模块的模块级依赖（*Dependencies.cmake 权威）
const styleEdges = [
  ["qt_quickcontrols2basic", "qt_quickcontrols2basicstyleimpl", "build"],
  ["qt_quickcontrols2basic", "qt_quickcontrols2", "build"],
  ["qt_quickcontrols2basicstyleimpl", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2fusion", "qt_quickcontrols2fusionstyleimpl", "build"],
  ["qt_quickcontrols2fusion", "qt_quickcontrols2", "build"],
  ["qt_quickcontrols2fusion", "qt_quickcontrols2basic", "build"],
  ["qt_quickcontrols2fusionstyleimpl", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2imagine", "qt_quickcontrols2imaginestyleimpl", "build"],
  ["qt_quickcontrols2imagine", "qt_quickcontrols2", "build"],
  ["qt_quickcontrols2imagine", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2imagine", "qt_quickcontrols2basic", "build"],
  ["qt_quickcontrols2imaginestyleimpl", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2material", "qt_quickcontrols2materialstyleimpl", "build"],
  ["qt_quickcontrols2material", "qt_quickcontrols2", "build"],
  ["qt_quickcontrols2material", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2material", "qt_quickcontrols2basic", "build"],
  ["qt_quickcontrols2materialstyleimpl", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2universal", "qt_quickcontrols2universalstyleimpl", "build"],
  ["qt_quickcontrols2universal", "qt_quickcontrols2", "build"],
  ["qt_quickcontrols2universal", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2universal", "qt_quickcontrols2basic", "build"],
  ["qt_quickcontrols2universalstyleimpl", "qt_quickcontrols2impl", "build"],
  ["qt_quickcontrols2windowsstyleimpl", "qt_quickcontrols2impl", "build"],
  ["qt_quickvectorimagegenerator", "qt_quickvectorimage", "build"],
  ["qt_quickvectorimagegenerator", "qt_svg", "build"],
  ["qt_lottievectorimagegenerator", "qt_lottie", "build"],
];
styleEdges.forEach(([s, t, ty]) => addEdge(s, t, ty));

/* ========================================================================
 * 3) Qt 运行时插件补齐（L3，部署目录真实 DLL）
 * ====================================================================== */
const plugins = [
  { id: "qt_certonly", label: "Qt TLS 后端 (CertOnly)", desc: "tls/qcertonlybackend.dll：仅证书校验 TLS 后端（与 Schannel 并存）" },
  { id: "qt_windowsstyle", label: "Qt Windows 样式 (Modern)", desc: "styles/qmodernwindowsstyled.dll：Windows 11 现代原生样式" },
  { id: "qt_networkinfo", label: "Qt 网络信息 (Windows)", desc: "networkinformation/qnetworklistmanagerd.dll：Windows 网络连接状态后端" },
  { id: "qt_tts_sapi", label: "Qt TTS 后端 (SAPI)", desc: "texttospeech/qtexttospeech_sapid.dll：Windows SAPI5 语音合成（ISpVoice COM）" },
  { id: "qt_tts_winrt", label: "Qt TTS 后端 (WinRT)", desc: "texttospeech/qtexttospeech_winrtd.dll：Windows.Media.SpeechSynthesis 语音合成" },
  { id: "qt_tts_mock", label: "Qt TTS 后端 (Mock)", desc: "texttospeech/qtexttospeech_mockd.dll：测试用模拟引擎" },
  { id: "qt_svgicon", label: "Qt 图标引擎 (SVG)", desc: "iconengines/qsvgicond.dll：SVG 图标引擎" },
  { id: "qt_uiotouch", label: "Qt 通用插件 (UIO Touch)", desc: "generic/qtuiotouchplugind.dll：UIO 触摸输入桥接（模拟器/调试）" },
  { id: "qt_vkeyboard_plugin", label: "Qt 虚拟键盘输入法插件", desc: "platforminputcontexts/qtvirtualkeyboardplugind.dll：Qt VirtualKeyboard 输入上下文插件" },
  { id: "qt_lottie_vectorimage", label: "Qt Lottie 矢量图格式", desc: "vectorimageformats/qlottievectorimaged.dll：QML 矢量图格式后端（.qml 导出 SVG 路径）" },
  { id: "qt_qmltooling", label: "Qt QML 调试工具集", desc: "qmltooling/：qmldbg_server / tcp / debugger / inspector / profiler / quickprofiler / messages / local / native / nativedebugger / preview / quick3dprofiler / quickeventreplay（11+ 插件，QML 远程调试）" },
];
plugins.forEach(p => addNode({ id: p.id, label: p.label, layer: 3, kind: "qt", version: "6.11.1", owner: "qt", desc: p.desc }));

const pluginEdges = [
  ["qt_network", "qt_certonly", "runtime"],
  ["qt_quickcontrols2", "qt_windowsstyle", "runtime"],
  ["qt_network", "qt_networkinfo", "runtime"],
  ["qt_texttospeech", "qt_tts_sapi", "runtime"],
  ["qt_texttospeech", "qt_tts_winrt", "runtime"],
  ["qt_texttospeech", "qt_tts_mock", "runtime"],
  ["qt_gui", "qt_svgicon", "runtime"],
  ["qt_gui", "qt_uiotouch", "runtime"],
  ["qt_gui", "qt_vkeyboard_plugin", "runtime"],
  ["qt_virtualkeyboard", "qt_vkeyboard_plugin", "runtime"],
  ["qt_gui", "qt_lottie_vectorimage", "runtime"],
  ["qt_quickvectorimage", "qt_lottie_vectorimage", "runtime"],
  ["qt_qml", "qt_qmltooling", "runtime"],
];
pluginEdges.forEach(([s, t, ty]) => addEdge(s, t, ty));

/* ========================================================================
 * 4) Qt 源码内嵌第三方库（L4 深度层，owner=qt）
 *    来源：Qt 6.11.0 官方 licenses-used-in-qt.html + qtbase 源码 3rdparty/
 * ====================================================================== */
const emb = (id, label, ver, lic, desc) =>
  addNode({ id, label, layer: 4, kind: "embedded", owner: "qt", version: ver, license: lic, desc });

// Qt Core 内嵌
emb("qt_emb_pcre2", "PCRE2（QtCore 内嵌）", "10.47", "BSD-2/BSD-3",
  "正则表达式引擎（QRegularExpression），qtbase/src/3rdparty/pcre2");
emb("qt_emb_zlib", "zlib（QtCore 内嵌）", "1.3.2", "zlib License",
  "数据压缩（QByteArray/qCompress，Qt 自备一份与 QuaZip 分离）");
emb("qt_emb_tinycbor", "TinyCBOR（QtCore 内嵌）", "7.0", "MIT",
  "CBOR 二进制序列化（QCborValue/QCborArray/QCborMap）");
emb("qt_emb_doubleconversion", "double-conversion（QtCore 内嵌）", "3.4.0", "BSD-3",
  "IEEE 双精度浮点高效二进制/十进制转换（QString 数字格式化）");
emb("qt_emb_hash", "哈希算法集（QtCore 内嵌）", "—", "CC0/Public Domain/BSD",
  "BLAKE2 + MD4 + MD5 + SHA-1 + SHA-3(Keccak) + SipHash + SHA-384/512(rfc6234)，QCryptographicHash 后端");
emb("qt_emb_easing", "Easing Equations（QtCore 内嵌）", "—", "BSD-3",
  "Robert Penner 缓动函数（QEasingCurve 动画）");
emb("qt_emb_clrd", "Unicode CLDR + UCD（QtCore 内嵌）", "v48.1 / 36", "Unicode License",
  "Unicode 通用语言数据仓库 + 字符数据库（QLocale/QString 本地化）");

// Qt Gui 内嵌
emb("qt_emb_freetype", "FreeType（QtGui 内嵌）", "2.14.1", "FTL / GPL-2",
  "字体光栅化引擎（QFont 渲染核心）");
emb("qt_emb_harfbuzz", "HarfBuzz-NG（QtGui 内嵌）", "12.3.2", "MIT",
  "复杂文字整形引擎（中东/南亚/东亚文字正确排布）");
emb("qt_emb_libpng", "LibPNG（QtGui 内嵌）", "1.6.55", "Libpng License",
  "PNG 编解码（QImage PNG 格式）");
emb("qt_emb_libjpeg", "LibJPEG-turbo（QtGui 内嵌）", "3.1.3", "IJG / BSD-3",
  "JPEG 编解码（SIMD 加速）");
emb("qt_emb_md4c", "MD4C（QtGui 内嵌）", "0.5.2", "MIT",
  "Markdown 解析（QTextMarkdownImporter/Exporter）");
emb("qt_emb_pixman", "Pixman（QtGui 内嵌）", "0.17.12", "MIT",
  "像素操作与光栅化库（QImage 合成/变换）");
emb("qt_emb_d3d12ma", "D3D12 Memory Allocator（QtGui 内嵌）", "f128d39", "MIT",
  "Direct3D 12 显存分配器（Qt RHI 后端）");
emb("qt_emb_vma", "Vulkan Memory Allocator（QtGui 内嵌）", "3.2.1", "MIT",
  "Vulkan 显存分配器（Qt RHI Vulkan 后端）");
emb("qt_emb_vulkanheaders", "Vulkan API Registry（QtGui 内嵌）", "1.4.308", "Apache-2 / MIT",
  "Vulkan 头文件（WrapVulkanHeaders，RHI 编译所需）");
emb("qt_emb_openglheaders", "OpenGL / GLES2 Headers（QtGui 内嵌）", "rev 27684", "MIT",
  "OpenGL / OpenGL ES 2 头文件（RHI GL 后端编译所需）");
emb("qt_emb_emoji_segmenter", "Emoji Segmenter（QtGui 内嵌）", "0.4.0", "Apache-2",
  "Emoji 序列分段（文本渲染）");

// Qt Image Formats 插件内嵌
emb("qt_emb_libtiff", "libtiff（Qt 图片格式内嵌）", "4.7.1", "Libtiff License",
  "TIFF 编解码（imageformats/qtiff）");
emb("qt_emb_libwebp", "libwebp（Qt 图片格式内嵌）", "1.6.0", "BSD-3",
  "WebP 编解码（imageformats/qwebp）");

// Qt Multimedia 内嵌（FFmpeg 已单列 L8）
emb("qt_emb_drlibs", "DR Libs（QtMultimedia 内嵌）", "0.14.4", "Unlicense / MIT",
  "音频重采样（DR_WAV/DR_MP3，FFmpeg 后端解码）");
emb("qt_emb_signalsmith", "Signalsmith Stretch（QtMultimedia 内嵌）", "1.0.0", "MIT",
  "高质量音频变速变调（音高/速率独立控制）");
emb("qt_emb_tlsf", "Two-Level Segregated Fit（QtMultimedia 内嵌）", "v3.1", "BSD-3",
  "实时内存分配器（音频缓冲）");

// Qt Network 内嵌
emb("qt_emb_libpsl", "libpsl + Public Suffix List（QtNetwork 内嵌）", "PSL 2026-01-16", "BSD-3 / MPL-2",
  "公共后缀列表解析（cookie 域/安全域判定）");

// Qt Pdf 内嵌
emb("qt_emb_pdfium", "PDFium（QtPdf 内嵌）", "Chromium PDFium", "BSD-3",
  "Chromium 的 PDF 引擎（QPdfDocument/QPdfPageRenderer 核心，静态编入 Qt6Pdf）");

// Qt Qml 内嵌
emb("qt_emb_jscore_macro", "JavaScriptCore Macro Assembler（QtQml 内嵌）", "—", "BSD-2",
  "JIT 宏汇编器（QML JS 引擎 V4 编译后端）");

// Qt Quick 内嵌
emb("qt_emb_yoga", "Yoga（QtQuick 内嵌）", "2.0.1", "MIT",
  "Flexbox 布局引擎（QtQuick.Layouts / 布局计算）");

// Qt Shader Tools 内嵌
emb("qt_emb_spirv_cross", "SPIRV-Cross（QtShaderTools 内嵌）", "—", "Apache-2 / MIT",
  "SPIR-V 反射与跨后端转换（rhi 着色器处理）");
emb("qt_emb_glslang", "glslang（QtShaderTools 内嵌）", "16.1.0", "BSD-3 / MIT / Apache-2",
  "GLSL/HLSL → SPIR-V 编译器（qsb 工具链）");

// Qt Virtual Keyboard 内嵌
emb("qt_emb_openwnn", "OpenWnn（QtVirtualKeyboard 内嵌）", "—", "Apache-2",
  "日语输入法（假名 → 汉字转换）");
emb("qt_emb_pinyinime", "PinyinIME（QtVirtualKeyboard 内嵌）", "—", "Apache-2",
  "拼音输入法（汉字候选）");
emb("qt_emb_tcime", "tcime（QtVirtualKeyboard 内嵌）", "—", "Apache-2 / BSD-3",
  "繁体中文注音输入法");

// Qt Test 内嵌
emb("qt_emb_catch2", "Catch2（QtTest 内嵌）", "2.13.10", "BSL-1.0",
  "测试断言库（QtTest 内部使用）");

/* ---------- 内嵌库边：Qt 模块 → 内嵌库（build 编译期编入） ---------- */
const embEdges = [
  ["qt_core", "qt_emb_pcre2"],
  ["qt_core", "qt_emb_zlib"],
  ["qt_core", "qt_emb_tinycbor"],
  ["qt_core", "qt_emb_doubleconversion"],
  ["qt_core", "qt_emb_hash"],
  ["qt_core", "qt_emb_easing"],
  ["qt_core", "qt_emb_clrd"],
  ["qt_gui", "qt_emb_freetype"],
  ["qt_gui", "qt_emb_harfbuzz"],
  ["qt_gui", "qt_emb_libpng"],
  ["qt_gui", "qt_emb_libjpeg"],
  ["qt_gui", "qt_emb_md4c"],
  ["qt_gui", "qt_emb_pixman"],
  ["qt_gui", "qt_emb_d3d12ma"],
  ["qt_gui", "qt_emb_vma"],
  ["qt_gui", "qt_emb_vulkanheaders"],
  ["qt_gui", "qt_emb_openglheaders"],
  ["qt_gui", "qt_emb_emoji_segmenter"],
  ["qt_emb_freetype", "qt_emb_libpng"],          // BundledFreetype → WrapPNG
  ["qt_imageformats", "qt_emb_libtiff"],
  ["qt_imageformats", "qt_emb_libwebp"],
  ["qt_multimedia", "qt_emb_drlibs"],
  ["qt_multimedia", "qt_emb_signalsmith"],
  ["qt_multimedia", "qt_emb_tlsf"],
  ["qt_network", "qt_emb_libpsl"],
  ["qt_pdf", "qt_emb_pdfium"],
  ["qt_qml", "qt_emb_jscore_macro"],
  ["qt_quick", "qt_emb_yoga"],
  ["qt_shadertools", "qt_emb_spirv_cross"],
  ["qt_shadertools", "qt_emb_glslang"],
  ["qt_virtualkeyboard", "qt_emb_openwnn"],
  ["qt_virtualkeyboard", "qt_emb_pinyinime"],
  ["qt_virtualkeyboard", "qt_emb_tcime"],
  ["qt_test", "qt_emb_catch2"],
];
embEdges.forEach(([s, t]) => addEdge(s, t, "build"));

/* ========================================================================
 * 5) 系统组件细化（L8，owner=microsoft）
 *    图形/DirectX 族拆分为独立节点；新增媒体/语音/平台插件系统库
 * ====================================================================== */
const sys = (id, label, desc) =>
  addNode({ id, label, layer: 8, kind: "runtime", owner: "microsoft", desc });

// 图形 / DirectX 族
sys("win_d3d11", "d3d11 (Direct3D 11)", "Direct3D 11（Qt RHI 主渲染后端 + WMF/EVR 呈现 + FFmpeg 硬件解码）");
sys("win_d3d12", "d3d12 + dxcompiler + dxil", "Direct3D 12（Qt RHI D3D12 后端；dxcompiler/dxil 为 HLSL→DXIL 编译器，windeployqt 部署）");
sys("win_dxgi", "dxgi + dxguid", "DXGI 图形基础设施（适配器/交换链；dxguid 为 GUID 定义库）");
sys("win_directwrite", "dwrite (DirectWrite)", "DirectWrite 文字排版（qwindows 平台插件字体渲染）");
sys("win_direct2d", "d2d1 (Direct2D)", "Direct2D（qwindows 可选 Direct2D 呈现路径）");
sys("win_d3d9", "d3d9 (Direct3D 9)", "Direct3D 9（qwindows 旧版 GL 测试/回退）");
sys("win_dcomp", "dcomp (DirectComposition)", "DirectComposition 桌面合成（Qt Quick RHI 窗口合成）");
sys("win_opengl", "opengl32 + opengl32sw", "OpenGL 系统库 + opengl32sw.dll 软件渲染回退（无 GPU 时）");
sys("win_d3dcompiler", "D3Dcompiler_47", "Direct3D HLSL 编译器（RHI 着色器编译，windeployqt 部署）");

// 媒体 / 语音 / 系统服务
sys("win_mediafoundation", "Media Foundation", "Windows Media Foundation：Mfplat.dll / Mf.dll / Mfreadwrite.dll（MFStartup/MFCreateSourceReader…，WMF 多媒体后端核心）");
sys("win_wasapi", "WASAPI 音频", "mmdeviceapi.dll + audioclient.dll（Windows 音频会话 API，音频输出/输入）");
sys("win_evr", "EVR (evr.dll)", "增强视频渲染器（WMF 后端视频呈现，D3D11 交换链）");
sys("win_sapi", "SAPI (sapi.dll)", "Speech API 5（qtexttospeech_sapi 后端：ISpVoice COM 语音合成）");
sys("win_winrt", "Windows Runtime", "WinRT 运行时（runtimeobject.dll；qtexttospeech_winrt 后端经 RoActivateInstance 激活 Windows.Media.SpeechSynthesis）");
sys("win_wmcodecdsp", "wmcodecdsp", "Windows 媒体编解码器 DSP（音频/视频 MFT 编解码）");

// 平台插件系统库（qwindows 链接 + 运行时）
sys("win_gdi32", "gdi32", "GDI 图形设备接口（字体/绘图/DIB）");
sys("win_uxtheme", "uxtheme", "Windows 主题引擎（控件视觉风格）");
sys("win_wintab", "wintab32", "Wintab 数位板 API（qwindows 手写笔/压感，动态加载）");
sys("win_imm32", "imm32", "IMM32 输入法管理器（IME 组合窗口）");
sys("win_setupapi", "setupapi", "设备安装 API（qwindows 平台插件）");
sys("win_shlwapi", "shlwapi", "Shell 轻量工具（路径/注册表辅助，qwindows + WinRT TTS）");
sys("win_version", "version.dll", "版本资源 API（qwindows 读取系统/驱动版本）");
sys("win_winmm", "winmm", "Windows 多媒体（计时器/低层音频，qwindows）");
sys("win_wtsapi", "wtsapi32", "终端服务 API（会话/桌面监控，qwindows）");
sys("win_shcore", "shcore", "Shell 核心（Per-Monitor DPI 感知，qwindows）");
sys("win_mscms", "mscms", "Windows 颜色管理（ICC 色彩配置，qwindows）");
sys("win_schannel_sys", "schannel", "Windows 安全通道（TLS/SSL 加密协议，Qt Schannel 后端底层）");

/* ---------- 系统组件边 ---------- */
const sysEdges = [
  // 聚合节点 win_graphics 的拆分（win_graphics 保留为聚合标记）
  ["win_graphics", "win_d3d11", "runtime"],
  ["win_graphics", "win_d3d12", "runtime"],
  ["win_graphics", "win_dxgi", "runtime"],
  ["win_graphics", "win_directwrite", "runtime"],
  ["win_graphics", "win_direct2d", "runtime"],
  ["win_graphics", "win_d3d9", "runtime"],
  ["win_graphics", "win_dcomp", "runtime"],
  ["win_graphics", "win_opengl", "runtime"],
  ["win_graphics", "win_d3dcompiler", "runtime"],
  // Qt RHI / Gui / 平台插件的图形后端
  ["qt_quick", "win_d3d11", "runtime"],
  ["qt_quick", "win_d3d12", "runtime"],
  ["qt_quick", "win_dxgi", "runtime"],
  ["qt_quick", "win_dcomp", "runtime"],
  ["qt_gui", "win_d3d11", "runtime"],
  ["qt_gui", "win_dxgi", "runtime"],
  ["qt_gui", "win_gdi32", "runtime"],
  ["qt_gui", "win_uxtheme", "runtime"],
  // qwindows 平台插件系统库
  ["qt_qwindows", "win_directwrite", "runtime"],
  ["qt_qwindows", "win_direct2d", "runtime"],
  ["qt_qwindows", "win_d3d9", "runtime"],
  ["qt_qwindows", "win_gdi32", "runtime"],
  ["qt_qwindows", "win_uxtheme", "runtime"],
  ["qt_qwindows", "win_wintab", "runtime"],
  ["qt_qwindows", "win_imm32", "runtime"],
  ["qt_qwindows", "win_setupapi", "runtime"],
  ["qt_qwindows", "win_shlwapi", "runtime"],
  ["qt_qwindows", "win_version", "runtime"],
  ["qt_qwindows", "win_winmm", "runtime"],
  ["qt_qwindows", "win_wtsapi", "runtime"],
  ["qt_qwindows", "win_shcore", "runtime"],
  ["qt_qwindows", "win_mscms", "runtime"],
  // 多媒体后端系统依赖
  ["qt_wmf_plugin", "win_mediafoundation", "runtime"],
  ["qt_wmf_plugin", "win_wasapi", "runtime"],
  ["qt_wmf_plugin", "win_evr", "runtime"],
  ["qt_wmf_plugin", "win_wmcodecdsp", "runtime"],
  ["qt_wmf_plugin", "win_d3d11", "runtime"],
  ["qt_ffmpeg_plugin", "win_d3d11", "runtime"],
  ["qt_ffmpeg_plugin", "win_dxgi", "runtime"],
  // TTS 后端
  ["qt_tts_sapi", "win_sapi", "runtime"],
  ["qt_tts_winrt", "win_winrt", "runtime"],
  // TLS
  ["qt_schannel", "win_schannel_sys", "runtime"],
  // 系统组件之间的依赖
  ["win_d3d11", "win_dxgi", "runtime"],
  ["win_d3d12", "win_dxgi", "runtime"],
  ["win_direct2d", "win_d3d11", "runtime"],
  ["win_direct2d", "win_dxgi", "runtime"],
  ["win_dcomp", "win_dxgi", "runtime"],
  ["win_evr", "win_d3d11", "runtime"],
  ["win_evr", "win_dxgi", "runtime"],
  ["win_directwrite", "win_gdi32", "runtime"],
  ["win_wasapi", "win_system", "runtime"],
  ["win_mediafoundation", "win_system", "runtime"],
  ["win_winrt", "win_system", "runtime"],
  ["win_sapi", "win_system", "runtime"],
];
sysEdges.forEach(([s, t, ty]) => addEdge(s, t, ty));

/* ========================================================================
 * 6) 版本 / 描述修正
 * ====================================================================== */
setVersion("ffmpeg", "7.1.3");
setDesc("ffmpeg", "FFmpeg 7.1.3（QtMultimedia FFmpeg 后端）：avcodec-61 / avformat-61 / avutil-59 / swresample-5 / swscale-8（Qt 6.11 内嵌打包，windeployqt 部署）");
setDesc("win_graphics", "图形/DirectX 聚合（子节点：d3d11/d3d12/dxgi/dwrite/d2d/d3d9/dcomp/opengl/D3Dcompiler）");
setDesc("qt_qwindows", "Windows 平台集成插件（platforms/qwindows.dll），Qt GUI 运行必需；链接 gdi32/dwrite/d3d9/imm32/setupapi 等 17 个系统库");
setDesc("qt_imageformats", "imageformats/：gif / icns / ico / jpeg / pdf / svg / tga / tiff / wbmp / webp（内嵌 libtiff + libwebp）");
setDesc("qt_texttospeech", "TTS 朗读（可选，TRANSLEX_HAS_TTS）；Windows 带 SAPI + WinRT 双后端插件");

/* ========================================================================
 * 7) meta 更新
 * ====================================================================== */
data.meta.date = "2026-08-20";
data.meta.sources = [
  "CMakeLists.txt / tests/CMakeLists.txt / CMakePresets.json",
  "src/services/** 的 #include（源码级引用）",
  "qml/** 的 import（QML 依赖）",
  "third_party/FluentUI, quazip, zlib（git 子模块 + 本地补丁）",
  "D:/Software/Qt/6.11.1/msvc2022_64/lib/cmake/*/*Dependencies.cmake（Qt 模块传递依赖矩阵）",
  "Qt 6.11.0 官方 licenses-used-in-qt.html（Qt 源码内嵌第三方库清单）",
  "GitHub qt/qtbase、qt/qtmultimedia、qt/qtspeech、qt/qtwebengine 源码（Windows 系统库链接）",
  "build-vs2026-x64/Debug/*.dll + 各插件子目录（windeployqt 部署的真实运行时清单）"
];

writeFileSync(dataPath, JSON.stringify(data, null, 2), 'utf8');
console.log(`✅ 深度增强完成: ${dataPath}`);
console.log(`   节点 ${data.nodes.length} 个, 边 ${data.edges.length} 条`);
console.log(`   实现主体 ${data.owners.length} 个, 层级 ${data.layers.length} 个`);
