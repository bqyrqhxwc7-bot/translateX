// Translex UI 驱动客户端（review agent 模拟用户操作）：
// 连接应用内 UiDriverService（TRANSLEX_UI_DRIVER=1 启动的 translex）的
// named pipe，发送业务动作命令并返回结果。协议见 src/driver_service.h。
//
// 用法（review agent）：
//   node .opencode/scripts/ui-driver.mjs --action openFile --file samples/demo.docx
//   node .opencode/scripts/ui-driver.mjs --action setDark --dark true
//   node .opencode/scripts/ui-driver.mjs --action getState
//   node .opencode/scripts/ui-driver.mjs --action translateLine --line 0
//   node .opencode/scripts/ui-driver.mjs --action translateAll
// 组合示例（操作 → 截图 → vision 断言）：
//   node .opencode/scripts/ui-driver.mjs --action openFile --file samples/demo.docx
//   pwsh -File .opencode/scripts/screenshot.ps1 -Out %TEMP%\ui.png
import net from 'node:net';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const pipeName = 'translex-ui-driver';
const pipePath = process.platform === 'win32' ? `\\\\.\\pipe\\${pipeName}` : pipeName;

// ---- 参数解析：--action <cmd> --file <path> --dark <true|false> --line <n> ----
const argv = process.argv.slice(2);
const opts = { action: 'getState' };
for (let i = 0; i < argv.length; ++i) {
  if (argv[i] === '--action') opts.action = argv[++i];
  else if (argv[i] === '--file') opts.file = argv[++i];
  else if (argv[i] === '--dark') opts.dark = argv[++i] === 'true' || argv[++i] === '1';
  else if (argv[i] === '--line') opts.line = parseInt(argv[++i], 10);
  else if (argv[i] === '--wait') opts.wait = parseInt(argv[++i], 10);
}

const commandArgs = { openFile: opts.file, setDark: opts.dark,
                      translateLine: opts.line };

function send(cmd, args, id) {
  return new Promise((resolve, reject) => {
    const socket = net.connect({ path: pipePath }, () => {
      socket.write(JSON.stringify({ id, cmd, args }) + '\n');
    });
    let buf = '';
    socket.on('data', (d) => {
      buf += d.toString();
      // 服务端逐行回复；收到完整行后自行关闭（服务端不主动断开）
      const nl = buf.indexOf('\n');
      if (nl >= 0) {
        const line = buf.slice(0, nl);
        buf = buf.slice(nl + 1);
        try {
          socket.end();
          resolve(JSON.parse(line));
        } catch {
          socket.destroy();
          reject(new Error('回复解析失败: ' + line));
        }
      }
    });
    socket.on('error', reject);
    socket.setTimeout(20000, () => { socket.destroy(); reject(new Error('命令超时')); });
  });
}

async function main() {
  // 若应用未运行（无 pipe），则启动（驱动模式）
  let appStarted = false;
  const probe = await new Promise((resolve) => {
    const s = net.connect({ path: pipePath });
    s.on('connect', () => { s.end(); resolve(true); });
    s.on('error', () => resolve(false));
  });
  if (!probe) {
    const exe = join(repoRoot, 'build-vs2026-x64', 'Debug', 'translex.exe');
    if (!existsSync(exe)) {
      console.error(JSON.stringify({ ok: false, error: `未找到 ${exe}` }));
      process.exit(1);
    }
    console.error('ui-driver: 启动应用（TRANSLEX_UI_DRIVER=1）...');
    spawn(exe, [], { env: { ...process.env, TRANSLEX_UI_DRIVER: '1' }, detached: true, stdio: 'ignore' }).unref();
    appStarted = true;
    // 等待 pipe 就绪（最多 15s）
    let ready = false;
    for (let i = 0; i < 30; ++i) {
      await new Promise((r) => setTimeout(r, 500));
      const ok = await new Promise((resolve) => {
        const s = net.connect({ path: pipePath });
        s.on('connect', () => { s.end(); resolve(true); });
        s.on('error', () => resolve(false));
      });
      if (ok) { ready = true; break; }
    }
    if (!ready) {
      console.error(JSON.stringify({ ok: false, error: '应用启动超时（pipe 未就绪）' }));
      process.exit(1);
    }
  }
  if (opts.wait) {
    await new Promise((r) => setTimeout(r, opts.wait));
  }

  const id = Date.now() % 100000;
  const reply = await send(opts.action, commandArgs[opts.action], id);
  // 输出 JSON（review 解析用）；错误走 stderr
  if (reply.ok) {
    console.log(JSON.stringify({ ok: true, result: reply.result }));
  } else {
    console.error(JSON.stringify({ ok: false, error: reply.result }));
    process.exit(1);
  }
  if (appStarted) {
    // 本脚本启动的应用：保活给后续截图用（review 结束时用 Stop-Process 清理）
    console.error('ui-driver: 应用已启动并保持运行（后续截图用 screenshot.ps1，结束后 Stop-Process -Name translex）');
  }
}

main().catch((e) => {
  console.error(JSON.stringify({ ok: false, error: String(e.message || e) }));
  process.exit(1);
});
