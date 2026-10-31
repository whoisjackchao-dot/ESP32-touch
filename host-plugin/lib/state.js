/**
 * Pure host-side reduction: DSH session/agent observations -> the C05 model.
 *
 * This module performs no I/O, starts no timers and spawns nothing. `now` is
 * injected so every fold is deterministic and testable without a device.
 *
 * Target mode (docs/12-dsh-plugin.md section 3.1):
 *   any pending approval/question -> INPUT_REQUIRED
 *   else any running session     -> WORKING
 *   else                          -> IDLE
 *
 * Cards are sorted by "has pending > running > most recently created" and cut
 * to three (C05 section 4); the top card also supplies the single-card fields.
 */

export const MODES = Object.freeze({
  IDLE: 'IDLE',
  WORKING: 'WORKING',
  INPUT_REQUIRED: 'INPUT_REQUIRED',
});

export const CARD_STATUS = Object.freeze({
  WORKING: 'working',
  INPUT_REQUIRED: 'input_required',
  DONE: 'done',
  FAILED: 'failed',
});

/** C03 section 7 R2: the fixed agent prefix rendered on the device. */
export const AGENT_PREFIX = 'DSH';

/**
 * Semantic transitions a fold may report. Their values are the C05 event names
 * the transport sends; protocol.js validates them independently.
 */
export const TRANSITION = Object.freeze({
  START: 'task.start',
  INPUT_REQUIRED: 'agent.input_required',
  IDLE: 'agent.idle',
  ERROR: 'task.error',
  CARD_REFRESH: 'device.heartbeat',
});

const MAX_CARDS = 3;
const MAX_REMEMBERED_DISPOSALS = 200;

/** Create the mutable accumulator used by {@link reduceEvent}. */
export function createState() {
  return { sessions: new Map(), disposed: new Set() };
}

function rememberDisposal(state, id) {
  if (!(state.disposed instanceof Set)) return;
  state.disposed.add(id);
  while (state.disposed.size > MAX_REMEMBERED_DISPOSALS) {
    state.disposed.delete(state.disposed.values().next().value);
  }
}

function idOf(session) {
  if (session === undefined || session === null) return undefined;
  if (typeof session === 'string' || typeof session === 'number') return String(session);
  const id = session.id;
  return id === undefined || id === null ? undefined : String(id);
}

function finiteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function projectName(cwd) {
  if (typeof cwd !== 'string' || cwd.length === 0) return '';
  const trimmed = cwd.replace(/[\\/]+$/, '');
  const slash = Math.max(trimmed.lastIndexOf('/'), trimmed.lastIndexOf('\\'));
  return slash >= 0 ? trimmed.slice(slash + 1) : trimmed;
}

function ensureSession(state, session) {
  const id = idOf(session);
  if (id === undefined) return undefined;
  // A pending approval/question abort can be delivered after disposal; never
  // resurrect a card for an id we have already seen disposed.
  if (state.disposed instanceof Set && state.disposed.has(id)) return undefined;
  let entry = state.sessions.get(id);
  if (entry) {
    const header = session && typeof session === 'object' ? session.header : undefined;
    if (entry.cwd === '' && header && typeof header.cwd === 'string') entry.cwd = header.cwd;
    if (entry.createdAt === 0 && header && finiteNumber(header.createdAt)) entry.createdAt = header.createdAt;
    return entry;
  }
  const header = session && typeof session === 'object' ? session.header : undefined;
  entry = {
    id,
    cwd: typeof header?.cwd === 'string' ? header.cwd : (typeof session?.cwd === 'string' ? session.cwd : ''),
    createdAt: finiteNumber(header?.createdAt) ? header.createdAt : (finiteNumber(session?.createdAt) ? session.createdAt : 0),
    title: null,
    running: false,
    pendingApprovals: new Set(),
    pendingQuestions: new Set(),
    errored: false,
  };
  state.sessions.set(id, entry);
  return entry;
}

