/**
 * Serial helper client: plugin side of the internal PowerShell line protocol
 * (docs/12-dsh-plugin.md section 4).
 *
 * The plugin owns one child process. The child opens the COM port and retries
 * that open every 2 s on its own; this client only re-spawns the process if it
 * exits unexpectedly. Everything (spawn function, timeouts) is injectable so
 * `node --test` never touches a real serial port or PowerShell.
 *
 * plugin -> helper (JSON lines on stdin):
 *   {"op":"open","port":"COM7","baud":115200}
 *   {"op":"write","data":"<base64 bytes>"}
 *   {"op":"close"}   {"op":"ping"}
 * helper -> plugin (JSON lines on stdout):
 *   {"op":"ready"}  {"op":"line","data":"<base64 device log>"}
 *   {"op":"state","connected":true}  {"op":"error","message":"..."}
 */
import { spawn as nodeSpawn } from 'node:child_process';
import readline from 'node:readline';
import { fileURLToPath } from 'node:url';

export const DEFAULT_BAUD = 115200;
export const DEFAULT_RESTART_DELAY_MS = 2000;
export const DEFAULT_START_TIMEOUT_MS = 30000;
export const DEFAULT_STOP_GRACE_MS = 1500;

/** Absolute path of the bundled helper script (never installed as a bin). */
export function helperScriptPath() {
  return fileURLToPath(new URL('../helper/serial-bridge.ps1', import.meta.url));
}

/** Windows PowerShell 5.1, which owns System.IO.Ports.SerialPort. */
export function defaultHelperCommand() {
  const root = process.env.SystemRoot || process.env.WINDIR || 'C:\\Windows';
  return `${root}\\System32\\WindowsPowerShell\\v1.0\\powershell.exe`;
}

