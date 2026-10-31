import test from 'node:test';
import assert from 'node:assert/strict';
import { apply, applyWith, normalizeConfig } from '../lib/index.js';

const NOW = 1_700_000_000_000;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function makeSession(id = 's1', options = {}) {
  const createdAt = options.createdAt ?? NOW - 5000;
  const cwd = options.cwd ?? 'C:\\work\\alpha';
  return { id, header: { cwd, createdAt }, cwd, createdAt };
}

function makeAgent(session) {
  return { id: `agent-${session.id}`, session };
}

function fakeCtx() {
  const handlers = new Map();
  const effects = [];
  const logs = [];
  return {
    handlers,
    effects,
    logs,
    on(name, listener) {
      handlers.set(name, listener);
      return () => handlers.delete(name);
    },
    effect(body) {
      const dispose = body();
      effects.push(dispose);
      return dispose;
    },
    logger: {
      info: (message) => logs.push(['info', message]),
      warn: (message) => logs.push(['warn', message]),
      debug: (message) => logs.push(['debug', message]),
    },
    fire(name, ...args) {
      const listener = handlers.get(name);
      if (!listener) throw new Error(`no listener registered for ${name}`);
      return listener(...args);
    },
  };
}

function fakeTransportFactory() {
  const records = { lines: [], starts: 0, stops: 0, factoryCalls: 0, options: null };
  const transport = {
    connected: true,
    async start() {
      records.starts += 1;
    },
    async stop() {
      records.stops += 1;
    },
    writeLine(line) {
      if (!transport.connected) return false;
      records.lines.push(line);
      return true;
    },
  };
  return {
    transport,
    records,
    factory(options) {
      records.factoryCalls += 1;
      records.options = options;
      return transport;
    },
  };
}

function messages(records) {
  return records.lines.map((line) => JSON.parse(line));
}

function lastMessage(records) {
  const parsed = messages(records);
  return parsed.length > 0 ? parsed.at(-1) : null;
}

async function setup(config = { port: 'COM7', heartbeatMs: 10_000 }) {
  const ctx = fakeCtx();
  const fake = fakeTransportFactory();
  const dispose = applyWith({
    ctx,
    config,
    now: () => NOW,
    transportFactory: fake.factory,
  });
  return { ctx, fake, records: fake.records, transport: fake.transport, dispose };
}

test('normalizeConfig: defaults, env fallback, auto rejection, warnings', () => {
  assert.deepEqual(normalizeConfig({}, {}), { port: '', baud: 115200, heartbeatMs: 10000, warnings: [] });
  assert.deepEqual(normalizeConfig({}, { ESP32_TOUCH_PORT: ' COM7 ' }), {
    port: 'COM7',
    baud: 115200,
    heartbeatMs: 10000,
    warnings: [],
  });
  assert.equal(normalizeConfig({ port: 'auto' }, {}).port, '');
  assert.equal(normalizeConfig({ port: 'auto' }, {}).warnings.length, 1);
  assert.equal(normalizeConfig({ baud: 0, heartbeatMs: -1 }, {}).baud, 115200);
  assert.equal(normalizeConfig({ heartbeatMs: 20000 }, {}).warnings.length, 1);
  assert.equal(normalizeConfig({ port: 'COM9', baud: 9600, heartbeatMs: 5000 }, {}).baud, 9600);
});

test('unconfigured port: loads, observes, never spawns a transport', async () => {
  const ctx = fakeCtx();
  let factoryCalls = 0;
  const dispose = applyWith({
    ctx,
    config: {},
    env: {},
    now: () => NOW,
    transportFactory: () => {
      factoryCalls += 1;
      throw new Error('must not be called without a port');
    },
  });
  const session = makeSession();
  ctx.fire('session/created', session);
  ctx.fire('agent/status', { agent: makeAgent(session), status: 'running' });
  await sleep(30);
  assert.equal(factoryCalls, 0);
  assert.equal(ctx.logs.some(([, message]) => message.includes('no serial port configured')), true);
  await dispose();
  assert.equal(factoryCalls, 0);
});

