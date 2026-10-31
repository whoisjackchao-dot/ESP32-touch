#!/usr/bin/env node
/**
 * Real-device E2E (H-class): drive the ESP32-S3 pet with the SAME transport and
 * encoder the plugin uses, then print whatever the device writes back.
 *
 * Default SKIP: this only runs when both environment variables are set, so
 * `node --test` and ordinary sessions never touch a serial port:
 *
 *   $env:ESP32_TOUCH_PORT = 'COM7'
 *   $env:ESP32_TOUCH_E2E  = '1'
 *   node test/e2e-device.mjs
 *
 * Optional: ESP32_TOUCH_BAUD (default 115200).
 * It sends the six C05 events (task.start, agent.input_required, task.done,
 * task.error, agent.idle, device.heartbeat) and a three-card stack, with a
 * short pause between lines so the device log is readable.
 *
 * Do NOT point this at a port owned by another program (for example the
 * StickS3 COM5 used by a different project): the open will fail and the test
 * exits non-zero instead of disturbing that process.
 */
import { createSerialHelper } from '../lib/serial-helper.js';
import { EVENTS, encodeEvent } from '../lib/protocol.js';

const port = (process.env.ESP32_TOUCH_PORT || '').trim();
const enabled = process.env.ESP32_TOUCH_E2E === '1';

if (!enabled || port.length === 0) {
  const reason = !enabled ? 'ESP32_TOUCH_E2E is not 1' : 'ESP32_TOUCH_PORT is not set';
  console.log(`[e2e-device] skipped (${reason}); set ESP32_TOUCH_PORT and ESP32_TOUCH_E2E=1 to run`);
  process.exit(0);
}

const baud = Number.parseInt(process.env.ESP32_TOUCH_BAUD ?? '115200', 10) || 115200;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

const card = (title, project, elapsed_s, status) => ({ title, project, agent: 'DSH', elapsed_s, status });
const single = [card('DSH E2E', 'ESP32-touch', 0, 'working')];
const threeCards = [
  card('DSH E2E / 顶卡', 'ESP32-touch', 12, 'input_required'),
  card('DSH E2E / 中卡', 'host-plugin', 7, 'working'),
  card('DSH E2E / 底卡', 'firmware', 3, 'done'),
];

const lines = [
  [EVENTS.TASK_START, single],
  [EVENTS.INPUT_REQUIRED, [card('DSH E2E', 'ESP32-touch', 1, 'input_required')]],
  [EVENTS.TASK_DONE, [card('DSH E2E', 'ESP32-touch', 2, 'done')]],
  [EVENTS.TASK_ERROR, [card('DSH E2E', 'ESP32-touch', 3, 'failed')]],
  [EVENTS.AGENT_IDLE, [card('DSH E2E', 'ESP32-touch', 4, 'done')]],
  [EVENTS.HEARTBEAT, []],
  [EVENTS.TASK_START, threeCards],
];

const helper = createSerialHelper({
  port,
  baud,
  onState: ({ connected }) => console.log(`[e2e-device] serial ${connected ? 'connected' : 'disconnected'}`),
  onLine: (text) => console.log(`[device] ${text.trimEnd()}`),
  onError: (error) => console.warn(`[e2e-device] helper error: ${error.message}`),
  log: (message) => console.log(`[e2e-device] ${message}`),
});

let exitCode = 0;
try {
  await helper.start();
  const connected = await helper.whenConnected(15000);
  if (!connected) {
    console.error(`[e2e-device] could not open ${port} within 15 s; check the port and that no other program owns it`);
    exitCode = 1;
  } else {
    for (const [event, cards] of lines) {
      const line = encodeEvent(event, { cards });
      console.log(`[e2e-device] -> ${line}`);
      helper.writeLine(line);
      await sleep(400);
    }
    await sleep(800);
    console.log('[e2e-device] done: expect 6 state transitions, 3 cards, then WORKING again');
  }
} catch (error) {
  console.error(`[e2e-device] failed: ${error?.message ?? error}`);
  exitCode = 1;
} finally {
  await helper.stop().catch(() => {});
}
process.exit(exitCode);