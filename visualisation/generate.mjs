#!/usr/bin/env node
// Translex 依赖可视化生成脚本
// 读取 data/dependencies.json → 生成自包含 dependency-graph.html（数据内嵌，离线可用）
// 用法：node generate.mjs   （在 visualisation/ 目录下执行）
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const dataPath = join(__dirname, 'data', 'dependencies.json');
const templatePath = join(__dirname, 'template.html');
const outPath = join(__dirname, 'dependency-graph.html');

const data = JSON.parse(readFileSync(dataPath, 'utf8'));
const template = readFileSync(templatePath, 'utf8');

if (!template.includes('/*__GRAPH_DATA__*/')) {
    console.error('模板缺少占位符 /*__GRAPH_DATA__*/');
    process.exit(1);
}

const html = template.replace('/*__GRAPH_DATA__*/', JSON.stringify(data));
writeFileSync(outPath, html, 'utf8');

const nodeCount = data.nodes.length;
const edgeCount = data.edges.length;
console.log(`✅ 生成完成: ${outPath}`);
console.log(`   节点 ${nodeCount} 个, 边 ${edgeCount} 条, ${(html.length / 1024).toFixed(1)} KB`);
