// Vision MCP 服务器：让 opencode agent「看图」——审查 UI 截图/设计稿。
// 协议：MCP stdio（JSON-RPC 2.0，换行分隔）。
// 提供者优先级（无需环境变量）：
//   1. opencode-go 凭据（默认）：自动读 ~/.local/share/opencode/auth.json，
//      默认模型 minimax-m3（实测支持视觉；VISION_MODEL 环境变量可覆盖）
//   2. 智谱 GLM：key 放 .opencode/mcp/vision/keys.local.json（gitignore，不入库）
//      {"zhipuApiKey": "sk-..."}，模型默认 glm-4.6v-flashx（VISION_MODEL 可覆盖）
//   3. OLLAMA_VISION_MODEL → 本地 Ollama 视觉模型（如 qwen2.5vl:7b）
// 用法示例（发给 agent）：「用 vision describe_image 看 qml/TranslateHomePage.qml 的截图
//   C:\Users\sr291\Desktop\ui.png，审查布局与配色」
import { readFileSync, existsSync } from 'node:fs';
import { homedir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import readline from 'node:readline';

const log = (...a) => process.stderr.write(`[vision-mcp] ${a.join(' ')}\n`);
const serverDir = dirname(fileURLToPath(import.meta.url));

function getGoKeyFromAuth() {
  try {
    const p = join(homedir(), '.local', 'share', 'opencode', 'auth.json');
    if (!existsSync(p)) return null;
    const auth = JSON.parse(readFileSync(p, 'utf8'));
    const go = auth['opencode-go'];
    if (!go) return null;
    return go.key || go.apiKey || (go.credentials && go.credentials.key) || null;
  } catch {
    return null;
  }
}

function getZhipuKeyFromFile() {
  try {
    const p = join(serverDir, 'keys.local.json');
    if (!existsSync(p)) return null;
    const k = JSON.parse(readFileSync(p, 'utf8'));
    return k.zhipuApiKey || null;
  } catch {
    return null;
  }
}

function providerConfig() {
  const goKey = getGoKeyFromAuth();
  if (goKey) {
    return {
      name: 'opencode-go',
      model: process.env.VISION_MODEL || 'minimax-m3',
      url: 'https://opencode.ai/zen/go/v1/chat/completions',
      key: goKey,
    };
  }
  const zhipuKey = getZhipuKeyFromFile();
  if (zhipuKey) {
    return {
      name: 'zhipu',
      model: process.env.VISION_MODEL || 'glm-4.6v-flashx',
      url: 'https://open.bigmodel.cn/api/paas/v4/chat/completions',
      key: zhipuKey,
    };
  }
  if (process.env.OLLAMA_VISION_MODEL) {
    return {
      name: 'ollama',
      model: process.env.OLLAMA_VISION_MODEL,
      url: (process.env.OLLAMA_URL || 'http://127.0.0.1:11434') + '/api/chat',
      key: null,
    };
  }
  return null;
}

async function callVision(cfg, imageBase64, prompt) {
  const messages = [
    {
      role: 'user',
      content:
        cfg.name === 'ollama'
          ? [
              { type: 'image', image: imageBase64 },
              { type: 'text', text: prompt },
            ]
          : [
              { type: 'image_url', image_url: { url: `data:image/png;base64,${imageBase64}` } },
              { type: 'text', text: prompt },
            ],
    },
  ];
  const body =
    cfg.name === 'ollama'
      ? { model: cfg.model, messages, stream: false }
      : { model: cfg.model, messages, stream: false };

  const headers = { 'Content-Type': 'application/json' };
  if (cfg.key) headers.Authorization = `Bearer ${cfg.key}`;

  const res = await fetch(cfg.url, {
    method: 'POST',
    headers,
    body: JSON.stringify(body),
    signal: AbortSignal.timeout(60000),
  });
  if (!res.ok) {
    const err = await res.text().catch(() => '');
    throw new Error(`vision API ${res.status}: ${err.slice(0, 300)}`);
  }
  const data = await res.json();
  let text = '';
  if (cfg.name === 'ollama') {
    text = data.message?.content || '';
  } else {
    const c = data.choices?.[0]?.message?.content;
    text = Array.isArray(c) ? c.map((x) => x.text || '').join('\n') : String(c || '');
  }
  // 去掉 <think> 推理块（部分模型如 minimax-m3 会包一层）
  return text.replace(/<think>[\s\S]*?<\/think>/g, '').trim();
}

const TOOLS = [
  {
    name: 'describe_image',
    description:
      '用视觉模型分析本地图片（UI 截图/设计稿）：描述布局、配色、间距、对齐、可读性、可用性隐患。' +
      'path 为本地图片绝对路径；prompt 可选，指定审查重点（默认 UI 审查）。',
    inputSchema: {
      type: 'object',
      properties: {
        path: { type: 'string', description: '本地图片绝对路径（png/jpg）' },
        prompt: {
          type: 'string',
          description: '审查重点提示词，例如「审查这个 QML 界面的布局与配色，指出问题」',
        },
      },
      required: ['path'],
    },
  },
  {
    name: 'vision_status',
    description: '查看当前视觉提供者与模型配置（调试用，不含密钥）',
    inputSchema: { type: 'object', properties: {} },
  },
];

function jsonRpc(id, result) {
  process.stdout.write(JSON.stringify({ jsonrpc: '2.0', id, result }) + '\n');
}

function jsonRpcError(id, code, message) {
  process.stdout.write(
    JSON.stringify({ jsonrpc: '2.0', id, error: { code, message } }) + '\n'
  );
}

const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
rl.on('line', async (line) => {
  if (!line.trim()) return;
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    log('bad json:', line.slice(0, 200));
    return;
  }
  const { id, method, params } = msg;

  try {
    if (method === 'initialize') {
      jsonRpc(id, {
        protocolVersion: '2024-11-05',
        capabilities: { tools: {} },
        serverInfo: { name: 'vision', version: '1.0.0' },
      });
    } else if (method === 'notifications/initialized' || method === 'ping') {
      if (id !== undefined) jsonRpc(id, {});
    } else if (method === 'tools/list') {
      jsonRpc(id, { tools: TOOLS });
    } else if (method === 'tools/call') {
      const name = params?.name;
      const args = params?.arguments || {};
      if (name === 'vision_status') {
        const cfg = providerConfig();
        jsonRpc(id, {
          content: [
            {
              type: 'text',
              text: cfg
                ? `provider=${cfg.name} model=${cfg.model} url=${cfg.url}`
                : '未配置视觉提供者：设置 ZHIPU_API_KEY，或安装 Ollama 视觉模型并设 OLLAMA_VISION_MODEL',
            },
          ],
        });
        return;
      }
      if (name === 'describe_image') {
        const p = args.path;
        if (!p || !existsSync(p)) {
          throw new Error(`图片不存在: ${p}`);
        }
        const data = readFileSync(p);
        if (data.length > 8 * 1024 * 1024) {
          throw new Error('图片超过 8MB，请先缩小截图');
        }
        const cfg = providerConfig();
        if (!cfg) {
          throw new Error(
            '未配置视觉提供者：1) 设置环境变量 ZHIPU_API_KEY；2) 或依赖 opencode-go 凭据；3) 或设 OLLAMA_VISION_MODEL'
          );
        }
        const prompt =
          args.prompt ||
          '请审查这张软件界面截图：描述整体布局、配色、间距、对齐、字体层级，并指出视觉/可用性问题（按严重程度排序）。用中文回答。';
        const text = await callVision(cfg, data.toString('base64'), prompt);
        jsonRpc(id, { content: [{ type: 'text', text }] });
        return;
      }
      throw new Error(`未知工具: ${name}`);
    } else {
      log('unhandled method:', method);
      if (id !== undefined) jsonRpcError(id, -32601, `unknown method: ${method}`);
    }
  } catch (e) {
    log('error:', e.message);
    if (id !== undefined) jsonRpcError(id, -32000, String(e.message || e));
  }
});
