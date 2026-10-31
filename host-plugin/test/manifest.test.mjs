import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, existsSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const manifest = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const patch = readFileSync(join(root, 'cordis.patch.yml'), 'utf8');
const readme = readFileSync(join(root, 'README.md'), 'utf8');

test('package.json is a DSH Host bundle manifest', () => {
  assert.equal(manifest.name, 'dsh-esp32-touch-bridge');
  assert.match(manifest.version, /^\d+\.\d+\.\d+/);
  assert.equal(manifest.type, 'module');
  assert.equal(manifest.dsh?.bundle?.patch, './cordis.patch.yml');
  assert.equal(existsSync(join(root, 'cordis.patch.yml')), true);
  assert.ok(manifest.exports['.'], 'exports["."] is required');
  assert.equal(manifest.exports['./package.json'], './package.json');
  assert.equal(manifest.exports['./locale/*.json'], './locale/*.json');
  assert.equal(manifest.keywords.includes('dsh-plugin'), true);
  assert.match(manifest.license, /MIT/);
  assert.match(manifest.engines.node, />=20/);
  assert.equal('bin' in manifest, false, 'the internal helper must not be a bin');
  assert.equal('dependencies' in manifest, false, 'no third-party dependencies');
  assert.equal('devDependencies' in manifest, false);
  assert.equal('scripts' in manifest, false, 'no build step');
});

test('cordis.patch.yml inserts the frozen row id/name with config', () => {
  assert.match(patch, /^\s*- insert:/m);
  assert.match(patch, /id: esp32-touch-bridge/);
  assert.match(patch, /name: dsh-esp32-touch-bridge/);
  assert.match(patch, /baud:\s*115200/);
  assert.match(patch, /heartbeatMs:\s*10000/);
  assert.match(patch, /# port: COM7/);
});

test('display metadata: locale files and icon exist in the documented shape', () => {
  const en = JSON.parse(readFileSync(join(root, 'locale', 'en.json'), 'utf8'));
  const zh = JSON.parse(readFileSync(join(root, 'locale', 'zh.json'), 'utf8'));
  for (const locale of [en, zh]) {
    assert.equal(typeof locale.meta.title, 'string');
    assert.ok(locale.meta.title.length > 0);
    assert.equal(typeof locale.meta.description, 'string');
    assert.ok(locale.meta.description.length > 0);
  }
  assert.notEqual(en.meta.title, zh.meta.title);
  const iconPath = join(root, manifest.icon);
  assert.equal(existsSync(iconPath), true);
  const icon = readFileSync(iconPath, 'utf8');
  assert.match(icon, /<svg[\s>]/);
  assert.ok(statSync(iconPath).size <= 256 * 1024);
});

test('shipped files are declared and the helper script is present', () => {
  for (const entry of ['lib', 'helper', 'locale', 'icon.svg', 'README.md', 'LICENSE']) {
    assert.ok(manifest.files.includes(entry), `files[] must include ${entry}`);
  }
  assert.equal(existsSync(join(root, 'helper', 'serial-bridge.ps1')), true);
  assert.equal(existsSync(join(root, 'lib', 'index.js')), true);
  assert.equal(existsSync(join(root, 'LICENSE')), true);
});

test('README declares the internal helper, privacy, limits, install and uninstall', () => {
  for (const needle of [
    '内部辅助进程',
    'serial-bridge.ps1',
    'PowerShell 5.1',
    'dsh plugin --profile',
    'ESP32_TOUCH_PORT',
    'user-questions/request',
    '隐私与安全',
    '已知限制',
    'node --test',
    'e2e-device.mjs',
    'remove dsh-esp32-touch-bridge',
  ]) {
    assert.ok(readme.includes(needle), `README must mention ${needle}`);
  }
  assert.match(readme, /没有\*\* `bin`/);
});

test('module exports the Cordis entry points', async () => {
  const module = await import('../lib/index.js');
  assert.equal(module.name, 'esp32-touch-bridge');
  assert.equal(typeof module.apply, 'function');
  assert.equal(typeof module.applyWith, 'function');
  assert.equal(typeof module.normalizeConfig, 'function');
});