function sessionForAgent(state, agent) {
  if (!agent || typeof agent !== 'object') return undefined;
  const session = agent.session;
  const id = idOf(session) ?? idOf(agent);
  if (id === undefined) return undefined;
  if (!state.sessions.has(id)) {
    return ensureSession(state, { id, header: session && typeof session === 'object' ? session.header : undefined });
  }
  return state.sessions.get(id);
}

function applySessionEvent(state, session, event) {
  // DSH announces `session/created` before any `session/event`, so an event
  // for an unknown session is a late replay (e.g. a decision after disposal).
  // Ignoring it avoids resurrecting a ghost card that would never be disposed.
  const id = idOf(session);
  if (id === undefined) return false;
  const entry = state.sessions.get(id);
  if (!entry || !event || typeof event !== 'object') return false;
  const data = event.data ?? {};
  switch (event.type) {
    case 'session/title': {
      if (typeof data.title !== 'string' || data.title.length === 0) return false;
      if (entry.title === data.title) return false;
      entry.title = data.title;
      return true;
    }
    case 'approval/asked': {
      const key = data.id === undefined ? '?' : String(data.id);
      if (entry.pendingApprovals.has(key)) return false;
      entry.pendingApprovals.add(key);
      return true;
    }
    case 'approval/decided': {
      const key = data.id === undefined ? '?' : String(data.id);
      return entry.pendingApprovals.delete(key);
    }
    default:
      return false;
  }
}

function applyAgentStatus(state, agent, status) {
  const entry = sessionForAgent(state, agent);
  if (!entry) return false;
  const running = status === 'running';
  let changed = false;
  if (entry.running !== running) {
    entry.running = running;
    changed = true;
  }
  // New work clears a previous FAILED card (the same convention as the
  // StickS3 host bridge).
  if (running && entry.errored) {
    entry.errored = false;
    changed = true;
  }
  return changed;
}

function applyAgentError(state, agent) {
  const entry = sessionForAgent(state, agent);
  if (!entry) return false;
  if (entry.errored) return false;
  entry.errored = true;
  return true;
}

function applyQuestion(state, session, request, pending) {
  const entry = ensureSession(state, session);
  if (!entry) return false;
  const key = request !== null && typeof request === 'object' ? request : '?';
  const had = entry.pendingQuestions.has(key);
  if (pending && !had) {
    entry.pendingQuestions.add(key);
    return true;
  }
  if (!pending && had) {
    entry.pendingQuestions.delete(key);
    return true;
  }
  return false;
}

function applyOne(state, event) {
  switch (event?.type) {
    case 'session/created': {
      const id = idOf(event.session);
      if (state.disposed instanceof Set && id !== undefined) state.disposed.delete(id);
      return ensureSession(state, event.session) !== undefined;
    }
    case 'session/disposed': {
      const id = idOf(event.session);
      if (id === undefined) return false;
      rememberDisposal(state, id);
      return state.sessions.delete(id);
    }
    case 'session/event':
      return applySessionEvent(state, event.session, event.event);
    case 'agent/status':
      return applyAgentStatus(state, event.agent, event.status);
    case 'agent/error':
      return applyAgentError(state, event.agent);
    case 'user-questions/start':
      return applyQuestion(state, event.session ?? event.request?.agent?.session, event.request, true);
    case 'user-questions/end':
      return applyQuestion(state, event.session ?? event.request?.agent?.session, event.request, false);
    default:
      return false;
  }
}

function statusOf(entry) {
  if (entry.pendingApprovals.size > 0 || entry.pendingQuestions.size > 0) return CARD_STATUS.INPUT_REQUIRED;
  if (entry.errored) return CARD_STATUS.FAILED;
  if (entry.running) return CARD_STATUS.WORKING;
  return CARD_STATUS.DONE;
}

function compareSessions(a, b) {
  if (a.pending !== b.pending) return a.pending ? -1 : 1;
  if (a.running !== b.running) return a.running ? -1 : 1;
  if (a.createdAt !== b.createdAt) return b.createdAt - a.createdAt;
  return a.id < b.id ? -1 : a.id > b.id ? 1 : 0;
}