export function createSerialHelper({
  port,
  baud = DEFAULT_BAUD,
  command = defaultHelperCommand(),
  args = ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', helperScriptPath()],
  spawnImpl = nodeSpawn,
  cwd,
  env,
  startTimeoutMs = DEFAULT_START_TIMEOUT_MS,
  restartDelayMs = DEFAULT_RESTART_DELAY_MS,
  stopGraceMs = DEFAULT_STOP_GRACE_MS,
  onLine = () => {},
  onState = () => {},
  onError = () => {},
  log = () => {},
  setTimeoutImpl = setTimeout,
  clearTimeoutImpl = clearTimeout,
} = {}) {
  if (typeof port !== 'string' || port.trim().length === 0) {
    throw new TypeError('createSerialHelper requires a non-empty port');
  }

  let child = null;
  let rl = null;
  let ready = false;
  let connected = false;
  let stopping = false;
  let disposed = false;
  let startPromise = null;
  let resolveStart = null;
  let rejectStart = null;
  let startTimer = null;
  let restartTimer = null;
  const connectionWaiters = new Set();

  function clearStartTimer() {
    if (startTimer !== null) {
      clearTimeoutImpl(startTimer);
      startTimer = null;
    }
  }

  function failStart(error) {
    clearStartTimer();
    const reject = rejectStart;
    rejectStart = null;
    resolveStart = null;
    startPromise = null;
    if (reject) reject(error);
  }

  function resolveStarted() {
    clearStartTimer();
    const resolve = resolveStart;
    resolveStart = null;
    rejectStart = null;
    if (resolve) resolve();
  }

  function setConnected(value) {
    if (connected === value) return;
    connected = value;
    try {
      onState({ connected });
    } catch (err) {
      log(`[serial-helper] onState failed: ${err?.message ?? err}`);
    }
    if (value) {
      for (const waiter of [...connectionWaiters]) waiter(true);
    }
  }

  function writeJson(message) {
    if (!child || !child.stdin || child.stdin.destroyed === true || child.stdin.writable === false) return false;
    try {
      child.stdin.write(JSON.stringify(message) + '\n');
      return true;
    } catch (err) {
      log(`[serial-helper] stdin write failed: ${err?.message ?? err}`);
      return false;
    }
  }

  function handleLine(line) {
    if (typeof line !== 'string') return;
    const text = line.trim();
    if (text.length === 0) return;
    let message;
    try {
      message = JSON.parse(text);
    } catch {
      log(`[serial-helper] ignoring non-JSON helper line: ${text.slice(0, 120)}`);
      return;
    }
    switch (message?.op) {
      case 'ready':
        ready = true;
        resolveStarted();
        // The helper announces readiness before a port exists; ask it to open.
        writeJson({ op: 'open', port, baud });
        break;
      case 'state':
        setConnected(message.connected === true);
        break;
      case 'line': {
        const text = Buffer.from(String(message.data ?? ''), 'base64').toString('utf8');
        try {
          onLine(text);
        } catch (err) {
          log(`[serial-helper] onLine failed: ${err?.message ?? err}`);
        }
        break;
      }
      case 'error':
        try {
          onError(new Error(String(message.message ?? 'serial helper error')));
        } catch (err) {
          log(`[serial-helper] onError failed: ${err?.message ?? err}`);
        }
        break;
      default:
        log(`[serial-helper] ignoring unknown helper op: ${String(message?.op)}`);
    }
  }

  function handleExit(code, signal) {
    const hadChild = child !== null;
    child = null;
    ready = false;
    clearStartTimer();
    if (rl) {
      try {
        rl.close();
      } catch {
        // ignore
      }
      rl = null;
    }
    failStart(new Error(`serial helper exited (code ${String(code)}, signal ${String(signal)})`));
    setConnected(false);
    if (!hadChild || stopping || disposed) return;
    scheduleRestart();
  }

  function scheduleRestart() {
    if (restartTimer !== null || stopping || disposed) return;
    restartTimer = setTimeoutImpl(() => {
      restartTimer = null;
      if (stopping || disposed) return;
      log('[serial-helper] restarting helper process');
      try {
        spawnChild();
      } catch (err) {
        log(`[serial-helper] restart failed: ${err?.message ?? err}`);
        scheduleRestart();
      }
    }, restartDelayMs);
    restartTimer.unref?.();
  }

  function spawnChild() {
    const proc = spawnImpl(command, args, {
      stdio: ['pipe', 'pipe', 'ignore'],
      cwd,
      env,
      windowsHide: true,
    });
    child = proc;
    ready = false;
    proc.on?.('error', (err) => {
      log(`[serial-helper] helper process error: ${err?.message ?? err}`);
      failStart(err instanceof Error ? err : new Error(String(err)));
      if (child === proc) handleExit(null, null);
    });
    proc.on?.('exit', (code, signal) => {
      log(`[serial-helper] helper process exited code=${String(code)} signal=${String(signal)}`);
      if (child === proc) handleExit(code, signal);
    });
    if (proc.stdout) {
      rl = readline.createInterface({ input: proc.stdout });
      rl.on('line', handleLine);
      rl.on('error', () => {});
    } else {
      failStart(new Error('serial helper produced no stdout'));
    }
    if (proc.stdin && typeof proc.stdin.on === 'function') {
      proc.stdin.on('error', () => {});
    }
    return proc;
  }

  function start() {
    if (disposed) return Promise.reject(new Error('serial helper disposed'));
    if (startPromise) return startPromise;
    if (child && ready) return Promise.resolve();
    startPromise = new Promise((resolve, reject) => {
      resolveStart = resolve;
      rejectStart = reject;
      startTimer = setTimeoutImpl(() => {
        failStart(new Error('serial helper start timed out'));
        if (child) {
          try {
            child.kill?.();
          } catch {
            // ignore
          }
        }
      }, startTimeoutMs);
      startTimer.unref?.();
    });
    if (!child) {
      try {
        spawnChild();
      } catch (err) {
        failStart(err instanceof Error ? err : new Error(String(err)));
      }
    }
    return startPromise;
  }

  /** Serialize one C05 line and write it to the device; false when not connected. */
  function writeLine(line) {
    if (typeof line !== 'string' || !connected) return false;
    return writeJson({ op: 'write', data: Buffer.from(line + '\n', 'utf8').toString('base64') });
  }

  function writeBytes(bytes) {
    if (!connected) return false;
    return writeJson({ op: 'write', data: Buffer.from(bytes).toString('base64') });
  }

  function closePort() {
    return writeJson({ op: 'close' });
  }

  function ping() {
    return writeJson({ op: 'ping' });
  }

  function whenConnected(timeoutMs = 5000) {
    if (connected) return Promise.resolve(true);
    if (disposed || stopping) return Promise.resolve(false);
    return new Promise((resolve) => {
      const waiter = (value) => {
        clearTimeoutImpl(timer);
        connectionWaiters.delete(waiter);
        resolve(value);
      };
      const timer = setTimeoutImpl(() => waiter(false), timeoutMs);
      timer.unref?.();
      connectionWaiters.add(waiter);
    });
  }

  async function stop() {
    if (stopping) return;
    stopping = true;
    if (restartTimer !== null) {
      clearTimeoutImpl(restartTimer);
      restartTimer = null;
    }
    const proc = child;
    if (!proc) {
      disposed = true;
      failStart(new Error('serial helper stopped'));
      return;
    }
    writeJson({ op: 'close' });
    try {
      proc.stdin?.end?.();
    } catch {
      // ignore
    }
    const exited = await new Promise((resolve) => {
      if (proc.exitCode !== null && proc.exitCode !== undefined) {
        resolve(true);
        return;
      }
      let settled = false;
      const timer = setTimeoutImpl(() => {
        if (settled) return;
        settled = true;
        resolve(false);
      }, stopGraceMs);
      timer.unref?.();
      proc.once?.('exit', () => {
        if (settled) return;
        settled = true;
        clearTimeoutImpl(timer);
        resolve(true);
      });
    });
    if (!exited) {
      try {
        proc.kill?.();
      } catch {
        // ignore
      }
    }
    if (child === proc) child = null;
    if (rl) {
      try {
        rl.close();
      } catch {
        // ignore
      }
      rl = null;
    }
    disposed = true;
    setConnected(false);
    failStart(new Error('serial helper stopped'));
  }

  return {
    start,
    stop,
    writeLine,
    writeBytes,
    closePort,
    ping,
    whenConnected,
    get connected() {
      return connected;
    },
    get ready() {
      return ready;
    },
    get pid() {
      return child?.pid;
    },
    isRunning() {
      return child !== null && child.exitCode === null && !stopping && !disposed;
    },
  };
}