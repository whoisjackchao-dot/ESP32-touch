# M5a 设备事件入口 H 测试

- port: COM7
- 结果：16/16 PASS

| 检查 | 结果 | 明细 |
|---|---|---|
| boot_m5a_ready | PASS | PIO0 KEY2=GPIO4 PWR=GPIO5 (low active) I (737) m3: m5a: READY (240x240, 8fps, keys=GPIO0/4/5, pomo 25/5, C05 USB events) |
| task_start_working | PASS | latency=0.165s {'state': 'WORKING', 'frame': 4, 'cards': 1} |
| g12_latency_le_1s | PASS | latency=0.165 |
| cards_text | PASS | M5A-CARD |
| dump_working | PASS |  |
| tasks3_order | PASS | C-TOP|C-MID|C-OLD |
| input_required | PASS | {'state': 'INPUT_REQUIRED', 'frame': 5, 'cards': 3} |
| dump_input | PASS |  |
| task_done | PASS | {'state': 'DONE', 'frame': 2, 'cards': 3} |
| dump_done | PASS |  |
| task_error | PASS | {'state': 'FAILED', 'frame': 3, 'cards': 3} |
| idle_edge_done | PASS | {'state': 'DONE', 'frame': 2, 'cards': 3} |
| invalid_no_crash | PASS | {'state': 'DONE', 'frame': 2, 'cards': 3} -> {'state': 'DONE', 'frame': 2, 'cards': 3} |
| tasks_empty_clears | PASS | None |
| offline_after_15s | PASS | {'state': 'OFFLINE', 'frame': 1, 'cards': 0} |
| heartbeat_restore | PASS | {'state': 'WORKING', 'frame': 4, 'cards': 0} |