/**
 * Build an immutable-ish snapshot of the accumulator at `at` milliseconds.
 * `sessions` is sorted for the card stack; `cards` is already cut to three.
 */
export function snapshotState(state, at = Date.now()) {
  const sessions = [];
  for (const entry of state.sessions.values()) {
    const pending = entry.pendingApprovals.size > 0 || entry.pendingQuestions.size > 0;
    const elapsed_s = entry.createdAt > 0 ? Math.max(0, Math.floor((at - entry.createdAt) / 1000)) : 0;
    const project = projectName(entry.cwd);
    const title = typeof entry.title === 'string' && entry.title.length > 0
      ? entry.title
      : project || `session ${entry.id.slice(0, 6)}`;
    sessions.push({
      id: entry.id,
      title,
      project,
      cwd: entry.cwd,
      createdAt: entry.createdAt,
      elapsed_s,
      running: entry.running,
      pending,
      pendingApproval: entry.pendingApprovals.size > 0,
      pendingQuestion: entry.pendingQuestions.size > 0,
      errored: entry.errored,
      status: statusOf(entry),
    });
  }
  sessions.sort(compareSessions);
  const cards = sessions.slice(0, MAX_CARDS).map((s) => ({
    title: s.title,
    project: s.project,
    agent: AGENT_PREFIX,
    elapsed_s: s.elapsed_s,
    status: s.status,
  }));
  const pending = sessions.some((s) => s.pending);
  const working = sessions.some((s) => s.running);
  return {
    mode: pending ? MODES.INPUT_REQUIRED : working ? MODES.WORKING : MODES.IDLE,
    pending,
    working,
    sessions,
    cards,
    primary: cards.length > 0 ? cards[0] : null,
  };
}

function modeTransitions(before, after) {
  const out = [];
  const prevPending = before.mode === MODES.INPUT_REQUIRED;
  const nextPending = after.mode === MODES.INPUT_REQUIRED;
  if (!prevPending && nextPending) {
    out.push(TRANSITION.INPUT_REQUIRED);
  } else if (prevPending && !nextPending) {
    if (after.mode === MODES.WORKING) out.push(TRANSITION.START);
    else if (after.mode === MODES.IDLE) out.push(TRANSITION.IDLE);
  }
  if (!prevPending && !nextPending) {
    if (before.mode === MODES.IDLE && after.mode === MODES.WORKING) out.push(TRANSITION.START);
    if (before.mode !== MODES.IDLE && after.mode === MODES.IDLE) out.push(TRANSITION.IDLE);
  }
  return out;
}

/**
 * Fold one DSH observation into `state`.
 *
 * @param state - accumulator from {@link createState}.
 * @param event - `{type: 'session/created'|'session/disposed'|'session/event'|
 *   'agent/status'|'agent/error'|'user-questions/start'|'user-questions/end', ...}`
 * @param now - epoch ms or a `() => epoch ms` injected for tests.
 * @returns `{snapshot, transitions, cardChanged}` where `transitions` holds C05
 *   event names to emit; an otherwise quiet card change reports
 *   `device.heartbeat` so the card stack still refreshes within one round trip.
 */
export function reduceEvent(state, event, now = Date.now()) {
  const at = typeof now === 'function' ? now() : now;
  const before = snapshotState(state, at);
  const changed = applyOne(state, event);
  const after = snapshotState(state, at);
  const transitions = [];
  if (event?.type === 'agent/error') transitions.push(TRANSITION.ERROR);
  transitions.push(...modeTransitions(before, after));
  const cardChanged = JSON.stringify(before.cards) !== JSON.stringify(after.cards);
  if (transitions.length === 0 && cardChanged) transitions.push(TRANSITION.CARD_REFRESH);
  return { changed: changed || transitions.length > 0 || cardChanged, snapshot: after, transitions, cardChanged };
}

/** Convenience for tests and fixtures: fold a list, returning every result. */
export function reduceEvents(events, options = {}) {
  const { state = createState(), now = Date.now() } = options;
  return events.map((event) => reduceEvent(state, event, now));
}