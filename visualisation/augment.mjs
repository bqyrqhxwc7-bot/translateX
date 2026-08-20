#!/usr/bin/env node
// 数据增强脚本（一次性）：为 dependencies.json 补充遗漏依赖 + 给节点分配「实现主体」(owner)
// 用法：node augment.mjs   （在 visualisation/ 目录下执行，会原地更新 data/dependencies.json）
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataPath = join(__dirname, 'data', 'dependencies.json');
const data = JSON.parse(readFileSync(dataPath, 'utf8'));
const have = (id) => data.nodes.some(n => n.id === id);
const edgeHas = (s, t) => data.edges.some(e => e.source === s && e.target === t);

/* ---------- 1) 补充节点 ---------- */
const newNodes = [
  { id: "qt_qwindows", label: "Qt 平台插件 (qwindows)", layer: 3, kind: "qt", version: "6.11.1", owner: "qt",
    desc: "Windows 平台集成插件（platforms/qwindows.dll），Qt GUI 运行必需" },
  { id: "qt_schannel", label: "Qt TLS 后端 (Schannel)", layer: 3, kind: "qt", version: "6.11.1", owner: "qt",
    desc: "Windows Schannel TLS 后端（tls/qschannelbackend.dll），HTTPS 用系统证书存储" },
  { id: "qt_ffmpeg_plugin", label: "Qt 多媒体后端 (FFmpeg)", layer: 3, kind: "qt", version: "6.11.1", owner: "qt",
    desc: "multimedia/ffmpegmediaplugin.dll（QtMultimedia FFmpeg 后端插件）" },
  { id: "qt_wmf_plugin", label: "Qt 多媒体后端 (WMF)", layer: 3, kind: "qt", version: "6.11.1", owner: "qt",
    desc: "multimedia/windowsmediaplugin.dll（Windows Media Foundation 后端）" },
  { id: "qt_imageformats", label: "Qt 图片格式插件", layer: 3, kind: "qt", version: "6.11.1", owner: "qt",
    desc: "imageformats/：gif / icns / ico / jpeg / pdf / svg / tga / tiff / wbmp / webp" },
  { id: "fluentui_assets", label: "FluentIcons 字体 + i18n", layer: 4, kind: "embedded", owner: "fluentui",
    desc: "FluentIcons.ttf 图标字体 + fluentui_en_US/zh_CN.qm 翻译资源（内嵌 qrc）" },
];
newNodes.forEach(n => { if (!have(n.id)) data.nodes.push(n); });

/* ---------- 2) 补充边 ---------- */
const newEdges = [
  ["qt_gui", "qt_qwindows", "runtime"],
  ["qt_qwindows", "win_system", "runtime"],
  ["qt_network", "qt_schannel", "runtime"],
  ["qt_schannel", "win_system", "runtime"],
  ["qt_multimedia", "qt_ffmpeg_plugin", "runtime"],
  ["qt_ffmpeg_plugin", "ffmpeg", "runtime"],
  ["qt_multimedia", "qt_wmf_plugin", "runtime"],
  ["qt_wmf_plugin", "win_system", "runtime"],
  ["qt_gui", "qt_imageformats", "runtime"],
  ["qt_imageformats", "win_system", "runtime"],
  ["fluentui", "fluentui_assets", "source"],
];
newEdges.forEach(([s, t, ty]) => { if (!edgeHas(s, t)) data.edges.push({ source: s, target: t, type: ty }); });

/* ---------- 3) 实现主体 (owner) 元数据 ---------- */
data.owners = [
  { id: "translex", name: "本项目 (Translex)", color: "#4FC1FF", desc: "Translex 仓库（QML + services + 测试）" },
  { id: "qt", name: "Qt (The Qt Company)", color: "#744DA9", desc: "Qt 6.11.1 模块 / 运行时 / 插件" },
  { id: "fluentui", name: "FluentUI (zhuzichu520)", color: "#E6008D", desc: "FluentUI 1.7.7 组件库 + 内嵌子库" },
  { id: "quazip", name: "QuaZip (S. Tachenov)", color: "#C586C0", desc: "QuaZip 1.7.2" },
  { id: "zlib", name: "zlib (madler)", color: "#B5CEA8", desc: "zlib 1.3.2.1" },
  { id: "kitware", name: "Kitware", color: "#CE9178", desc: "CMake / CTest / CPack / Ninja" },
  { id: "microsoft", name: "Microsoft", color: "#0078D4", desc: "MSVC / VS 2026 / Windows 系统 / DirectX" },
  { id: "llvm", name: "LLVM", color: "#9CDCFE", desc: "clang-cl" },
  { id: "gcc", name: "GCC (MinGW)", color: "#F48771", desc: "MinGW g++" },
  { id: "ffmpeg", name: "FFmpeg 项目", color: "#6CCB5F", desc: "FFmpeg 6.x 库（QtMultimedia 后端）" },
  { id: "nsis", name: "NSIS 项目", color: "#DCDCAA", desc: "Nullsoft 脚本安装系统" },
];

/* ---------- 4) 节点分配 owner（按规则，缺失默认本项目） ---------- */
const ownerFor = (id) => {
  if (id.startsWith('qt_')) return 'qt';
  if (id.startsWith('fluentui')) return 'fluentui';
  if (id === 'quazip' || id === 'zlib') return id;
  if (['cmake', 'ctest', 'cpack', 'ninja'].includes(id)) return 'kitware';
  if (['msvc', 'vswhere', 'win_system', 'win_user32', 'win_dwmapi', 'win_graphics', 'msvc_runtime'].includes(id)) return 'microsoft';
  if (id === 'clangcl') return 'llvm';
  if (id === 'mingw') return 'gcc';
  if (id === 'ffmpeg') return 'ffmpeg';
  if (id === 'nsis') return 'nsis';
  return 'translex';
};
let assigned = 0;
data.nodes.forEach(n => {
  if (!n.owner) { n.owner = ownerFor(n.id); assigned++; }
});

// 修正 QuaZip 精确版本
const quazip = data.nodes.find(n => n.id === 'quazip');
if (quazip) quazip.version = '1.7.2';

writeFileSync(dataPath, JSON.stringify(data, null, 2), 'utf8');
console.log(`✅ 数据增强完成: ${dataPath}`);
console.log(`   节点 ${data.nodes.length} 个（新增 ${newNodes.length}）, 边 ${data.edges.length} 条（新增 ${newEdges.length}）`);
console.log(`   owner 分配 ${assigned} 个节点, 共 ${data.owners.length} 个实现主体`);
