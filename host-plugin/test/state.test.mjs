import test from 'node:test';
import assert from 'node:assert/strict';
import {
  MODES,
  TRANSITION,
  createState,
  reduceEvent,
  reduceEvents,
  snapshotState,
} from '../lib/state.js';

const NOW = 1_700_000_000_000;

function makeSession(id, { cwd = 'C:\\work\\alpha', createdAt = NOW - 5000 } = {}) {
  return { id, header: { cwd, createdAt }, cwd, createdAt };
}

function makeAgent(session) {
  return { id: `agent-${session.id}`, session };
}

function first(events, now = NOW) {
  const state = createState();
  return events.map((event) => reduceEvent(state, event, now));
}

function transitionsOf(results) {
  return results.flatMap((result) => result.transitions);
}

test('empty state is IDLE with no cards', () => {
  const snapshot = snapshotState(createState(), NOW);
  assert.equal(snapshot.mode, MODES.IDLE);
  assert.deepEqual(snapshot.cards, []);
  assert.equal(snapshot.primary, null);
});

test('session/created only refreshes the card stack (no state animation)', () => {
  const [result] = first([{ type: 'session/created', session: makeSession('s1') }]);
  assert.deepEqual(result.transitions, [TRANSITION.CARD_REFRESH]);
  assert.equal(result.snapshot.mode, MODES.IDLE);
  assert.equal(result.snapshot.cards.length, 1);
  assert.deepEqual(result.snapshot.cards[0], {
    title: 'alpha',
    project: 'alpha',
    agent: 'DSH',
    elapsed_s: 5,
    status: 'done',
  });
});

test('session/title overrides the cwd fallback; no cwd falls back to session <id6>', () => {
  const session = makeSession('s1');
  const titleResults = first([
    { type: 'session/created', session },
    { type: 'session/event', session, event: { type: 'session/title', data: { title: '修复 ESP32 串口' } } },
  ]);
  assert.equal(titleResults[0].snapshot.cards[0].title, 'alpha');
  assert.equal(titleResults[1].snapshot.cards[0].title, '修复 ESP32 串口');

  const bare = first([{ type: 'session/created', session: makeSession('abcdefgh', { cwd: '' }) }]);
  assert.equal(bare[0].snapshot.cards[0].title, 'session abcdef');
  assert.equal(bare[0].snapshot.cards[0].project, '');
});

test('running -> WORKING/task.start and last idle -> agent.idle', () => {
  const session = makeSession('s1');
  const agent = makeAgent(session);
  const results = first([
    { type: 'session/created', session },
    { type: 'agent/status', agent, status: 'running' },
    { type: 'agent/status', agent, status: 'idle' },
  ]);
  assert.equal(results[1].snapshot.mode, MODES.WORKING);
  assert.deepEqual(results[1].transitions, [TRANSITION.START]);
  assert.equal(results[1].snapshot.cards[0].status, 'working');
  assert.deepEqual(results[2].transitions, [TRANSITION.IDLE]);
  assert.equal(results[2].snapshot.mode, MODES.IDLE);
  assert.equal(results[2].snapshot.cards[0].status, 'done');
});

test('two running sessions: agent.idle only after the last one stops', () => {
  const a = makeSession('s1', { createdAt: NOW - 1000 });
  const b = makeSession('s2', { createdAt: NOW - 2000 });
  const results = first([
    { type: 'session/created', session: a },
    { type: 'session/created', session: b },
    { type: 'agent/status', agent: makeAgent(a), status: 'running' },
    { type: 'agent/status', agent: makeAgent(b), status: 'running' },
    { type: 'agent/status', agent: makeAgent(a), status: 'idle' },
    { type: 'agent/status', agent: makeAgent(b), status: 'idle' },
  ]);
  assert.deepEqual(results[2].transitions, [TRANSITION.START]);
  assert.deepEqual(results[3].transitions, [TRANSITION.CARD_REFRESH]);
  assert.deepEqual(results[4].transitions, [TRANSITION.CARD_REFRESH]);
  assert.deepEqual(results[5].transitions, [TRANSITION.IDLE]);
});

test('approval/asked -> INPUT_REQUIRED, approval/decided -> resume', () => {
  const session = makeSession('s1');
  const agent = makeAgent(session);
  const results = first([
    { type: 'session/created', session },
    { type: 'agent/status', agent, status: 'running' },
    { type: 'session/event', session, event: { type: 'approval/asked', data: { id: 'a1', toolName: 'bash' } } },
    { type: 'session/event', session, event: { type: 'approval/decided', data: { id: 'a1', outcome: 'allowed-once' } } },
  ]);
  assert.deepEqual(results[2].transitions, [TRANSITION.INPUT_REQUIRED]);
  assert.equal(results[2].snapshot.mode, MODES.INPUT_REQUIRED);
  assert.equal(results[2].snapshot.cards[0].status, 'input_required');
  assert.equal(results[2].snapshot.sessions[0].pendingApproval, true);
  assert.deepEqual(results[3].transitions, [TRANSITION.START]);
  assert.equal(results[3].snapshot.mode, MODES.WORKING);
  assert.equal(results[3].snapshot.sessions[0].pendingApproval, false);
});

test('overlapping approvals stay pending until every id is decided', () => {
  const session = makeSession('s1');
  const results = first([
    { type: 'session/created', session },
    { type: 'session/event', session, event: { type: 'approval/asked', data: { id: 'a1' } } },
    { type: 'session/event', session, event: { type: 'approval/asked', data: { id: 'a2' } } },
    { type: 'session/event', session, event: { type: 'approval/decided', data: { id: 'a1' } } },
    { type: 'session/event', session, event: { type: 'approval/decided', data: { id: 'a2' } } },
  ]);
  assert.equal(results[2].snapshot.pending, true);
  assert.equal(results[3].snapshot.pending, true);
  assert.equal(results[4].snapshot.pending, false);
  assert.deepEqual(results[4].transitions, [TRANSITION.IDLE]);
});

