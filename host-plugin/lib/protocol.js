/**
 * C05 v1 NDJSON encoder — pure, no I/O, no device knowledge beyond the frozen
 * contract (docs/contracts/C05-event-protocol.md, 2026-10-31).
 *
 * Every emitted line is one JSON object (no nested arrays/objects other than
 * `tasks`), ends with `\n` at the serial layer, is UTF-8 and fits in 512 bytes.
 * Values are truncated by Unicode code point (C05 section 2/4), never by bytes,
 * so a truncated title never splits a character.
 */

export const PROTOCOL_VERSION = 1;
export const MAX_LINE_BYTES = 512;
export const MAX_TASKS = 3;
export const TITLE_MAX = 63;
export const PROJECT_MAX = 63;
export const AGENT_MAX = 7;
export const AGENT_NAME = 'DSH';

/** C05 section 3 event names. Unknown names are rejected, never encoded. */
export const EVENTS = Object.freeze({
  TASK_START: 'task.start',
  INPUT_REQUIRED: 'agent.input_required',
  TASK_DONE: 'task.done',
  TASK_ERROR: 'task.error',
  AGENT_IDLE: 'agent.idle',
  HEARTBEAT: 'device.heartbeat',
});

export const CARD_STATUSES = Object.freeze(['working', 'input_required', 'done', 'failed']);

const EVENT_SET = new Set(Object.values(EVENTS));
const STATUS_SET = new Set(CARD_STATUSES);

/** A value the plugin refuses to encode (whole line dropped by the caller). */
export class ProtocolError extends Error {
  constructor(code, message) {
    super(message);
    this.name = 'ProtocolError';
    this.code = code;
  }
}

function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

/** Truncate to at most `max` Unicode code points (surrogate pairs stay intact). */
export function truncateChars(value, max) {
  if (typeof value !== 'string') return value;
  const points = Array.from(value);
  return points.length <= max ? value : points.slice(0, max).join('');
}

function textField(value, max, name) {
  if (typeof value !== 'string') {
    throw new ProtocolError('TYPE', `${name} must be a string`);
  }
  return truncateChars(value, max);
}

function elapsedField(value, name) {
  if (!Number.isInteger(value) || value < 0) {
    throw new ProtocolError('TYPE', `${name} must be a non-negative integer`);
  }
  return value;
}

function normalizeTask(card) {
  // C05 section 4: an item with a wrong-typed field is ignored, not fatal;
  // `title` is required (an empty string is legal and renders "(untitled)").
  if (!isPlainObject(card) || typeof card.title !== 'string') return null;
  const task = { title: truncateChars(card.title, TITLE_MAX) };
  if (card.project !== undefined) {
    if (typeof card.project !== 'string') return null;
    task.project = truncateChars(card.project, PROJECT_MAX);
  } else {
    task.project = '';
  }
  if (card.elapsed_s !== undefined) {
    if (!Number.isInteger(card.elapsed_s) || card.elapsed_s < 0) return null;
    task.elapsed_s = card.elapsed_s;
  } else {
    task.elapsed_s = 0;
  }
  if (card.status !== undefined) {
    if (typeof card.status !== 'string') return null;
    task.status = STATUS_SET.has(card.status) ? card.status : 'working';
  } else {
    task.status = 'working';
  }
  if (card.agent !== undefined) {
    if (typeof card.agent !== 'string') return null;
    task.agent = truncateChars(card.agent, AGENT_MAX);
  } else {
    task.agent = AGENT_NAME;
  }
  return task;
}

function longestStringRef(refs) {
  let best = null;
  let bestBytes = 0;
  for (const ref of refs) {
    const value = ref.get();
    if (typeof value !== 'string' || value.length === 0) continue;
    const bytes = Buffer.byteLength(value, 'utf8');
    if (bytes > bestBytes) {
      best = ref;
      bestBytes = bytes;
    }
  }
  return best;
}

function stringRefs(line) {
  const refs = [];
  const add = (holder, key) => {
    refs.push({ get: () => holder[key], set: (value) => { holder[key] = value; } });
  };
  for (const key of ['title', 'project', 'agent']) add(line, key);
  for (const task of line.tasks) {
    add(task, 'title');
    add(task, 'project');
    add(task, 'agent');
  }
  return refs;
}

/**
 * Guarantee C05's 512-byte line cap without splitting characters: shrink the
 * longest string one code point at a time, then drop trailing cards, and only
 * then report an error (which the caller logs and drops).
 */
function fitLine(line) {
  let json = JSON.stringify(line);
  if (Buffer.byteLength(json, 'utf8') <= MAX_LINE_BYTES) return json;
  const refs = stringRefs(line);
  for (let guard = 0; guard < 8192; guard += 1) {
    const ref = longestStringRef(refs);
    if (!ref) {
      if (line.tasks.length > 0) {
        line.tasks.pop();
        json = JSON.stringify(line);
        continue;
      }
      throw new ProtocolError('TOO_LONG', `C05 line exceeds ${MAX_LINE_BYTES} bytes`);
    }
    const points = Array.from(ref.get());
    ref.set(points.slice(0, -1).join(''));
    json = JSON.stringify(line);
    if (Buffer.byteLength(json, 'utf8') <= MAX_LINE_BYTES) return json;
  }
  throw new ProtocolError('TOO_LONG', `C05 line exceeds ${MAX_LINE_BYTES} bytes`);
}

/**
 * Encode one C05 event line (without the trailing newline).
 *
 * @param event - one of {@link EVENTS}; anything else throws `EVENT`.
 * @param snapshot - `{cards: [...]}` as produced by state.js (a `tasks` array
 *   is accepted as a fallback). The first card becomes the top-level single
 *   card (title/project/agent/elapsed_s); up to three become `tasks`.
 * @returns the JSON line, guaranteed <= 512 UTF-8 bytes.
 * @throws {ProtocolError} on an unknown event or invalid top-card value —
 *   callers MUST drop that emission instead of writing a malformed line.
 */
export function encodeEvent(event, snapshot = {}) {
  if (!EVENT_SET.has(event)) {
    throw new ProtocolError('EVENT', `unknown C05 event: ${String(event)}`);
  }
  const source = snapshot ?? {};
  const cards = source.cards ?? source.tasks ?? [];
  if (!Array.isArray(cards)) {
    throw new ProtocolError('TYPE', 'cards must be an array');
  }
  const line = { version: PROTOCOL_VERSION, event };
  if (cards.length > 0) {
    const top = cards[0];
    if (!isPlainObject(top)) throw new ProtocolError('TYPE', 'card must be an object');
    line.title = textField(top.title ?? '', TITLE_MAX, 'title');
    line.project = textField(top.project ?? '', PROJECT_MAX, 'project');
    line.agent = textField(top.agent ?? AGENT_NAME, AGENT_MAX, 'agent');
    line.elapsed_s = elapsedField(top.elapsed_s ?? 0, 'elapsed_s');
  }
  const tasks = [];
  for (const card of cards) {
    if (tasks.length >= MAX_TASKS) break;
    const task = normalizeTask(card);
    if (task !== null) tasks.push(task);
  }
  line.tasks = tasks;
  return fitLine(line);
}

/** Encode the 10 s liveness line; carries the same card stack when present. */
export function encodeHeartbeat(snapshot = {}) {
  return encodeEvent(EVENTS.HEARTBEAT, snapshot);
}