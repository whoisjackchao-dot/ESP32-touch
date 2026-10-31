/**
 * DSH (Cordis) host plugin entry: dsh-esp32-touch-bridge.
 *
 * Observes ONLY public DSH events and mirrors them to an ESP32-S3 desktop pet
 * as C05 v1 NDJSON lines over a local serial port:
 *   session/created, session/disposed, session/event (session/title,
 *   approval/asked, approval/decided), agent/status, agent/error, and the
 *   user-questions/request waterfall.
 *
 * The waterfall listener observes the pending question and always delegates
 * with next(); it never answers, rewrites or delays an approval.
 *
 * Without `config.port` (or ESP32_TOUCH_PORT) the plugin loads but spawns no
 * helper and opens no serial port. The transport itself is created lazily on
 * the first write, so installation alone has no side effects.
 *
 * Layering: state.js reduces, protocol.js encodes, serial-helper.js transports.
 * `applyWith` injects all three seams for `node --test`.
 */
import { MODES, createState, reduceEvent, snapshotState } from './state.js';
import { EVENTS, encodeEvent } from './protocol.js';
import { createSerialHelper } from './serial-helper.js';

export const name = 'esp32-touch-bridge';

/** Cordis entry point (bundle row: id=esp32-touch-bridge, name=dsh-esp32-touch-bridge). */
export function apply(ctx, config) {
  return applyWith({ ctx, config });
}

function positiveInteger(value, fallback) {
  return Number.isInteger(value) && value > 0 ? value : fallback;
}

/**
 * Resolve the row config, falling back to ESP32_TOUCH_PORT for the port.
 * `auto` is deliberately unsupported in v1 (docs/12 section 5): scanning COMx
 * reliably is not possible from Node, so the user configures a port.
 */
export function normalizeConfig(raw = {}, env = process.env) {
  const source = raw !== null && typeof raw === 'object' ? raw : {};
  const rawPort = source.port ?? env?.ESP32_TOUCH_PORT;
  let port = typeof rawPort === 'string' ? rawPort.trim() : '';
  const warnings = [];
  if (port.toLowerCase() === 'auto') {
    port = '';
    warnings.push('config.port "auto" is not supported in v1; set an explicit COMx (the plugin stays device-less)');
  }
  const baud = positiveInteger(source.baud, 115200);
  const heartbeatMs = positiveInteger(source.heartbeatMs, 10000);
  if (heartbeatMs >= 15000) {
    warnings.push(`config.heartbeatMs=${heartbeatMs} is >= the device OFFLINE timeout (15 s); use 10000 (C05 section 3)`);
  }
  return { port, baud, heartbeatMs, warnings };
}

function makeLogger(ctx, injected) {
  const forward = (level, message) => {
    if (typeof injected === 'function') {
      try {
        injected(level, message);
      } catch {
        // never let logging break the bridge
      }
      return;
    }
    const logger = ctx?.logger;
    const fn = logger && typeof logger[level] === 'function' ? logger[level] : undefined;
    if (fn) {
      try {
        fn.call(logger, message);
      } catch {
        // ignore
      }
      return;
    }
    if (level === 'warn' || level === 'error') {
      try {
        console.warn(message);
      } catch {
        // ignore
      }
    }
  };
  return {
    info: (message) => forward('info', message),
    warn: (message) => forward('warn', message),
    debug: (message) => forward('debug', message),
  };
}

/** Default transport factory; entirely replaceable for tests. */
export function defaultTransportFactory(options = {}) {
  return createSerialHelper(options);
}

function resyncEventFor(mode) {
  if (mode === MODES.INPUT_REQUIRED) return EVENTS.INPUT_REQUIRED;
  if (mode === MODES.WORKING) return EVENTS.TASK_START;
  return EVENTS.AGENT_IDLE;
}

/**
 * Testable wiring: `ctx` is any object with `on` (and optionally `effect` /
 * `logger`); `transportFactory` returns something with `start()`, `stop()`,
 * `writeLine(line)`, plus the callback options it was handed.
 */