test('apply returns a disposer and wires all documented DSH events', async () => {
  const ctx = fakeCtx();
  const fake = fakeTransportFactory();
  const dispose = apply(ctx, { port: 'COM7' });
  // apply() cannot inject a fake factory, so verify the listener names only.
  for (const name of [
    'session/created',
    'session/disposed',
    'session/event',
    'agent/status',
    'agent/error',
    'user-questions/request',
  ]) {
    assert.equal(typeof ctx.handlers.get(name), 'function', `missing listener ${name}`);
  }
  assert.equal(typeof dispose, 'function');
  await dispose();
  assert.equal(fake.records.factoryCalls, 0);
});

test('events map to the C05 edge events with cards attached', async () => {
  const { ctx, records, dispose } = await setup();
  const session = makeSession();
  const agent = makeAgent(session);

  ctx.fire('session/created', session);
  await sleep(20);
  let message = lastMessage(records);
  assert.equal(message.event, 'device.heartbeat');
  assert.equal(message.version, 1);
  assert.equal(message.tasks.length, 1);
  assert.equal(message.title, 'alpha');
  assert.equal(message.project, 'alpha');
  assert.equal(message.agent, 'DSH');
  assert.equal(message.elapsed_s, 5);
  assert.equal(message.tasks[0].status, 'done');

  ctx.fire('agent/status', { agent, status: 'running' });
  await sleep(20);
  message = lastMessage(records);
  assert.equal(message.event, 'task.start');
  assert.equal(message.tasks[0].status, 'working');

  ctx.fire('session/event', session, { type: 'session/title', data: { title: '修复串口' } });
  await sleep(20);
  message = lastMessage(records);
  assert.equal(message.event, 'device.heartbeat');
  assert.equal(message.title, '修复串口');

  ctx.fire('session/event', session, { type: 'approval/asked', data: { id: 'a1', toolName: 'bash' } });
  await sleep(20);
  message = lastMessage(records);
  assert.equal(message.event, 'agent.input_required');
  assert.equal(message.tasks[0].status, 'input_required');

  ctx.fire('session/event', session, { type: 'approval/decided', data: { id: 'a1', outcome: 'allowed-once' } });
  await sleep(20);
  assert.equal(lastMessage(records).event, 'task.start');

  ctx.fire('agent/status', { agent, status: 'idle' });
  await sleep(20);
  assert.equal(lastMessage(records).event, 'agent.idle');

  ctx.fire('agent/error', { agent, turn: 1, step: 1, error: new Error('boom') });
  await sleep(20);
  message = lastMessage(records);
  assert.equal(message.event, 'task.error');
  assert.equal(message.tasks[0].status, 'failed');

  await dispose();
});

test('user-questions waterfall: observes, always calls next(), preserves the answer', async () => {
  const { ctx, records, dispose } = await setup();
  const session = makeSession();
  const request = { agent: makeAgent(session), questions: [], signal: new AbortController().signal };
  let nextCalls = 0;
  const answer = { answers: [{ id: 'q1', selected: ['yes'] }] };

  const result = ctx.fire('user-questions/request', request, () => {
    nextCalls += 1;
    return Promise.resolve(answer);
  });
  await sleep(20);
  assert.equal(nextCalls, 1);
  // The answer may resolve before the flush, so assert the edge was emitted.
  const events = messages(records).map((message) => message.event);
  assert.ok(events.includes('agent.input_required'), `events: ${events.join(',')}`);
  assert.equal(events.at(-1), 'agent.idle');
  assert.equal(await result, answer);
  await sleep(20);
  assert.equal(lastMessage(records).event, 'agent.idle');
  await dispose();
});

test('user-questions waterfall: next() rejection is propagated unchanged', async () => {
  const { ctx, records, dispose } = await setup();
  const session = makeSession();
  const failure = new Error('answerer failed');
  const request = { agent: makeAgent(session), questions: [] };
  const result = ctx.fire('user-questions/request', request, () => Promise.reject(failure));
  await assert.rejects(result, (error) => error === failure);
  await sleep(20);
  assert.equal(lastMessage(records).event, 'agent.idle');
  await dispose();
});

