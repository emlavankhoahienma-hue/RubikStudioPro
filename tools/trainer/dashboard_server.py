#!/usr/bin/env python3
"""
RubikStudioPro Overnight PC Trainer Web Dashboard & Process Controller.
Serves mobile + desktop live training telemetry over LAN & localhost (default port 8765),
launches/stops tools/trainer/trainer_engine.exe cleanly, and supports verified post-completion
Windows auto-shutdown (shutdown /s /t 60) strictly after atomic save + validation pass.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT_DIR = Path(__file__).resolve().parents[2]
TRAINER_DIR = Path(__file__).resolve().parent
ENGINE_EXE = TRAINER_DIR / ("trainer_engine.exe" if os.name == "nt" else "trainer_engine")
STATUS_FILE = TRAINER_DIR / "status.json"
STOP_FLAG = TRAINER_DIR / "stop.flag"
PAYLOAD_HPP = ROOT_DIR / "CoreCpp" / "Common" / "TrainedPayload.hpp"


def get_lan_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
    except Exception:
        try:
            return socket.gethostbyname(socket.gethostname())
        except Exception:
            return "127.0.0.1"


class TrainerController:
    def __init__(self, port: int):
        self.lock = threading.Lock()
        self.port = port
        self.proc: subprocess.Popen | None = None
        self.monitor_thread: threading.Thread | None = None
        self.auto_shutdown = False
        self.manual_stop_requested = False
        self.shutdown_triggered = False
        self.last_exit_code: int | None = None
        self.server_note = ""

    def is_running(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def start(self, hours: float, threads: int, val_count: int, batch_count: int,
              max_ram_mb: float, auto_shutdown: bool) -> tuple[bool, str]:
        with self.lock:
            if self.is_running():
                return False, "Trainer is already running."
            if not ENGINE_EXE.exists():
                return False, f"Engine binary not found: {ENGINE_EXE}"
            try:
                if STOP_FLAG.exists():
                    STOP_FLAG.unlink()
            except OSError:
                pass

            self.auto_shutdown = bool(auto_shutdown)
            self.manual_stop_requested = False
            self.shutdown_triggered = False
            self.last_exit_code = None
            self.server_note = f"Started training ({hours}h, {threads} threads)."

            cmd = [
                str(ENGINE_EXE),
                "--hours", f"{max(0.01, float(hours)):.4f}",
                "--threads", str(max(1, int(threads))),
                "--val", str(max(4, int(val_count))),
                "--batch", str(max(4, int(batch_count))),
                "--max-ram-mb", f"{max(256.0, float(max_ram_mb)):.1f}",
                "--out", str(PAYLOAD_HPP),
                "--status", str(STATUS_FILE),
                "--stop-file", str(STOP_FLAG),
            ]
            self.proc = subprocess.Popen(
                cmd,
                cwd=str(ROOT_DIR),
                stdout=subprocess.DEVNULL,
                stderr=subprocess.STDOUT,
            )
            self.monitor_thread = threading.Thread(target=self._watch_proc, daemon=True)
            self.monitor_thread.start()
            return True, "Trainer launched."

    def stop(self) -> tuple[bool, str]:
        with self.lock:
            if not self.is_running():
                return False, "Trainer is not running."
            self.manual_stop_requested = True
            STOP_FLAG.write_text("stop\n", encoding="utf-8")
            self.server_note = "Stop & Save signal sent. Finishing current epoch safely..."
            return True, "Stop signal written; trainer will save checkpoint and exit cleanly."

    def set_auto_shutdown(self, enabled: bool) -> tuple[bool, str]:
        with self.lock:
            self.auto_shutdown = bool(enabled)
            if not self.auto_shutdown and self.shutdown_triggered and os.name == "nt":
                subprocess.run(["shutdown", "/a"], check=False)
                self.shutdown_triggered = False
                self.server_note = "Auto-shutdown disarmed & Windows shutdown aborted."
                return True, self.server_note
            self.server_note = f"Auto-shutdown {'ARMED' if self.auto_shutdown else 'DISARMED'}."
            return True, self.server_note

    def _watch_proc(self) -> None:
        proc = self.proc
        if proc is None:
            return
        code = proc.wait()
        with self.lock:
            self.last_exit_code = code
            should_shutdown = (
                self.auto_shutdown
                and not self.manual_stop_requested
                and code == 0
            )
        if not should_shutdown:
            return

        # Confirm final status & payload integrity before shutting down OS
        try:
            data = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
        except Exception:
            data = {}
        ok = (
            data.get("state") == "completed"
            and data.get("payload_saved") is True
            and PAYLOAD_HPP.exists()
            and PAYLOAD_HPP.stat().st_size > 100000
        )
        with self.lock:
            if ok:
                self.server_note = "Verified completion & saved checkpoint. Triggering Windows shutdown in 60s (run 'shutdown /a' to cancel)."
                self.shutdown_triggered = True
                if os.name == "nt":
                    subprocess.run(["shutdown", "/s", "/t", "60", "/c", "RubikStudioPro 5h Training Completed & Saved"], check=False)
            else:
                self.server_note = "Training exited without verified completion; OS shutdown skipped for safety."

    def snapshot(self) -> dict:
        status_data = {}
        if STATUS_FILE.exists():
            try:
                status_data = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
            except Exception:
                status_data = {"state": "reading", "message": "Status file busy..."}
        lan_ip = get_lan_ip()
        with self.lock:
            return {
                "controller": {
                    "running": self.is_running(),
                    "pid": self.proc.pid if self.is_running() else None,
                    "auto_shutdown": self.auto_shutdown,
                    "shutdown_triggered": self.shutdown_triggered,
                    "last_exit_code": self.last_exit_code,
                    "cpu_cores": os.cpu_count() or 4,
                    "note": self.server_note,
                    "local_url": f"http://localhost:{self.port}",
                    "lan_url": f"http://{lan_ip}:{self.port}",
                },
                "trainer": status_data,
            }


DASHBOARD_HTML = r"""<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>RubikStudioPro · Overnight PC Trainer</title>
<style>
  :root {
    --bg:#0d1117; --card:#161b22; --card2:#1c232d; --border:#30363d;
    --text:#e6edf3; --muted:#8b949e; --accent:#58a6ff; --green:#3fb950;
    --amber:#d29922; --red:#f85149; --purple:#bc8cff;
  }
  * { box-sizing:border-box; margin:0; padding:0; }
  body { background:var(--bg); color:var(--text); font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif; padding:12px; line-height:1.45; }
  .wrap { max-width:1180px; margin:0 auto; display:flex; flex-direction:column; gap:12px; }
  .header { display:flex; flex-wrap:wrap; justify-content:space-between; align-items:center; gap:10px; background:var(--card); border:1px solid var(--border); border-radius:12px; padding:14px 16px; }
  .title { font-size:1.15rem; font-weight:700; display:flex; align-items:center; gap:8px; }
  .pill { font-size:.75rem; font-weight:700; padding:4px 10px; border-radius:999px; text-transform:uppercase; letter-spacing:.04em; background:#21262d; border:1px solid var(--border); }
  .pill.run { background:rgba(63,185,80,.15); color:var(--green); border-color:rgba(63,185,80,.4); }
  .pill.bfs { background:rgba(88,166,255,.15); color:var(--accent); border-color:rgba(88,166,255,.4); }
  .pill.done { background:rgba(188,140,255,.15); color:var(--purple); border-color:rgba(188,140,255,.4); }
  .pill.err { background:rgba(248,81,73,.15); color:var(--red); border-color:rgba(248,81,73,.4); }
  .urls { font-size:.82rem; color:var(--muted); display:flex; flex-wrap:wrap; gap:10px; }
  .urls code { background:var(--card2); padding:2px 7px; border-radius:6px; color:var(--accent); border:1px solid var(--border); }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(270px,1fr)); gap:12px; }
  .card { background:var(--card); border:1px solid var(--border); border-radius:12px; padding:14px 16px; }
  .card h2 { font-size:.86rem; text-transform:uppercase; letter-spacing:.05em; color:var(--muted); margin-bottom:10px; display:flex; justify-content:space-between; }
  .big { font-size:1.85rem; font-weight:800; font-variant-numeric:tabular-nums; }
  .sub { font-size:.83rem; color:var(--muted); margin-top:4px; }
  .bar-bg { width:100%; height:10px; background:#21262d; border-radius:999px; overflow:hidden; margin:8px 0; }
  .bar-fill { height:100%; background:linear-gradient(90deg,var(--accent),var(--green)); transition:width .4s ease; }
  .row { display:flex; justify-content:space-between; font-size:.86rem; padding:4px 0; border-bottom:1px dashed #21262d; }
  .row:last-child { border-bottom:none; }
  .controls { display:grid; grid-template-columns:repeat(auto-fit,minmax(135px,1fr)); gap:10px; align-items:end; }
  label { display:flex; flex-direction:column; gap:4px; font-size:.78rem; color:var(--muted); }
  input[type=number], select { background:var(--card2); color:var(--text); border:1px solid var(--border); border-radius:8px; padding:8px 10px; font-size:.92rem; width:100%; }
  .chk { flex-direction:row; align-items:center; gap:8px; font-size:.86rem; color:var(--text); padding:6px 0; cursor:pointer; }
  .btn-row { display:flex; flex-wrap:wrap; gap:8px; margin-top:10px; }
  button { border:none; border-radius:8px; padding:10px 14px; font-weight:700; font-size:.88rem; cursor:pointer; color:#fff; background:var(--accent); flex:1; min-width:130px; }
  button:disabled { opacity:.45; cursor:not-allowed; }
  button.stop { background:var(--red); }
  button.sec { background:#30363d; }
  button.warn { background:var(--amber); color:#0d1117; }
  .chart-box { width:100%; height:220px; background:var(--card2); border-radius:10px; border:1px solid var(--border); padding:8px; }
  .weights-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(130px,1fr)); gap:6px; font-size:.8rem; }
  .wchip { background:var(--card2); border:1px solid var(--border); border-radius:8px; padding:6px 8px; display:flex; justify-content:space-between; }
  .logs { background:#090c10; border:1px solid var(--border); border-radius:10px; padding:10px; max-height:220px; overflow-y:auto; font-family:ui-monospace,Consolas,monospace; font-size:.78rem; color:#c9d1d9; }
  .logs div { padding:2px 0; border-bottom:1px solid #161b22; }
  .ok { color:var(--green); } .bad { color:var(--red); } .hl { color:var(--amber); }
</style>
</head>
<body>
<div class="wrap">
  <div class="header">
    <div>
      <div class="title">RubikStudioPro Overnight Trainer <span id="statePill" class="pill">IDLE</span></div>
      <div class="sub" id="statusMsg">Waiting to start...</div>
    </div>
    <div class="urls">
      <span>PC: <code id="localUrl">-</code></span>
      <span>Phone (LAN): <code id="lanUrl">-</code></span>
    </div>
  </div>

  <div class="grid">
    <div class="card">
      <h2>4x4 Solution Length <span>Base 177.1 → Gate ≤75</span></h2>
      <div class="big" id="len4">-</div>
      <div class="sub" id="meta4">P95: - | Max: - | Time P50: -</div>
      <div class="row"><span>Solve Success Rate</span><strong id="succ4">-</strong></div>
      <div class="row"><span>Reduction vs Baseline</span><strong class="ok" id="drop4">-</strong></div>
    </div>

    <div class="card">
      <h2>5x5 Solution Length <span>Base 456.7 → Gate ≤120</span></h2>
      <div class="big" id="len5">-</div>
      <div class="sub" id="meta5">P95: - | Max: - | Time P50: -</div>
      <div class="row"><span>Solve Success Rate</span><strong id="succ5">-</strong></div>
      <div class="row"><span>Reduction vs Baseline</span><strong class="ok" id="drop5">-</strong></div>
    </div>

    <div class="card">
      <h2>Progress &amp; Hardware <span id="threadsLbl">-</span></h2>
      <div class="row"><span>Phase A (9 BFS Tables)</span><strong id="bfsPct">0%</strong></div>
      <div class="bar-bg"><div class="bar-fill" id="bfsBar" style="width:0%"></div></div>
      <div class="row"><span>Phase B (Time Budget)</span><strong id="timePct">0%</strong></div>
      <div class="bar-bg"><div class="bar-fill" id="timeBar" style="width:0%"></div></div>
      <div class="row"><span>Epoch / Saved Updates</span><strong id="epLbl">0 / 0</strong></div>
      <div class="row"><span>Elapsed / ETA</span><strong id="etaLbl">0s / -</strong></div>
      <div class="row"><span>RAM / Payload CRC</span><strong id="ramLbl">-</strong></div>
    </div>
  </div>

  <div class="card">
    <h2>Real Training Trajectory (Best Validation Moves vs Epoch) <span id="histMeta">0 points</span></h2>
    <div class="chart-box"><svg id="chartSvg" viewBox="0 0 800 200" width="100%" height="100%" preserveAspectRatio="none"></svg></div>
  </div>

  <div class="grid">
    <div class="card">
      <h2>4x4 Evolved Weights</h2>
      <div class="weights-grid" id="w4Grid"></div>
    </div>
    <div class="card">
      <h2>5x5 Evolved Weights</h2>
      <div class="weights-grid" id="w5Grid"></div>
    </div>
  </div>

  <div class="card">
    <h2>Controls &amp; Auto-Shutdown</h2>
    <div class="controls">
      <label>Preset / Duration (hours)
        <input id="inHours" type="number" step="0.1" min="0.02" max="24" value="5.0">
      </label>
      <label>Quick Presets
        <select id="presetSel" onchange="document.getElementById('inHours').value=this.value">
          <option value="5.0" selected>5.0h (Overnight)</option>
          <option value="3.0">3.0h (Medium)</option>
          <option value="1.0">1.0h (Fast)</option>
          <option value="0.05">3m (Test Run)</option>
        </select>
      </label>
      <label>CPU Threads
        <input id="inThreads" type="number" min="1" max="16" value="4">
      </label>
      <label>Val Cubes (Fixed)
        <input id="inVal" type="number" min="8" max="200" value="40">
      </label>
      <label>Batch / Epoch
        <input id="inBatch" type="number" min="4" max="64" value="16">
      </label>
    </div>
    <label class="chk">
      <input id="chkShutdown" type="checkbox" onchange="toggleShutdown(this.checked)">
      <span>Auto-shutdown Windows (60s delay) cleanly after full completion &amp; verified checkpoint save</span>
    </label>
    <div class="btn-row">
      <button id="btnStart" onclick="startTrain()">Start Training</button>
      <button id="btnStop" class="stop" onclick="stopTrain()" disabled>Stop &amp; Save Checkpoint</button>
      <button id="btnAbortSd" class="warn" onclick="abortShutdown()" style="display:none">Cancel Windows Shutdown</button>
    </div>
    <div class="sub" id="ctrlNote" style="margin-top:8px"></div>
  </div>

  <div class="card">
    <h2>Live Training Log</h2>
    <div class="logs" id="logBox"></div>
  </div>
</div>

<script>
const fmtTime = s => {
  s = Math.max(0, Math.floor(s || 0));
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  return (h > 0 ? h + 'h ' : '') + String(m).padStart(2,'0') + 'm ' + String(sec).padStart(2,'0') + 's';
};

function renderWeights(elId, w) {
  const el = document.getElementById(elId);
  if (!w) { el.innerHTML = ''; return; }
  el.innerHTML = Object.entries(w).map(([k, v]) =>
    `<div class="wchip"><span>${k}</span><strong>${typeof v==='number' ? (Number.isInteger(v)?v:v.toFixed(1)) : v}</strong></div>`
  ).join('');
}

function renderChart(history) {
  const svg = document.getElementById('chartSvg');
  document.getElementById('histMeta').textContent = (history?.length || 0) + ' points';
  if (!history || history.length === 0) {
    svg.innerHTML = '<text x="400" y="105" fill="#8b949e" font-size="14" text-anchor="middle">Waiting for Epoch 0 baseline...</text>';
    return;
  }
  const W=800, H=200, padL=44, padR=16, padT=16, padB=24;
  const xAt = i => history.length === 1 ? (padL + W - padR)/2 : padL + (i / (history.length - 1)) * (W - padL - padR);
  // Dual normalized bands: top half 5x5 (90..150), bottom half 4x4 (50..100)
  const y5 = v => padT + (1 - Math.min(1, Math.max(0, (v - 90) / 60))) * 76;
  const y4 = v => 104 + (1 - Math.min(1, Math.max(0, (v - 50) / 50))) * 72;

  const p5 = history.map((h, i) => `${xAt(i).toFixed(1)},${y5(h.m5).toFixed(1)}`).join(' ');
  const p4 = history.map((h, i) => `${xAt(i).toFixed(1)},${y4(h.m4).toFixed(1)}`).join(' ');
  const g5 = y5(120).toFixed(1), g4 = y4(75).toFixed(1);

  let dots = '';
  history.forEach((h, i) => {
    if (h.acc) {
      dots += `<circle cx="${xAt(i).toFixed(1)}" cy="${y5(h.m5).toFixed(1)}" r="3.5" fill="#bc8cff"/>`;
      dots += `<circle cx="${xAt(i).toFixed(1)}" cy="${y4(h.m4).toFixed(1)}" r="3.5" fill="#3fb950"/>`;
    }
  });

  svg.innerHTML = `
    <line x1="${padL}" y1="${g5}" x2="${W-padR}" y2="${g5}" stroke="#bc8cff" stroke-dasharray="4 4" opacity="0.5"/>
    <text x="4" y="${Number(g5)+4}" fill="#bc8cff" font-size="10">5x5 ≤120</text>
    <line x1="${padL}" y1="${g4}" x2="${W-padR}" y2="${g4}" stroke="#3fb950" stroke-dasharray="4 4" opacity="0.5"/>
    <text x="4" y="${Number(g4)+4}" fill="#3fb950" font-size="10">4x4 ≤75</text>
    <polyline fill="none" stroke="#bc8cff" stroke-width="2.4" points="${p5}"/>
    <polyline fill="none" stroke="#3fb950" stroke-width="2.4" points="${p4}"/>
    ${dots}
    <text x="${W-padR}" y="14" fill="#bc8cff" font-size="11" text-anchor="end">5x5 Best: ${history[history.length-1].m5.toFixed(2)}</text>
    <text x="${W-padR}" y="192" fill="#3fb950" font-size="11" text-anchor="end">4x4 Best: ${history[history.length-1].m4.toFixed(2)}</text>
  `;
}

let firstLoad = true;
async function poll() {
  try {
    const r = await fetch('/api/status');
    const data = await r.json();
    const c = data.controller || {}, t = data.trainer || {};
    document.getElementById('localUrl').textContent = c.local_url || '-';
    document.getElementById('lanUrl').textContent = c.lan_url || '-';
    if (firstLoad && c.cpu_cores) {
      document.getElementById('inThreads').value = Math.max(1, Math.min(8, c.cpu_cores));
      firstLoad = false;
    }

    const st = t.state || (c.running ? 'running' : 'idle');
    const pill = document.getElementById('statePill');
    pill.textContent = st.toUpperCase();
    pill.className = 'pill ' + (st==='training'||st==='validating' ? 'run' : st==='bfs_tables' ? 'bfs' : st==='completed' ? 'done' : st==='error' ? 'err' : '');
    document.getElementById('statusMsg').textContent = t.message || c.note || 'Ready';
    document.getElementById('ctrlNote').textContent = c.note || '';

    document.getElementById('btnStart').disabled = !!c.running;
    document.getElementById('btnStop').disabled = !c.running;
    document.getElementById('chkShutdown').checked = !!c.auto_shutdown;
    document.getElementById('btnAbortSd').style.display = c.shutdown_triggered ? 'inline-block' : 'none';

    const b4 = t.best_4x4 || {}, b5 = t.best_5x5 || {};
    document.getElementById('len4').textContent = b4.count ? b4.len_mean.toFixed(2) + ' moves' : '-';
    document.getElementById('meta4').textContent = b4.count ? `P95: ${b4.len_p95} | Max: ${b4.len_max} | P50: ${b4.ms_p50.toFixed(1)} ms` : 'Waiting baseline...';
    document.getElementById('succ4').textContent = b4.count ? `${b4.solved}/${b4.count} (${(b4.success*100).toFixed(1)}%)` : '-';
    document.getElementById('drop4').textContent = b4.count ? `-${((1 - b4.len_mean/177.1)*100).toFixed(1)}%` : '-';

    document.getElementById('len5').textContent = b5.count ? b5.len_mean.toFixed(2) + ' moves' : '-';
    document.getElementById('meta5').textContent = b5.count ? `P95: ${b5.len_p95} | Max: ${b5.len_max} | P50: ${b5.ms_p50.toFixed(1)} ms` : 'Waiting baseline...';
    document.getElementById('succ5').textContent = b5.count ? `${b5.solved}/${b5.count} (${(b5.success*100).toFixed(1)}%)` : '-';
    document.getElementById('drop5').textContent = b5.count ? `-${((1 - b5.len_mean/456.7)*100).toFixed(1)}%` : '-';

    const bfsPct = Number(t.tables_pct || 0);
    const tgtSec = Number(t.target_hours || 0) * 3600;
    const elSec = Number(t.elapsed_sec || 0);
    const tPct = tgtSec > 0 ? Math.min(100, (elSec / tgtSec) * 100) : 0;
    document.getElementById('threadsLbl').textContent = (t.threads || c.cpu_cores || '-') + ' Threads';
    document.getElementById('bfsPct').textContent = bfsPct.toFixed(1) + '% (' + (t.tables_built||0) + '/9)';
    document.getElementById('bfsBar').style.width = bfsPct + '%';
    document.getElementById('timePct').textContent = tPct.toFixed(1) + '%';
    document.getElementById('timeBar').style.width = tPct + '%';
    document.getElementById('epLbl').textContent = `${t.epoch || 0} / ${t.accepted_updates || 0} saved`;
    document.getElementById('etaLbl').textContent = `${fmtTime(elSec)} / ${t.eta_sec > 0 ? fmtTime(t.eta_sec) : '0s'}`;
    document.getElementById('ramLbl').textContent = `${(t.ram_mb||0).toFixed(1)} MB | CRC ${t.checksum || '-'}`;

    renderChart(t.history || []);
    renderWeights('w4Grid', t.weights_4x4);
    renderWeights('w5Grid', t.weights_5x5);

    const logBox = document.getElementById('logBox');
    logBox.innerHTML = (t.logs || []).slice(-30).reverse().map(l => `<div>${l.replace(/</g,'&lt;')}</div>`).join('');
  } catch (e) {}
}

async function startTrain() {
  const body = {
    hours: parseFloat(document.getElementById('inHours').value),
    threads: parseInt(document.getElementById('inThreads').value, 10),
    val_count: parseInt(document.getElementById('inVal').value, 10),
    batch_count: parseInt(document.getElementById('inBatch').value, 10),
    auto_shutdown: document.getElementById('chkShutdown').checked,
  };
  await fetch('/api/start', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
  setTimeout(poll, 300);
}
async function stopTrain() {
  await fetch('/api/stop', {method:'POST'});
  setTimeout(poll, 300);
}
async function toggleShutdown(val) {
  await fetch('/api/shutdown-toggle', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({enabled:val})});
  setTimeout(poll, 300);
}
async function abortShutdown() {
  document.getElementById('chkShutdown').checked = false;
  await toggleShutdown(false);
}

poll();
setInterval(poll, 1200);
</script>
</body>
</html>
"""


def make_handler(ctrl: TrainerController):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def _send_json(self, code: int, obj: dict):
            raw = json.dumps(obj).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/":
                raw = DASHBOARD_HTML.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
            elif path == "/api/status":
                self._send_json(200, ctrl.snapshot())
            else:
                self._send_json(404, {"error": "Not found"})

        def do_POST(self):
            path = urlparse(self.path).path
            length = int(self.headers.get("Content-Length", "0") or 0)
            body = {}
            if length > 0:
                try:
                    body = json.loads(self.rfile.read(length).decode("utf-8"))
                except Exception:
                    body = {}

            if path == "/api/start":
                ok, msg = ctrl.start(
                    hours=body.get("hours", 5.0),
                    threads=body.get("threads", os.cpu_count() or 4),
                    val_count=body.get("val_count", 40),
                    batch_count=body.get("batch_count", 16),
                    max_ram_mb=body.get("max_ram_mb", 1500.0),
                    auto_shutdown=body.get("auto_shutdown", False),
                )
                self._send_json(200 if ok else 409, {"ok": ok, "message": msg})
            elif path == "/api/stop":
                ok, msg = ctrl.stop()
                self._send_json(200 if ok else 409, {"ok": ok, "message": msg})
            elif path == "/api/shutdown-toggle":
                ok, msg = ctrl.set_auto_shutdown(bool(body.get("enabled", False)))
                self._send_json(200, {"ok": ok, "message": msg})
            else:
                self._send_json(404, {"error": "Not found"})

    return Handler


def main():
    parser = argparse.ArgumentParser(description="RubikStudioPro Overnight PC Trainer Dashboard")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host (0.0.0.0 allows mobile LAN access)")
    parser.add_argument("--port", type=int, default=8765, help="HTTP port (default 8765)")
    args = parser.parse_args()

    ctrl = TrainerController(args.port)
    srv = ThreadingHTTPServer((args.host, args.port), make_handler(ctrl))
    lan = get_lan_ip()
    print(f"RubikStudioPro Trainer Dashboard running:")
    print(f"  PC Browser    : http://localhost:{args.port}")
    print(f"  Phone / Mobile: http://{lan}:{args.port}  (same Wi-Fi/LAN)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down dashboard server...")
        srv.shutdown()


if __name__ == "__main__":
    main()
