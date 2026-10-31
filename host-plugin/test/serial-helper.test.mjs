import test from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { PassThrough } from 'node:stream';
import { existsSync } from 'node:fs';
import { createSerialHelper, helperScriptPath } from '../lib/serial-helper.js';

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

class FakeChild extends EventEmitter {
  constructor(pid) {
    super();
    this.pid = pid;
    this.exitCode = null;
    this.chunks = [];
    this.ended = false;
    this.killed = false;
    this.stdin = {
      destroyed: false,
      writable: true,
      write: (chunk) => {
        this.chunks.push(String(chunk));
        return true;
      },
      end: () => {
        this.ended = true;
      },
      on: () => {},
    };
    this.stdout = new PassThrough();
  }

  kill() {
    this.killed = true;
    this.exit(0);
  }

  exit(code = 0) {
    if (this.exitCode !== null) return;
    this.exitCode = code;
    this.emit('exit', code, null);
  }

  feed(message) {
    this.stdout.write(JSON.stringify(message) + '\n');
  }

  messages() {
    return this.chunks
      .join('')
      .split('\n')
      .filter((line) => line.length > 0)
      .map((line) => JSON.parse(line));
  }
}

function harness(options = {}) {
  const children = [];
  const states = [];
  const deviceLines = [];
  const errors = [];
  const spawnImpl = () => {
    const child = new FakeChild(500 + children.length);
    children.push(child);
    return child;
  };
  const helper = createSerialHelper({
    port: 'COM7',
    baud: 9600,
    spawnImpl,
    startTimeoutMs: 1000,
    restartDelayMs: 10,
    stopGraceMs: 50,
    onState: ({ connected }) => states.push(connected),
    onLine: (line) => deviceLines.push(line),
    onError: (error) => errors.push(error),
    ...options,
  });
  return { helper, children, states, deviceLines, errors };
}

test('helper script path exists and is a file', () => {
  assert.equal(existsSync(helperScriptPath()), true);
  assert.equal(helperScriptPath().endsWith('serial-bridge.ps1'), true);
});

test('line protocol: ready -> open, state, write(base64), line(base64), error, ping', async () => {
  const { helper, children, states, deviceLines, errors } = harness();
  const started = helper.start();
  assert.equal(children.length, 1);
  assert.equal(helper.connected, false);
  assert.equal(helper.writeLine('before-ready'), false);

  children[0].feed({ op: 'ready' });
  await started;
  assert.equal(helper.ready, true);
  assert.deepEqual(children[0].messages()[0], { op: 'open', port: 'COM7', baud: 9600 });

  assert.equal(helper.writeLine('not connected'), false);
  children[0].feed({ op: 'state', connected: true });
  await sleep(5);
  assert.deepEqual(states, [true]);
  assert.equal(helper.connected, true);
  assert.equal(await helper.whenConnected(100), true);

  assert.equal(helper.writeLine('{"version":1,"event":"task.start"}'), true);
  const writeMessage = children[0].messages().at(-1);
  assert.equal(writeMessage.op, 'write');
  assert.equal(Buffer.from(writeMessage.data, 'base64').toString('utf8'), '{"version":1,"event":"task.start"}\n');

  children[0].feed({ op: 'line', data: Buffer.from('I (123) hello\n', 'utf8').toString('base64') });
  await sleep(5);
  assert.deepEqual(deviceLines, ['I (123) hello\n']);

  children[0].feed({ op: 'error', message: 'open COM7 failed' });
  await sleep(5);
  assert.equal(errors.length, 1);
  assert.match(errors[0].message, /open COM7 failed/);

  assert.equal(helper.ping(), true);
  assert.equal(children[0].messages().at(-1).op, 'ping');
  assert.equal(helper.closePort(), true);
  assert.equal(children[0].messages().at(-1).op, 'close');

  const stopping = helper.stop();
  children[0].exit(0);
  await stopping;
  assert.equal(children[0].ended, true);
  assert.equal(helper.connected, false);
});

test('unexpected exit restarts the helper after restartDelayMs', async () => {
  const { helper, children } = harness();
  const started = helper.start();
  children[0].feed({ op: 'ready' });
  await started;
  children[0].feed({ op: 'state', connected: true });
  await sleep(5);

  children[0].exit(1);
  await sleep(40);
  assert.equal(children.length, 2, 'a replacement helper process should be spawned');
  children[1].feed({ op: 'ready' });
  await sleep(10);
  assert.deepEqual(children[1].messages()[0], { op: 'open', port: 'COM7', baud: 9600 });

  const stopping = helper.stop();
  children[1].exit(0);
  await stopping;
});

test('start times out and rejects when the helper never becomes ready', async () => {
  const { helper, children } = harness({ startTimeoutMs: 20 });
  const started = helper.start();
  await assert.rejects(started, /timed out/);
  assert.equal(children[0].killed, true);
  const stopping = helper.stop();
  children[0].exit(0);
  await stopping;
});

test('whenConnected resolves false after dispose', async () => {
  const { helper, children } = harness();
  const started = helper.start();
  children[0].feed({ op: 'ready' });
  await started;
  const stopping = helper.stop();
  children[0].exit(0);
  await stopping;
  assert.equal(await helper.whenConnected(50), false);
});

test('non-JSON helper output is ignored', async () => {
  const { helper, children, deviceLines } = harness();
  const started = helper.start();
  children[0].feed({ op: 'ready' });
  await started;
  children[0].stdout.write('not json\n');
  children[0].feed({ op: 'line', data: Buffer.from('ok', 'utf8').toString('base64') });
  await sleep(10);
  assert.deepEqual(deviceLines, ['ok']);
  const stopping = helper.stop();
  children[0].exit(0);
  await stopping;
});