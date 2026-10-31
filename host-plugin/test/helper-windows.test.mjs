import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import readline from 'node:readline';
import { existsSync } from 'node:fs';
import { helperScriptPath } from '../lib/serial-helper.js';

const isWindows = process.platform === 'win32';
const powershell = `${process.env.SystemRoot || 'C:\\Windows'}\\System32\\WindowsPowerShell\\v1.0\\powershell.exe`;

function waitFor(predicate, timeoutMs, label) {
  return new Promise((resolve, reject) => {
    const started = Date.now();
    const timer = setInterval(() => {
      try {
        if (predicate()) {
          clearInterval(timer);
          resolve();
        } else if (Date.now() - started > timeoutMs) {
          clearInterval(timer);
          reject(new Error(`timed out waiting for ${label}`));
        }
      } catch (error) {
        clearInterval(timer);
        reject(error);
      }
    }, 25);
    timer.unref?.();
  });
}

test('helper/serial-bridge.ps1 parses as PowerShell', { skip: !isWindows }, () => {
  assert.equal(existsSync(helperScriptPath()), true);
  const script = helperScriptPath().replace(/'/g, "''");
  const code = [
    '$tokens = $null',
    '$errors = $null',
    `[void][System.Management.Automation.Language.Parser]::ParseFile('${script}', [ref]$tokens, [ref]$errors)`,
    'if ($errors.Count -gt 0) { $errors | ForEach-Object { Write-Output $_.Message }; exit 1 }',
    'exit 0',
  ].join('; ');
  const result = spawnSync(powershell, ['-NoProfile', '-NonInteractive', '-Command', code], { encoding: 'utf8', timeout: 30000 });
  assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
});

test('helper process: ready, bad port reported, exits 0 when stdin closes', { skip: !isWindows, timeout: 30000 }, async () => {
  const child = spawn(
    powershell,
    ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', helperScriptPath()],
    { stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true },
  );
  const messages = [];
  const stderr = [];
  const cleanup = () => {
    try {
      child.kill();
    } catch {
      // ignore
    }
  };
  try {
    readline.createInterface({ input: child.stdout }).on('line', (line) => {
      try {
        messages.push(JSON.parse(line));
      } catch {
        // non-JSON stdout is a protocol violation but does not fail this test
      }
    });
    child.stderr.on('data', (chunk) => stderr.push(String(chunk)));

    await waitFor(() => messages.some((message) => message.op === 'ready'), 20000, 'helper ready');
    child.stdin.write(JSON.stringify({ op: 'open', port: 'COM99', baud: 115200 }) + '\n');
    await waitFor(
      () =>
        messages.some((message) => message.op === 'error') ||
        messages.some((message) => message.op === 'state' && message.connected === false),
      10000,
      'bad-port report',
    );

    child.stdin.end();
    const exitCode = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('helper did not exit after stdin closed')), 10000);
      timer.unref?.();
      child.once('exit', (code) => {
        clearTimeout(timer);
        resolve(code);
      });
    });
    assert.equal(exitCode, 0, stderr.join(''));
  } finally {
    cleanup();
  }
});