test('user-questions waterfall: abort clears the pending observation', async () => {
  const { ctx, records, dispose } = await setup();
  const session = makeSession();
  const controller = new AbortController();
  const request = { agent: makeAgent(session), questions: [], signal: controller.signal };
  ctx.fire('user-questions/request', request, () => new Promise(() => {}));
  await sleep(20);
  assert.equal(lastMessage(records).event, 'agent.input_required');
  controller.abort();
  await sleep(20);
  assert.equal(lastMessage(records).event, 'agent.idle');
  await dispose();
});

test('user-questions waterfall without next() is left untouched', async () => {
  const { ctx, dispose } = await setup();
  const request = { questions: [] };
  assert.equal(ctx.fire('user-questions/request', request, undefined), request);
  await dispose();
});

test('reconnect replays the current snapshot (level-triggered)', async () => {
  const { ctx, records, transport, dispose } = await setup();
  const session = makeSession();
  const agent = makeAgent(session);
  ctx.fire('session/created', session);
  ctx.fire('agent/status', { agent, status: 'running' });
  await sleep(30);
  const before = records.lines.length;
  transport.connected = false;
  records.options.onState({ connected: false });
  await sleep(10);
  transport.connected = true;
  records.options.onState({ connected: true });
  await sleep(20);
  assert.ok(records.lines.length > before);
  const message = lastMessage(records);
  assert.equal(message.event, 'task.start');
  assert.equal(message.tasks[0].status, 'working');
  await dispose();
});

test('heartbeat runs on the configured interval and stops on dispose', async () => {
  const { records, dispose } = await setup({ port: 'COM7', heartbeatMs: 15 });
  await sleep(70);
  const heartbeats = messages(records).filter((message) => message.event === 'device.heartbeat');
  assert.ok(heartbeats.length >= 2, `expected >=2 heartbeats, got ${heartbeats.length}`);
  assert.equal(records.starts, 1);
  await dispose();
  const count = records.lines.length;
  await sleep(50);
  assert.equal(records.lines.length, count);
  assert.equal(records.stops, 1);
});

test('transport receives port/baud and writes are serialized single-writer', async () => {
  const { ctx, records, dispose } = await setup();
  const session = makeSession();
  const agent = makeAgent(session);
  ctx.fire('session/created', session);
  ctx.fire('agent/status', { agent, status: 'running' });
  ctx.fire('agent/status', { agent, status: 'idle' });
  await sleep(40);
  assert.equal(records.options.port, 'COM7');
  assert.equal(records.options.baud, 115200);
  assert.equal(records.factoryCalls, 1);
  // Every emitted line is complete JSON and lines arrive in order.
  for (const line of records.lines) {
    assert.doesNotThrow(() => JSON.parse(line));
  }
  const events = messages(records).map((message) => message.event);
  assert.deepEqual(events.slice(-2), ['task.start', 'agent.idle']);
  await dispose();
});

test('dispose reclaims a transport that is still starting (bounded teardown)', async () => {
  const ctx = fakeCtx();
  let stopCalls = 0;
  let rejectStart;
  const transport = {
    connected: false,
    start: () => new Promise((resolve, reject) => {
      rejectStart = reject;
    }),
    stop: async () => {
      stopCalls += 1;
      rejectStart?.(new Error('stopped'));
    },
    writeLine: () => true,
  };
  const dispose = applyWith({
    ctx,
    config: { port: 'COM7', heartbeatMs: 10_000 },
    now: () => NOW,
    transportFactory: () => transport,
  });
  ctx.fire('agent/error', { agent: { id: 'ghost' }, turn: 1, step: 1, error: new Error('x') });
  await sleep(10);
  await dispose();
  assert.equal(stopCalls, 1);
});

test('after dispose no further emissions reach the transport', async () => {
  const { ctx, records, dispose } = await setup();
  await dispose();
  const session = makeSession();
  ctx.fire('session/created', session);
  ctx.fire('agent/status', { agent: makeAgent(session), status: 'running' });
  await sleep(30);
  assert.equal(records.lines.length, 0);
});