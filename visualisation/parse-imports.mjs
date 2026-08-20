#!/usr/bin/env node
// PE 导入表解析工具：列出 Windows DLL/exe 的真实动态链接依赖
// 用法：node parse-imports.mjs <文件1> [文件2] ...
import { readFileSync } from 'node:fs';

function getImports(path) {
  const b = readFileSync(path);
  const peOff = b.readUInt32LE(0x3C);
  const optStart = peOff + 24;
  const numSec = b.readUInt16LE(peOff + 6);          // COFF+2: NumberOfSections
  const optSize = b.readUInt16LE(peOff + 20);        // COFF+16: SizeOfOptionalHeader
  const secStart = optStart + optSize;
  const importRva = b.readUInt32LE(optStart + 112 + 8); // DataDirectory[1].VirtualAddress
  const sections = [];
  for (let i = 0; i < numSec; i++) {
    const o = secStart + i * 40;
    sections.push({
      name: b.toString('ascii', o, o + 8).replace(/\0/g, '').trim(),
      vsize: b.readUInt32LE(o + 8), va: b.readUInt32LE(o + 12),
      raw: b.readUInt32LE(o + 20),
    });
  }
  const rva2off = (rva) => {
    for (const s of sections) if (rva >= s.va && rva < s.va + s.vsize) return s.raw + (rva - s.va);
    return -1;
  };
  const dlls = new Set();
  let off = rva2off(importRva);
  if (off < 0) return [];
  for (;;) {
    const nameRva = b.readUInt32LE(off + 12);
    if (nameRva === 0) break;
    let no = rva2off(nameRva);
    if (no < 0) break;
    let name = '';
    while (b[no] !== 0) name += String.fromCharCode(b[no++]);
    dlls.add(name.toLowerCase());
    off += 20;
  }
  return [...dlls].sort();
}

for (const p of process.argv.slice(2)) {
  console.log(`\n=== ${p.split(/[\\/]/).pop()} ===`);
  try {
    console.log('  ' + getImports(p).join('\n  '));
  } catch (e) {
    console.log('  (解析失败: ' + e.message + ')');
  }
}