test('user questions keep INPUT_REQUIRED until the request ends', () => {
  const session = makeSession('s1');
  const request = { agent: makeAgent(session), questions: [] };
  const results = first([
    { type: 'session/created', session },
    { type: 'user-questions/start', session, request },
    { type: 'user-questions/end', session, request },
  ]);
  assert.deepEqual(results[1].transitions, [TRANSITION.INPUT_REQUIRED]);
  assert.equal(results[1].snapshot.sessions[0].pendingQuestion, true);
  assert.deepEqual(results[2].transitions, [TRANSITION.IDLE]);
  assert.equal(results[2].snapshot.sessions[0].pendingQuestion, false);
});

test('agent/error -> task.error with a failed card; new work clears the failure', () => {
  const session = makeSession('s1');
  const agent = makeAgent(session);
  const results = first([
    { type: 'session/created', session },
    { type: 'agent/status', agent, status: 'running' },
    { type: 'agent/error', agent, error: new Error('boom') },
    { type: 'agent/status', agent, status: 'running' },
  ]);
  assert.deepEqual(results[2].transitions, [TRANSITION.ERROR]);
  assert.equal(results[2].snapshot.cards[0].status, 'failed');
  assert.equal(results[2].snapshot.sessions[0].errored, true);
  assert.equal(results[3].snapshot.sessions[0].errored, false);
  assert.equal(results[3].snapshot.cards[0].status, 'working');
});

test('cards sort by pending > running > newest and stop at three', () => {
  const old = makeSession('s1', { cwd: 'C:\\work\\s1', createdAt: NOW - 9000 });
  const newer = makeSession('s2', { cwd: 'C:\\work\\s2', createdAt: NOW - 3000 });
  const pending = makeSession('s3', { cwd: 'C:\\work\\s3', createdAt: NOW - 8000 });
  const newest = makeSession('s4', { cwd: 'C:\\work\\s4', createdAt: NOW - 1000 });
  const results = first([
    { type: 'session/created', session: old },
    { type: 'session/created', session: newer },
    { type: 'session/created', session: pending },
    { type: 'agent/status', agent: makeAgent(pending), status: 'running' },
    { type: 'session/event', session: pending, event: { type: 'approval/asked', data: { id: 'a1' } } },
    { type: 'session/created', session: newest },
    { type: 'agent/status', agent: makeAgent(newest), status: 'running' },
  ]);
  const snapshot = results.at(-1).snapshot;
  assert.deepEqual(snapshot.cards.map((card) => card.title), ['s3', 's4', 's2']);
  assert.equal(snapshot.cards[0].status, 'input_required');
  assert.equal(snapshot.cards[1].status, 'working');
  assert.equal(snapshot.cards.length, 3);
});

test('session/disposed removes the card and falls back to IDLE', () => {
  const session = makeSession('s1');
  const results = first([
    { type: 'session/created', session },
    { type: 'agent/status', agent: makeAgent(session), status: 'running' },
    { type: 'session/disposed', session },
  ]);
  assert.deepEqual(results[2].transitions, [TRANSITION.IDLE]);
  assert.equal(results[2].snapshot.mode, MODES.IDLE);
  assert.deepEqual(results[2].snapshot.cards, []);
});

test('elapsed_s is a non-negative integer clamped at zero', () => {
  const future = makeSession('s1', { createdAt: NOW + 60_000 });
  const [result] = first([{ type: 'session/created', session: future }]);
  assert.equal(result.snapshot.cards[0].elapsed_s, 0);
  const old = makeSession('s2', { createdAt: NOW - 125_500 });
  const [oldResult] = first([{ type: 'session/created', session: old }]);
  assert.equal(oldResult.snapshot.cards[0].elapsed_s, 125);
});

test('unknown observations are ignored', () => {
  const session = makeSession('s1');
  const results = first([
    { type: 'session/created', session },
    { type: 'session/event', session, event: { type: 'assistant/message', data: { text: 'private' } } },
    { type: 'something/else' },
  ]);
  assert.deepEqual(results[1].transitions, []);
  assert.deepEqual(results[2].transitions, []);
  assert.equal(results[1].snapshot.cards[0].title, 'alpha');
});

test('late session/event for a disposed session does not resurrect a card', () => {
  const session = makeSession('s1');
  const results = first([
    { type: 'session/created', session },
    { type: 'session/disposed', session },
    { type: 'session/event', session, event: { type: 'approval/decided', data: { id: 'a1' } } },
    { type: 'session/event', session, event: { type: 'session/title', data: { title: 'ghost' } } },
    { type: 'user-questions/start', session, request: { agent: makeAgent(session), questions: [] } },
  ]);
  assert.deepEqual(results[2].snapshot.cards, []);
  assert.deepEqual(results[3].snapshot.cards, []);
  assert.deepEqual(results[4].snapshot.cards, []);
  assert.deepEqual(results[4].transitions, []);
});

test('reduceEvents folds a list with a shared accumulator', () => {
  const results = reduceEvents(
    [
      { type: 'session/created', session: makeSession('s1') },
      { type: 'agent/status', agent: makeAgent(makeSession('s1')), status: 'running' },
    ],
    { now: NOW },
  );
  assert.equal(results.length, 2);
  assert.equal(results[1].snapshot.mode, MODES.WORKING);
});