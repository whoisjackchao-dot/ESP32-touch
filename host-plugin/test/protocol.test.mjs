import test from 'node:test';
import assert from 'node:assert/strict';
import {
  AGENT_MAX,
  AGENT_NAME,
  EVENTS,
  MAX_LINE_BYTES,
  MAX_TASKS,
  PROTOCOL_VERSION,
  PROJECT_MAX,
  ProtocolError,
  TITLE_MAX,
  encodeEvent,
  encodeHeartbeat,
  truncateChars,
} from '../lib/protocol.js';

const CJK = '修复';

function parse(line) {
  assert.equal(line.includes('\n'), false, 'a C05 line must not contain a literal newline');
  return JSON.parse(line);
}

function bytes(line) {
  return Buffer.byteLength(line, 'utf8');
}

function card(index, overrides = {}) {
  return {
    title: `title-${index}`,
    project: `project-${index}`,
    agent: AGENT_NAME,
    elapsed_s: index,
    status: 'working',
    ...overrides,
  };
}

test('envelope: version 1, known event, NDJSON-safe, no trailing newline', () => {
  const line = encodeEvent(EVENTS.TASK_START, { cards: [] });
  assert.equal(line.endsWith('\n'), false);
  const message = parse(line);
  assert.equal(message.version, PROTOCOL_VERSION);
  assert.equal(message.version, 1);
  assert.equal(message.event, 'task.start');
  assert.deepEqual(message.tasks, []);
  assert.equal('title' in message, false);
});

test('heartbeat uses device.heartbeat and can carry the card stack', () => {
  const message = parse(encodeHeartbeat({ cards: [card(1)] }));
  assert.equal(message.event, 'device.heartbeat');
  assert.equal(message.tasks.length, 1);
  assert.equal(message.tasks[0].title, 'title-1');
});

test('unknown or missing event names are rejected', () => {
  for (const bad of ['task.blocked', '', undefined, null, 42]) {
    assert.throws(() => encodeEvent(bad, { cards: [] }), (error) => error instanceof ProtocolError && error.code === 'EVENT');
  }
});

test('top-level single-card fields come from cards[0] and omit status', () => {
  const message = parse(encodeEvent(EVENTS.INPUT_REQUIRED, { cards: [card(2, { status: 'input_required' })] }));
  assert.equal(message.title, 'title-2');
  assert.equal(message.project, 'project-2');
  assert.equal(message.agent, 'DSH');
  assert.equal(message.elapsed_s, 2);
  assert.equal('status' in message, false);
  assert.equal(message.tasks[0].status, 'input_required');
});

test('title/project truncate at 63 code points and agent at 7', () => {
  // ASCII keeps the line under 512 bytes, so the per-field cap is observable.
  const long = 'a'.repeat(90);
  const message = parse(encodeEvent(EVENTS.TASK_START, { cards: [card(1, { title: long, project: long, agent: 'ABCDEFGHIJ' })] }));
  assert.equal(Array.from(message.title).length, TITLE_MAX);
  assert.equal(Array.from(message.project).length, PROJECT_MAX);
  assert.equal(message.agent, 'ABCDEFG');
  assert.equal(message.tasks[0].agent, 'ABCDEFG');
  assert.ok(bytes(JSON.stringify(message)) <= MAX_LINE_BYTES);
  // CJK (3 bytes/char) is additionally shrunk by the 512-byte line cap.
  const cjk = CJK.repeat(40);
  const cjkMessage = parse(encodeEvent(EVENTS.TASK_START, { cards: [card(1, { title: cjk, project: cjk })] }));
  assert.ok(Array.from(cjkMessage.title).length <= TITLE_MAX);
  assert.ok(Array.from(cjkMessage.project).length <= PROJECT_MAX);
});

test('truncation is by code point, never splitting a surrogate pair', () => {
  const emoji = '🐶'.repeat(70);
  assert.equal(truncateChars(emoji, 3), '🐶🐶🐶');
  const message = parse(encodeEvent(EVENTS.TASK_START, { cards: [card(1, { title: emoji })] }));
  // The line cap may shrink the title further than 63 code points, but every
  // surviving code point is a whole emoji (a split surrogate would not match).
  assert.ok(Array.from(message.title).length <= TITLE_MAX);
  assert.ok(Array.from(message.title).length > 0);
  assert.equal(Array.from(message.title).every((point) => point === '🐶'), true);
});

test('invalid top-card values reject the whole line', () => {
  assert.throws(
    () => encodeEvent(EVENTS.TASK_START, { cards: [card(1, { elapsed_s: -1 })] }),
    (error) => error instanceof ProtocolError && error.code === 'TYPE',
  );
  assert.throws(
    () => encodeEvent(EVENTS.TASK_START, { cards: [card(1, { elapsed_s: 1.5 })] }),
    (error) => error instanceof ProtocolError && error.code === 'TYPE',
  );
  assert.throws(
    () => encodeEvent(EVENTS.TASK_START, { cards: [card(1, { title: 7 })] }),
    (error) => error instanceof ProtocolError && error.code === 'TYPE',
  );
  assert.throws(() => encodeEvent(EVENTS.TASK_START, { cards: 'nope' }), (error) => error.code === 'TYPE');
});

test('tasks are capped at three and preserve top-first order', () => {
  const cards = [card(0), card(1), card(2), card(3), card(4)];
  const message = parse(encodeEvent(EVENTS.TASK_START, { cards }));
  assert.equal(message.tasks.length, MAX_TASKS);
  assert.deepEqual(message.tasks.map((task) => task.title), ['title-0', 'title-1', 'title-2']);
});

test('invalid task items are skipped, not fatal (C05 section 4)', () => {
  const cards = [
    card(0),
    { title: 42 },
    { project: 'missing title' },
    card(3),
    card(4),
  ];
  const message = parse(encodeEvent(EVENTS.TASK_START, { cards }));
  assert.deepEqual(message.tasks.map((task) => task.title), ['title-0', 'title-3', 'title-4']);
});

test('unknown task status defaults to working; empty title stays empty', () => {
  const message = parse(
    encodeEvent(EVENTS.TASK_START, {
      cards: [card(0, { title: '', status: 'mystery' })],
    }),
  );
  assert.equal(message.tasks[0].title, '');
  assert.equal(message.tasks[0].status, 'working');
});

test('three long CJK cards still fit the 512-byte line cap', () => {
  const long = CJK.repeat(40);
  const cards = [0, 1, 2].map((index) => card(index, { title: long, project: long }));
  const line = encodeEvent(EVENTS.TASK_START, { cards });
  assert.ok(bytes(line) <= MAX_LINE_BYTES, `line is ${bytes(line)} bytes`);
  const message = parse(line);
  assert.equal(message.tasks.length, 3);
  for (const task of message.tasks) {
    assert.ok(Array.from(task.title).length <= TITLE_MAX);
    assert.ok(Array.from(task.project).length <= PROJECT_MAX);
    assert.ok(bytes(JSON.stringify(task)) > 0);
  }
});

test('escaped control characters stay on one line', () => {
  const title = 'a"b\\c\n\tend';
  const line = encodeEvent(EVENTS.TASK_START, { cards: [card(0, { title })] });
  assert.equal(line.includes('\n'), false);
  const message = parse(line);
  assert.equal(message.title, title.slice(0, TITLE_MAX));
  assert.equal(message.tasks[0].title, title.slice(0, TITLE_MAX));
});

test('heartbeat without cards is still a valid C05 envelope', () => {
  const message = parse(encodeHeartbeat());
  assert.deepEqual(message, { version: 1, event: 'device.heartbeat', tasks: [] });
});