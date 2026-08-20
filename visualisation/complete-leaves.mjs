#!/usr/bin/env node
// 叶子节点补边脚本：为「实际有依赖但图上缺边」的叶子补齐出边（幂等，跳过已存在边）
// 依据：
//  - FFmpeg 5 DLL 导入表实测（parse-imports.mjs）
//  - src/services 源码 #include（Qt Core 依赖）
//  - Qt 内嵌库/Windows 系统库的公认依赖关系
// 用法：node complete-leaves.mjs  （在 visualisation/ 目录下执行，原地更新 data/dependencies.json）
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataPath = join(__dirname, 'data', 'dependencies.json');
const data = JSON.parse(readFileSync(dataPath, 'utf8'));
const have = (id) => data.nodes.some(n => n.id === id);
const edgeHas = (s, t) => data.edges.some(e => e.source === s && e.target === t);

// 需要补的出边：[source, target, type]
const ADD = [
  // ---- L1 服务层 → Qt（源码 #include 验证）----
  ['driver_service', 'qt_core', 'build'],
  ['driver_service', 'qt_qml', 'build'],
  ['appguard', 'qt_core', 'build'],
  ['translationcache', 'qt_core', 'build'],
  ['termglossary', 'qt_core', 'build'],
  ['qualitygate', 'qt_core', 'build'],
  ['commentservice', 'qt_core', 'build'],
  ['trxparser', 'qt_core', 'build'],
  ['translationhistoryservice', 'qt_core', 'build'],

  // ---- L2 三方库 ----
  ['translex_sdk', 'qt_core', 'build'],          // 接口用 QString/QObject
  ['plugin_src', 'translex_sdk', 'source'],       // 插件实现 SDK 接口
  ['zlib', 'msvc_runtime', 'build'],              // 纯 C 库，链 CRT

  // ---- L3 Qt 入口点 / 运行时插件 ----
  ['qt_entrypointprivate', 'win_system', 'build'], // winmain 依赖 kernel32
  ['qt_certonly', 'qt_core', 'runtime'],
  ['qt_certonly', 'qt_network', 'runtime'],
  ['qt_windowsstyle', 'qt_core', 'runtime'],
  ['qt_windowsstyle', 'qt_widgets', 'runtime'],
  ['qt_windowsstyle', 'win_system', 'runtime'],
  ['qt_windowsstyle', 'win_uxtheme', 'runtime'],
  ['qt_networkinfo', 'qt_core', 'runtime'],
  ['qt_networkinfo', 'qt_network', 'runtime'],
  ['qt_networkinfo', 'win_system', 'runtime'],
  ['qt_tts_mock', 'qt_core', 'runtime'],
  ['qt_tts_mock', 'qt_texttospeech', 'runtime'],
  ['qt_svgicon', 'qt_core', 'runtime'],
  ['qt_svgicon', 'qt_gui', 'runtime'],
  ['qt_svgicon', 'qt_svg', 'runtime'],
  ['qt_uiotouch', 'qt_core', 'runtime'],
  ['qt_uiotouch', 'qt_gui', 'runtime'],
  ['qt_vkeyboard_plugin', 'qt_core', 'runtime'],
  ['qt_vkeyboard_plugin', 'qt_qml', 'runtime'],
  ['qt_vkeyboard_plugin', 'qt_quick', 'runtime'],
  ['qt_vkeyboard_plugin', 'qt_virtualkeyboard', 'runtime'],
  ['qt_vkeyboard_plugin', 'win_imm32', 'runtime'],
  ['qt_lottie_vectorimage', 'qt_core', 'runtime'],
  ['qt_lottie_vectorimage', 'qt_lottie', 'runtime'],
  ['qt_lottie_vectorimage', 'qt_svg', 'runtime'],
  ['qt_qmltooling', 'qt_core', 'runtime'],
  ['qt_qmltooling', 'qt_qml', 'runtime'],
  ['qt_qmltooling', 'qt_network', 'runtime'],      // qmldbg_server 用 TCP

  // ---- L4 Qt 内嵌库 → C 运行时（编译产物均链 CRT；vulkanheaders/openglheaders 纯头文件除外）----
  ...['pcre2', 'zlib', 'tinycbor', 'doubleconversion', 'hash', 'easing', 'clrd',
     'harfbuzz', 'libpng', 'libjpeg', 'md4c', 'pixman', 'd3d12ma', 'vma',
     'emoji_segmenter', 'libtiff', 'libwebp', 'drlibs', 'signalsmith', 'tlsf',
     'libpsl', 'pdfium', 'jscore_macro', 'yoga', 'spirv_cross', 'glslang',
     'openwnn', 'pinyinime', 'tcime', 'catch2'].map(id => [`qt_emb_${id}`, 'msvc_runtime', 'build']),

  // ---- L4 内嵌库之间的真实依赖 ----
  ['qt_emb_libpng', 'qt_emb_zlib', 'build'],         // libpng 依赖 zlib
  ['qt_emb_libtiff', 'qt_emb_zlib', 'build'],
  ['qt_emb_libtiff', 'qt_emb_libjpeg', 'build'],
  ['qt_emb_pdfium', 'qt_emb_freetype', 'build'],     // PDFium 依赖 FreeType
  ['qt_emb_pdfium', 'qt_emb_libjpeg', 'build'],
  ['qt_emb_pdfium', 'qt_emb_libpng', 'build'],
  ['qt_emb_pdfium', 'qt_emb_zlib', 'build'],
  ['qt_emb_pdfium', 'win_system', 'build'],          // PDFium 用 kernel32/gdi32 等

  // ---- L4 FluentUI 内嵌 ----
  ['fluentui_qrcode', 'msvc_runtime', 'build'],       // qrencode 纯 C
  ['fluentui_qmlcustomplot', 'qt_core', 'build'],     // qcustomplot 是 Qt 库
  ['fluentui_qmlcustomplot', 'qt_gui', 'build'],
  ['fluentui_qmlcustomplot', 'qt_widgets', 'build'],

  // ---- L5-L7 工具链（原生程序，链 CRT；Qt 工具链 QtCore）----
  ['msvc', 'msvc_runtime', 'tool'],
  ['ninja', 'msvc_runtime', 'tool'],
  ['vswhere', 'msvc_runtime', 'tool'],
  ['ctest', 'msvc_runtime', 'tool'],
  ['nsis', 'msvc_runtime', 'tool'],
  ['qt_tools', 'qt_core', 'tool'],
  ['qt_tools', 'msvc_runtime', 'tool'],

  // ---- L8 FFmpeg（导入表实测：kernel32/ole32/secur32/ws2_32/bcrypt + user32）----
  ['ffmpeg', 'win_system', 'runtime'],
  ['ffmpeg', 'win_user32', 'runtime'],

  // ---- L8 Windows 系统库（OS 公开依赖关系）----
  ['win_user32', 'win_system', 'runtime'],
  ['win_dwmapi', 'win_system', 'runtime'],
  ['win_gdi32', 'win_system', 'runtime'],
  ['win_gdi32', 'win_user32', 'runtime'],
  ['win_dxgi', 'win_system', 'runtime'],
  ['win_d3d9', 'win_system', 'runtime'],
  ['win_d3d9', 'win_user32', 'runtime'],
  ['win_d3d9', 'win_gdi32', 'runtime'],
  ['win_opengl', 'win_system', 'runtime'],
  ['win_opengl', 'win_user32', 'runtime'],
  ['win_opengl', 'win_gdi32', 'runtime'],
  ['win_d3dcompiler', 'win_system', 'runtime'],
  ['win_d3dcompiler', 'win_user32', 'runtime'],
  ['win_wmcodecdsp', 'win_system', 'runtime'],
  ['win_wmcodecdsp', 'win_mediafoundation', 'runtime'], // mfplat
  ['win_uxtheme', 'win_system', 'runtime'],
  ['win_uxtheme', 'win_user32', 'runtime'],
  ['win_uxtheme', 'win_gdi32', 'runtime'],
  ['win_wintab', 'win_system', 'runtime'],
  ['win_wintab', 'win_user32', 'runtime'],
  ['win_imm32', 'win_system', 'runtime'],
  ['win_imm32', 'win_user32', 'runtime'],
  ['win_setupapi', 'win_system', 'runtime'],
  ['win_setupapi', 'win_user32', 'runtime'],
  ['win_shlwapi', 'win_system', 'runtime'],
  ['win_shlwapi', 'win_user32', 'runtime'],
  ['win_version', 'win_system', 'runtime'],
  ['win_winmm', 'win_system', 'runtime'],
  ['win_winmm', 'win_user32', 'runtime'],
  ['win_wtsapi', 'win_system', 'runtime'],
  ['win_shcore', 'win_system', 'runtime'],
  ['win_shcore', 'win_user32', 'runtime'],
  ['win_mscms', 'win_system', 'runtime'],
  ['win_mscms', 'win_user32', 'runtime'],
  ['win_mscms', 'win_gdi32', 'runtime'],
  ['win_schannel_sys', 'win_system', 'runtime'],      // crypt32/secur32/ncrypt
];

let added = 0, skipped = 0, missing = [];
for (const [s, t, ty] of ADD) {
  if (!have(s)) { missing.push(`源缺失:${s}`); continue; }
  if (!have(t)) { missing.push(`目标缺失:${t}(${s})`); continue; }
  if (edgeHas(s, t)) { skipped++; continue; }
  data.edges.push({ source: s, target: t, type: ty });
  added++;
}

// 补边后重新统计叶子（无出边）
const out = {};
data.edges.forEach(e => { (out[e.source] = out[e.source] || []).push(e.target); });
const leaves = data.nodes.filter(n => !(out[n.id] && out[n.id].length));

writeFileSync(dataPath, JSON.stringify(data, null, 2), 'utf8');
console.log(`✅ 补边完成：新增 ${added} 条，跳过重复 ${skipped} 条`);
if (missing.length) console.log('⚠️  引用缺失:', missing.join('; '));
console.log(`   节点 ${data.nodes.length}, 边 ${data.edges.length}`);
console.log(`\n补边后剩余叶子节点 ${leaves.length} 个:`);
leaves.forEach(n => console.log('  -', n.id, '|', n.label));