export function applyWith({
  ctx,
  config = {},
  env = process.env,
  now = () => Date.now(),
  transportFactory = defaultTransportFactory,
  logger,
} = {}) {
  const cfg = normalizeConfig(config, env);
  const log = makeLogger(ctx, logger);
  const state = createState();

  let stopped = false;
  let helper = null;
  let heartbeatTimer = null;
  let chain = Promise.resolve();

  for (const warning of cfg.warnings) log.warn(`[${name}] ${warning}`);

  const emit = (eventName, snapshot) => {
    // Without a configured port the plugin only observes; it must never create
    // a helper process or attempt a write (docs/12 section 5 zero side effect).
    if (stopped || !cfg.port) return;
    let line;
    try {
      line = encodeEvent(eventName, snapshot);
    } catch (err) {
      log.warn(`[${name}] dropped ${eventName}: ${err?.message ?? err}`);
      return;
    }
    chain = chain.then(async () => {
      if (stopped) return;
      try {
        if (!helper) {
          const transport = transportFactory({
            port: cfg.port,
            baud: cfg.baud,
            onLine: (text) => log.info(`[${name}] device log: ${String(text).trimEnd()}`),
            onState: ({ connected }) => {
              log.info(`[${name}] serial ${connected ? 'connected' : 'disconnected'}`);
              // Level-triggered: the helper may have missed writes while the
              // port was down, so replay the current snapshot after reconnect.
              if (connected && !stopped) resync('serial connected');
            },
            onError: (err) => log.warn(`[${name}] serial helper: ${err?.message ?? err}`),
            log: (message) => log.debug(message),
          });
          // Store before awaiting start so dispose() can reclaim a helper that
          // is still starting instead of waiting for its start timeout.
          helper = transport;
          try {
            await transport.start();
          } catch (err) {
            if (helper === transport) helper = null;
            throw err;
          }
        }
        const written = typeof helper.writeLine === 'function' && helper.writeLine(line) === true;
        if (!written) log.debug(`[${name}] serial not connected; line dropped, snapshot will be replayed on connect`);
      } catch (err) {
        log.warn(`[${name}] serial write failed: ${err?.message ?? err}`);
      }
    });
  };

  const resync = (reason) => {
    const snapshot = snapshotState(state, now());
    const eventName = resyncEventFor(snapshot.mode);
    log.debug(`[${name}] replay snapshot (${reason}) -> ${eventName}`);
    emit(eventName, snapshot);
  };

  const handle = (observation) => {
    if (stopped) return;
    let result;
    try {
      result = reduceEvent(state, observation, now);
    } catch (err) {
      log.warn(`[${name}] state reduction failed: ${err?.message ?? err}`);
      return;
    }
    for (const eventName of result.transitions) emit(eventName, result.snapshot);
  };

  const on = (eventName, listener) => {
    if (!ctx || typeof ctx.on !== 'function') return;
    ctx.on(eventName, listener);
  };

  on('session/created', (session) => handle({ type: 'session/created', session }));
  on('session/disposed', (session) => handle({ type: 'session/disposed', session }));
  on('session/event', (session, event) => handle({ type: 'session/event', session, event }));
  on('agent/status', (payload) => handle({ type: 'agent/status', agent: payload?.agent, status: payload?.status }));
  on('agent/error', (payload) => handle({ type: 'agent/error', agent: payload?.agent, error: payload?.error }));

  // user-questions/request is a waterfall: observe only, then delegate with
  // next(). Never answer, mutate or short-circuit the request.
  on('user-questions/request', (request, next) => {
    if (typeof next !== 'function') {
      log.warn(`[${name}] user-questions/request called without next(); leaving the request untouched`);
      return request;
    }
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      const signal = request?.signal;
      if (signal && typeof signal.removeEventListener === 'function') {
        try {
          signal.removeEventListener('abort', finish);
        } catch {
          // ignore
        }
      }
      handle({ type: 'user-questions/end', session: request?.agent?.session, request });
    };
    try {
      handle({ type: 'user-questions/start', session: request?.agent?.session, request });
      const signal = request?.signal;
      if (signal && typeof signal.addEventListener === 'function') {
        if (signal.aborted === true) finish();
        else signal.addEventListener('abort', finish, { once: true });
      }
    } catch (err) {
      log.warn(`[${name}] observing user question failed: ${err?.message ?? err}`);
    }
    let result;
    try {
      result = next();
    } catch (err) {
      finish();
      throw err;
    }
    if (result && typeof result.then === 'function') {
      return result.then(
        (value) => {
          finish();
          return value;
        },
        (error) => {
          finish();
          throw error;
        },
      );
    }
    finish();
    return result;
  });

  if (cfg.port) {
    log.info(`[${name}] enabled for ${cfg.port} @ ${cfg.baud} baud, heartbeat ${cfg.heartbeatMs} ms`);
    heartbeatTimer = setInterval(() => {
      if (stopped) return;
      emit(EVENTS.HEARTBEAT, snapshotState(state, now()));
    }, cfg.heartbeatMs);
    heartbeatTimer.unref?.();
    if (typeof ctx?.effect === 'function') {
      try {
        ctx.effect(
          () => () => {
            if (heartbeatTimer !== null) {
              clearInterval(heartbeatTimer);
              heartbeatTimer = null;
            }
          },
          'esp32-touch-bridge:heartbeat',
        );
      } catch (err) {
        log.warn(`[${name}] could not register heartbeat effect: ${err?.message ?? err}`);
      }
    }
  } else {
    log.info(`[${name}] no serial port configured (config.port / ESP32_TOUCH_PORT); loaded without touching a device`);
  }

  // Returned to Cordis as this plugin's effect: teardown is idempotent.
  return async function dispose() {
    if (stopped) return;
    stopped = true;
    if (heartbeatTimer !== null) {
      clearInterval(heartbeatTimer);
      heartbeatTimer = null;
    }
    // Stop the transport first: it rejects a pending start() and closes stdin,
    // so a still-starting helper cannot delay unload for its start timeout.
    const current = helper;
    helper = null;
    if (current && typeof current.stop === 'function') {
      try {
        await current.stop();
      } catch (err) {
        log.debug(`[${name}] transport stop failed: ${err?.message ?? err}`);
      }
    }
    await chain.catch(() => {});
  };
}