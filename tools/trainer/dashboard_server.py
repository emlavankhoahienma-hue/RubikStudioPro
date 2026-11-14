#!/usr/bin/env python3
"""
RubikStudioPro Overnight PC Trainer Web Dashboard & Process Controller.
Giao diện điều khiển & theo dõi quá trình train Rubik 4x4/5x5 trên máy tính & điện thoại qua Wi-Fi/LAN.
"""

import argparse
import json
import os
import socket
import subprocess
import threading
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
        self.server_note = "Hệ thống đã sẵn sàng. Chọn thời gian rồi bấm Bắt Đầu Train."

    def is_running(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def start(self, hours: float, threads: int, val_count: int, batch_count: int,
              max_ram_mb: float, auto_shutdown: bool) -> tuple[bool, str]:
        with self.lock:
            if self.is_running():
                return False, "Bộ train đang chạy rồi."
            if not ENGINE_EXE.exists():
                return False, f"Không tìm thấy file chạy trainer: {ENGINE_EXE}"
            try:
                if STOP_FLAG.exists():
                    STOP_FLAG.unlink()
            except OSError:
                pass

            self.auto_shutdown = bool(auto_shutdown)
            self.manual_stop_requested = False
            self.shutdown_triggered = False
            self.last_exit_code = None
            self.server_note = f"Đã khởi động train ({hours} giờ, {threads} luồng CPU)."

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
            return True, "Đã bắt đầu chạy bộ train."

    def stop(self) -> tuple[bool, str]:
        with self.lock:
            if not self.is_running():
                return False, "Bộ train hiện chưa chạy."
            self.manual_stop_requested = True
            STOP_FLAG.write_text("stop\n", encoding="utf-8")
            self.server_note = "Đã gửi lệnh Dừng & Lưu. Máy sẽ hoàn tất nốt vòng hiện tại rồi lưu file an toàn..."
            return True, "Đã gửi tín hiệu dừng an toàn."

    def set_auto_shutdown(self, enabled: bool) -> tuple[bool, str]:
        with self.lock:
            self.auto_shutdown = bool(enabled)
            if not self.auto_shutdown and self.shutdown_triggered and os.name == "nt":
                subprocess.run(["shutdown", "/a"], check=False)
                self.shutdown_triggered = False
                self.server_note = "Đã hủy hẹn giờ tắt máy Windows."
                return True, self.server_note
            self.server_note = (
                "Đã BẬT tự động tắt Windows sau khi train xong và lưu file."
                if self.auto_shutdown
                else "Đã TẮT tự động tắt Windows."
            )
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

        # Chỉ tắt máy khi train kết thúc đúng lịch và file payload đã được lưu hợp lệ
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
                self.server_note = "Đã kiểm tra file lưu hoàn chỉnh. Windows sẽ tắt sau 60 giây (bấm Hủy Lệnh Tắt Máy nếu muốn dừng)."
                self.shutdown_triggered = True
                if os.name == "nt":
                    subprocess.run(["shutdown", "/s", "/t", "60", "/c", "RubikStudioPro da train xong va luu file an toan"], check=False)
            else:
                self.server_note = "Quá trình train dừng chưa trọn vẹn nên hệ thống giữ máy bật để bảo toàn dữ liệu."

    def snapshot(self) -> dict:
        status_data = {}
        if STATUS_FILE.exists():
            try:
                status_data = json.loads(STATUS_FILE.read_text(encoding="utf-8"))
            except Exception:
                status_data = {"state": "reading", "message": "Đang cập nhật dữ liệu mới..."}
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
<title>RubikStudioPro · Huấn Luyện AI Trên PC</title>
<style>
  :root {
    --bg:#0b0f17; --card:#131a26; --card2:#1a2333; --border:#283448;
    --text:#eef2f7; --muted:#93a4bc; --accent:#4da3ff; --green:#2fd168;
    --amber:#f5b041; --red:#f25f5c; --purple:#b78bfa;
  }
  * { box-sizing:border-box; margin:0; padding:0; }
  body {
    background:var(--bg);
    color:var(--text);
    font-family:system-ui,-apple-system,"Segoe UI",Roboto,"Noto Sans",sans-serif;
    padding:14px;
    line-height:1.5;
  }
  .wrap { max-width:1160px; margin:0 auto; display:flex; flex-direction:column; gap:14px; }

  .card {
    background:var(--card);
    border:1px solid var(--border);
    border-radius:14px;
    padding:16px 18px;
    box-shadow:0 6px 20px rgba(0,0,0,.25);
  }
  .header {
    display:flex;
    flex-wrap:wrap;
    justify-content:space-between;
    align-items:center;
    gap:14px;
  }
  .title-row { display:flex; flex-wrap:wrap; align-items:center; gap:10px; }
  .title { font-size:1.22rem; font-weight:800; letter-spacing:-.01em; }
  .pill {
    font-size:.76rem; font-weight:700; padding:4px 11px; border-radius:999px;
    background:#1f2a3c; color:var(--muted); border:1px solid var(--border);
  }
  .pill.run { background:rgba(47,209,104,.16); color:var(--green); border-color:rgba(47,209,104,.45); }
  .pill.bfs { background:rgba(77,163,255,.16); color:var(--accent); border-color:rgba(77,163,255,.45); }
  .pill.done { background:rgba(183,139,250,.18); color:var(--purple); border-color:rgba(183,139,250,.45); }
  .pill.err { background:rgba(242,95,92,.18); color:var(--red); border-color:rgba(242,95,92,.45); }

  .sub { font-size:.88rem; color:var(--muted); margin-top:4px; }
  .link-box { display:flex; flex-wrap:wrap; gap:8px; align-items:center; }
  .link-chip {
    display:flex; align-items:center; gap:6px;
    background:var(--card2); border:1px solid var(--border);
    padding:6px 10px; border-radius:10px; font-size:.82rem; color:var(--muted);
  }
  .link-chip code { color:var(--accent); font-weight:600; }
  .copy-btn {
    background:#27354c; color:var(--text); border:1px solid var(--border);
    border-radius:7px; padding:3px 8px; font-size:.75rem; font-weight:600;
    cursor:pointer; min-width:auto; flex:none;
  }

  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(285px,1fr)); gap:14px; }
  .grid-2 { display:grid; grid-template-columns:repeat(auto-fit,minmax(320px,1fr)); gap:14px; }
  .card-head {
    display:flex; justify-content:space-between; align-items:center; gap:8px;
    margin-bottom:10px;
  }
  .card-head h2 {
    font-size:.92rem; font-weight:700; color:#d2deee;
  }
  .badge {
    font-size:.74rem; font-weight:700; padding:3px 9px; border-radius:8px;
    background:var(--card2); color:var(--muted); border:1px solid var(--border);
    white-space:nowrap;
  }
  .badge.pass { background:rgba(47,209,104,.14); color:var(--green); border-color:rgba(47,209,104,.35); }
  .badge.goal { background:rgba(77,163,255,.12); color:var(--accent); border-color:rgba(77,163,255,.3); }

  .big { font-size:2rem; font-weight:800; font-variant-numeric:tabular-nums; color:#fff; }
  .row {
    display:flex; justify-content:space-between; align-items:center;
    font-size:.88rem; padding:7px 0; border-bottom:1px dashed #222d3f;
  }
  .row:last-child { border-bottom:none; }
  .row span { color:var(--muted); }
  .ok { color:var(--green); } .warn-txt { color:var(--amber); }

  .bar-bg { width:100%; height:11px; background:#202b3d; border-radius:999px; overflow:hidden; margin:6px 0 12px; }
  .bar-fill { height:100%; background:linear-gradient(90deg,var(--accent),var(--green)); transition:width .4s ease; border-radius:999px; }

  /* Form Cài Đặt Train */
  .form-grid {
    display:grid;
    grid-template-columns:repeat(auto-fit,minmax(175px,1fr));
    gap:12px;
    margin-top:8px;
  }
  .field {
    background:var(--card2);
    border:1px solid var(--border);
    border-radius:11px;
    padding:10px 12px;
    display:flex;
    flex-direction:column;
    gap:6px;
  }
  .field label { font-size:.83rem; font-weight:700; color:#dce6f5; }
  .field .hint { font-size:.75rem; color:var(--muted); }
  input[type=number], select {
    background:#0e1520;
    color:#fff;
    border:1px solid #33435c;
    border-radius:8px;
    padding:9px 11px;
    font-size:.95rem;
    font-weight:600;
    width:100%;
    outline:none;
  }
  input[type=number]:focus, select:focus { border-color:var(--accent); }

  .shutdown-box {
    margin-top:12px;
    background:var(--card2);
    border:1px solid var(--border);
    border-radius:11px;
    padding:11px 14px;
    display:flex;
    align-items:center;
    gap:10px;
    cursor:pointer;
    font-size:.9rem;
    transition:border-color .2s ease, background .2s ease;
  }
  .shutdown-box.armed {
    border-color:rgba(245,176,65,.6);
    background:rgba(245,176,65,.09);
  }
  .shutdown-box input[type=checkbox] {
    width:19px; height:19px; accent-color:var(--amber); flex-shrink:0; cursor:pointer;
  }

  .btn-row { display:flex; flex-wrap:wrap; gap:10px; margin-top:12px; }
  button.action {
    border:none; border-radius:10px; padding:12px 16px;
    font-weight:700; font-size:.94rem; cursor:pointer; color:#fff;
    flex:1; min-width:165px; transition:opacity .15s ease, transform .08s ease;
  }
  button.action:active { transform:scale(.99); }
  button.action:disabled { opacity:.42; cursor:not-allowed; }
  button.primary { background:linear-gradient(135deg,#2575fc,#3b8dff); }
  button.stop { background:linear-gradient(135deg,#d9383a,#f25f5c); }
  button.warn { background:var(--amber); color:#0b0f17; }

  .chart-box {
    width:100%; height:235px; background:var(--card2);
    border-radius:11px; border:1px solid var(--border); padding:10px;
  }
  .legend { display:flex; flex-wrap:wrap; gap:14px; font-size:.8rem; color:var(--muted); margin-bottom:8px; }
  .dot { display:inline-block; width:10px; height:10px; border-radius:50%; margin-right:5px; vertical-align:middle; }

  .weights-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(175px,1fr)); gap:8px; }
  .wchip {
    background:var(--card2); border:1px solid var(--border); border-radius:9px;
    padding:8px 10px; display:flex; justify-content:space-between; align-items:center; gap:8px;
  }
  .wchip .wname { font-size:.8rem; color:#d8e3f2; font-weight:600; }
  .wchip .wcode { font-size:.7rem; color:var(--muted); font-family:ui-monospace,Consolas,monospace; }
  .wchip strong { font-size:.95rem; color:var(--accent); font-variant-numeric:tabular-nums; }

  .logs {
    background:#080c12; border:1px solid var(--border); border-radius:10px;
    padding:10px 12px; max-height:220px; overflow-y:auto;
    font-family:ui-monospace,Consolas,monospace; font-size:.79rem; color:#cdd7e5;
  }
  .logs div { padding:3px 0; border-bottom:1px solid #141c29; }
</style>
</head>
<body>
<div class="wrap">
  <!-- Header -->
  <div class="card header">
    <div>
      <div class="title-row">
        <div class="title">RubikStudioPro · Bộ Train AI Trên PC</div>
        <span id="statePill" class="pill">SẴN SÀNG</span>
      </div>
      <div class="sub" id="statusMsg">Đang kết nối tới máy chủ...</div>
    </div>
    <div class="link-box">
      <div class="link-chip">
        <span>Máy tính:</span>
        <code id="localUrl">-</code>
      </div>
      <div class="link-chip">
        <span>Điện thoại (Wi-Fi):</span>
        <code id="lanUrl">-</code>
        <button class="copy-btn" onclick="copyLan()">Copy</button>
      </div>
    </div>
  </div>

  <!-- Khung Điều Khiển (Form dễ thao tác trên cả ĐT & PC) -->
  <div class="card">
    <div class="card-head">
      <h2>Cấu Hình &amp; Điều Khiển Phiên Train</h2>
      <span class="badge goal" id="threadsHead">Đang đọc CPU...</span>
    </div>
    <div class="form-grid">
      <div class="field">
        <label for="presetSel">Chọn nhanh thời lượng</label>
        <select id="presetSel" onchange="document.getElementById('inHours').value=this.value">
          <option value="5.0" selected>5 giờ · Train qua đêm</option>
          <option value="3.0">3 giờ · Mức vừa</option>
          <option value="1.0">1 giờ · Train nhanh</option>
          <option value="0.05">3 phút · Chạy thử nhanh</option>
        </select>
        <span class="hint">Tự điền vào ô số giờ bên cạnh</span>
      </div>
      <div class="field">
        <label for="inHours">Số giờ chạy tối đa</label>
        <input id="inHours" type="number" step="0.1" min="0.02" max="24" value="5.0">
        <span class="hint">VD: 5.0 = tự chạy đủ 5 tiếng</span>
      </div>
      <div class="field">
        <label for="inThreads">Số luồng CPU sử dụng</label>
        <input id="inThreads" type="number" min="1" max="16" value="4">
        <span class="hint">Mặc định tự chọn theo máy</span>
      </div>
      <div class="field">
        <label for="inVal">Số cục kiểm thử cố định</label>
        <input id="inVal" type="number" min="8" max="200" value="40">
        <span class="hint">Đề thi cố định để chống học ảo</span>
      </div>
      <div class="field">
        <label for="inBatch">Số cục học mỗi vòng</label>
        <input id="inBatch" type="number" min="4" max="64" value="16">
        <span class="hint">Đề ngẫu nhiên sinh mới từng vòng</span>
      </div>
    </div>

    <label class="shutdown-box" id="sdWrap">
      <input id="chkShutdown" type="checkbox" onchange="toggleShutdown(this.checked)">
      <span><strong>Tự tắt máy tính Windows</strong> (đếm ngược 60 giây) sau khi train chạy đủ giờ và lưu file thành công</span>
    </label>

    <div class="btn-row">
      <button id="btnStart" class="action primary" onclick="startTrain()">▶ Bắt Đầu Train</button>
      <button id="btnStop" class="action stop" onclick="stopTrain()" disabled>■ Dừng &amp; Lưu File Ngay</button>
      <button id="btnAbortSd" class="action warn" onclick="abortShutdown()" style="display:none">⚠ Hủy Lệnh Tắt Máy</button>
    </div>
    <div class="sub" id="ctrlNote" style="margin-top:8px"></div>
  </div>

  <!-- Kết quả & Tiến độ -->
  <div class="grid">
    <div class="card">
      <div class="card-head">
        <h2>Kết Quả Rubik 4x4</h2>
        <span class="badge" id="badge4">Trước: 177.1 → Mục tiêu ≤ 75</span>
      </div>
      <div class="big" id="len4">-</div>
      <div class="sub" id="meta4">Chờ đo mốc ban đầu...</div>
      <div class="row"><span>Tỷ lệ giải đúng</span><strong id="succ4">-</strong></div>
      <div class="row"><span>Giảm so với thuật toán cũ</span><strong class="ok" id="drop4">-</strong></div>
    </div>

    <div class="card">
      <div class="card-head">
        <h2>Kết Quả Rubik 5x5</h2>
        <span class="badge" id="badge5">Trước: 456.7 → Mục tiêu ≤ 120</span>
      </div>
      <div class="big" id="len5">-</div>
      <div class="sub" id="meta5">Chờ đo mốc ban đầu...</div>
      <div class="row"><span>Tỷ lệ giải đúng</span><strong id="succ5">-</strong></div>
      <div class="row"><span>Giảm so với thuật toán cũ</span><strong class="ok" id="drop5">-</strong></div>
    </div>

    <div class="card">
      <div class="card-head">
        <h2>Tiến Độ &amp; Phần Cứng</h2>
        <span class="badge" id="threadsLbl">-</span>
      </div>
      <div class="row"><span>Giai đoạn A (Sinh 9 bảng BFS)</span><strong id="bfsPct">0%</strong></div>
      <div class="bar-bg"><div class="bar-fill" id="bfsBar" style="width:0%"></div></div>
      <div class="row"><span>Giai đoạn B (Thời gian đã train)</span><strong id="timePct">0%</strong></div>
      <div class="bar-bg"><div class="bar-fill" id="timeBar" style="width:0%"></div></div>
      <div class="row"><span>Vòng học / Số lần cải thiện</span><strong id="epLbl">0 / 0</strong></div>
      <div class="row"><span>Đã chạy / Còn lại</span><strong id="etaLbl">0 giây / -</strong></div>
      <div class="row"><span>RAM đang dùng / Mã CRC</span><strong id="ramLbl">-</strong></div>
    </div>
  </div>

  <!-- Biểu đồ -->
  <div class="card">
    <div class="card-head">
      <h2>Biểu Đồ Quá Trình Giảm Số Bước</h2>
      <span class="badge" id="histMeta">0 điểm dữ liệu</span>
    </div>
    <div class="legend">
      <span><i class="dot" style="background:var(--purple)"></i>Rubik 5x5 (ngưỡng mục tiêu ≤ 120)</span>
      <span><i class="dot" style="background:var(--green)"></i>Rubik 4x4 (ngưỡng mục tiêu ≤ 75)</span>
      <span>● Điểm tròn = tìm thấy bộ tham số tốt hơn &amp; đã lưu</span>
    </div>
    <div class="chart-box"><svg id="chartSvg" viewBox="0 0 800 210" width="100%" height="100%" preserveAspectRatio="none"></svg></div>
  </div>

  <!-- Trọng số đang học -->
  <div class="grid-2">
    <div class="card">
      <div class="card-head"><h2>Tham Số Đang Học · 4x4</h2></div>
      <div class="weights-grid" id="w4Grid"></div>
    </div>
    <div class="card">
      <div class="card-head"><h2>Tham Số Đang Học · 5x5</h2></div>
      <div class="weights-grid" id="w5Grid"></div>
    </div>
  </div>

  <!-- Nhật ký -->
  <div class="card">
    <div class="card-head">
      <h2>Nhật Ký Train Trực Tiếp (Mới Nhất Ở Trên)</h2>
    </div>
    <div class="logs" id="logBox"></div>
  </div>
</div>

<script>
const STATE_VI = {
  idle: ['SẴN SÀNG', ''],
  bfs_tables: ['ĐANG TẠO BẢNG BFS', 'bfs'],
  training: ['ĐANG TRAIN', 'run'],
  validating: ['ĐANG KIỂM THỬ CHUẨN', 'run'],
  completed: ['HOÀN TẤT', 'done'],
  stopped: ['ĐÃ DỪNG AN TOÀN', 'done'],
  error: ['CÓ LỖI', 'err'],
  reading: ['ĐANG ĐỒNG BỘ', 'bfs'],
};

const WEIGHT_LABELS = {
  wPairedEdge: 'Điểm ghép xong cạnh',
  wHalfPair5x5: 'Điểm ghép nửa cạnh',
  wUDParked: 'Ưu tiên cất lên U/D',
  wSetup1Away: 'Thưởng ghép chuỗi 1 bước',
  wMoveCost: 'Phạt độ dài bước đi',
  wEqMisaligned: 'Phạt lệch tâm giữa',
  wBadFlip: 'Phạt hướng cạnh xấu',
  beamWidthFreeslice: 'Độ rộng tìm kiếm',
  centerTopK: 'Số phương án tâm',
};

const fmtTime = s => {
  s = Math.max(0, Math.floor(s || 0));
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  if (h > 0) return `${h} giờ ${String(m).padStart(2,'0')} phút`;
  if (m > 0) return `${m} phút ${String(sec).padStart(2,'0')} giây`;
  return `${sec} giây`;
};

function translateEngineMsg(msg, fallback) {
  if (!msg) return fallback || 'Sẵn sàng bắt đầu.';
  if (msg.startsWith('BFS ')) return 'Đang xây dựng bảng tìm kiếm nhanh ' + msg.slice(4);
  if (msg.startsWith('Evaluating baseline')) return 'Đang đo điểm mốc ban đầu trên tập kiểm thử WCA...';
  if (msg.startsWith('Epoch ')) {
    const ep = msg.match(/Epoch (\d+)/)?.[1] || '?';
    if (msg.includes('validating')) return `Vòng ${ep}: Tìm thấy tham số hứa hẹn, đang chấm lại trên toàn bộ đề chuẩn...`;
    return `Vòng ${ep}: Đang chạy thử nghiệm và so sánh bộ tham số mới...`;
  }
  if (msg.includes('completed')) return 'Đã hoàn tất phiên train và lưu bộ tham số tốt nhất vào CoreCpp/Common/TrainedPayload.hpp.';
  if (msg.includes('stopped cleanly')) return 'Đã dừng an toàn và lưu kết quả tốt nhất vào CoreCpp/Common/TrainedPayload.hpp.';
  if (msg.includes('Phase A tables generated')) return 'Đã sinh và kiểm tra xong 9 bảng BFS.';
  return msg;
}

function renderWeights(elId, w) {
  const el = document.getElementById(elId);
  if (!w) { el.innerHTML = '<div class="sub">Chưa có dữ liệu trọng số.</div>'; return; }
  el.innerHTML = Object.entries(w).map(([k, v]) => {
    const label = WEIGHT_LABELS[k] || k;
    const val = typeof v === 'number' ? (Number.isInteger(v) ? v : v.toFixed(2)) : v;
    return `<div class="wchip"><div><div class="wname">${label}</div><div class="wcode">${k}</div></div><strong>${val}</strong></div>`;
  }).join('');
}

function renderChart(history) {
  const svg = document.getElementById('chartSvg');
  document.getElementById('histMeta').textContent = (history?.length || 0) + ' điểm dữ liệu';
  if (!history || history.length === 0) {
    svg.innerHTML = '<text x="400" y="110" fill="#93a4bc" font-size="14" text-anchor="middle">Chưa có điểm dữ liệu — Bấm Bắt Đầu Train để vẽ đường cong học</text>';
    return;
  }
  const W=800, padL=48, padR=16, padT=18;
  const xAt = i => history.length === 1 ? (padL + W - padR)/2 : padL + (i / (history.length - 1)) * (W - padL - padR);
  const y5 = v => padT + (1 - Math.min(1, Math.max(0, (v - 90) / 60))) * 76;
  const y4 = v => 112 + (1 - Math.min(1, Math.max(0, (v - 50) / 50))) * 76;

  const p5 = history.map((h, i) => `${xAt(i).toFixed(1)},${y5(h.m5).toFixed(1)}`).join(' ');
  const p4 = history.map((h, i) => `${xAt(i).toFixed(1)},${y4(h.m4).toFixed(1)}`).join(' ');
  const g5 = y5(120).toFixed(1), g4 = y4(75).toFixed(1);

  let dots = '';
  history.forEach((h, i) => {
    if (h.acc) {
      dots += `<circle cx="${xAt(i).toFixed(1)}" cy="${y5(h.m5).toFixed(1)}" r="4" fill="#b78bfa"/>`;
      dots += `<circle cx="${xAt(i).toFixed(1)}" cy="${y4(h.m4).toFixed(1)}" r="4" fill="#2fd168"/>`;
    }
  });

  const last = history[history.length - 1];
  svg.innerHTML = `
    <line x1="${padL}" y1="${g5}" x2="${W-padR}" y2="${g5}" stroke="#b78bfa" stroke-dasharray="4 4" opacity="0.45"/>
    <text x="4" y="${Number(g5)+4}" fill="#b78bfa" font-size="10">5x5 ≤120</text>
    <line x1="${padL}" y1="${g4}" x2="${W-padR}" y2="${g4}" stroke="#2fd168" stroke-dasharray="4 4" opacity="0.45"/>
    <text x="4" y="${Number(g4)+4}" fill="#2fd168" font-size="10">4x4 ≤75</text>
    <polyline fill="none" stroke="#b78bfa" stroke-width="2.6" points="${p5}"/>
    <polyline fill="none" stroke="#2fd168" stroke-width="2.6" points="${p4}"/>
    ${dots}
    <text x="${W-padR}" y="15" fill="#b78bfa" font-size="12" font-weight="700" text-anchor="end">5x5 tốt nhất: ${last.m5.toFixed(2)} bước</text>
    <text x="${W-padR}" y="202" fill="#2fd168" font-size="12" font-weight="700" text-anchor="end">4x4 tốt nhất: ${last.m4.toFixed(2)} bước</text>
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
      const defThreads = Math.max(1, Math.min(8, c.cpu_cores));
      document.getElementById('inThreads').value = defThreads;
      document.getElementById('threadsHead').textContent = `Máy có ${c.cpu_cores} luồng CPU`;
      firstLoad = false;
    }

    const st = t.state || (c.running ? 'training' : 'idle');
    const [stText, stCls] = STATE_VI[st] || [st.toUpperCase(), ''];
    const pill = document.getElementById('statePill');
    pill.textContent = stText;
    pill.className = 'pill ' + stCls;

    document.getElementById('statusMsg').textContent = translateEngineMsg(t.message, c.note);
    document.getElementById('ctrlNote').textContent = c.note || '';

    document.getElementById('btnStart').disabled = !!c.running;
    document.getElementById('btnStop').disabled = !c.running;
    document.getElementById('chkShutdown').checked = !!c.auto_shutdown;
    document.getElementById('sdWrap').classList.toggle('armed', !!c.auto_shutdown);
    document.getElementById('btnAbortSd').style.display = c.shutdown_triggered ? 'inline-block' : 'none';

    const b4 = t.best_4x4 || {}, b5 = t.best_5x5 || {};
    document.getElementById('len4').textContent = b4.count ? b4.len_mean.toFixed(2) + ' bước' : '-';
    document.getElementById('meta4').textContent = b4.count ? `95% ca dưới: ${b4.len_p95} | Tối đa: ${b4.len_max} | Tốc độ: ${b4.ms_p50.toFixed(0)} ms` : 'Chờ đo mốc ban đầu...';
    document.getElementById('succ4').textContent = b4.count ? `${b4.solved}/${b4.count} (${(b4.success*100).toFixed(1)}%)` : '-';
    document.getElementById('drop4').textContent = b4.count ? `Giảm ${((1 - b4.len_mean/177.1)*100).toFixed(1)}%` : '-';
    const bg4 = document.getElementById('badge4');
    if (b4.count && b4.len_mean <= 75.0) { bg4.textContent = 'Đạt chuẩn ≤ 75'; bg4.className = 'badge pass'; }
    else { bg4.textContent = 'Trước: 177.1 → Mục tiêu ≤ 75'; bg4.className = 'badge'; }

    document.getElementById('len5').textContent = b5.count ? b5.len_mean.toFixed(2) + ' bước' : '-';
    document.getElementById('meta5').textContent = b5.count ? `95% ca dưới: ${b5.len_p95} | Tối đa: ${b5.len_max} | Tốc độ: ${b5.ms_p50.toFixed(0)} ms` : 'Chờ đo mốc ban đầu...';
    document.getElementById('succ5').textContent = b5.count ? `${b5.solved}/${b5.count} (${(b5.success*100).toFixed(1)}%)` : '-';
    document.getElementById('drop5').textContent = b5.count ? `Giảm ${((1 - b5.len_mean/456.7)*100).toFixed(1)}%` : '-';
    const bg5 = document.getElementById('badge5');
    if (b5.count && b5.len_mean <= 120.0) { bg5.textContent = 'Đạt chuẩn ≤ 120'; bg5.className = 'badge pass'; }
    else { bg5.textContent = 'Trước: 456.7 → Mục tiêu ≤ 120'; bg5.className = 'badge'; }

    const bfsPct = Number(t.tables_pct || 0);
    const tgtSec = Number(t.target_hours || 0) * 3600;
    const elSec = Number(t.elapsed_sec || 0);
    const tPct = tgtSec > 0 ? Math.min(100, (elSec / tgtSec) * 100) : 0;
    document.getElementById('threadsLbl').textContent = (t.threads || c.cpu_cores || '-') + ' luồng CPU';
    document.getElementById('bfsPct').textContent = bfsPct.toFixed(1) + '% (' + (t.tables_built||0) + '/9 bảng)';
    document.getElementById('bfsBar').style.width = bfsPct + '%';
    document.getElementById('timePct').textContent = tPct.toFixed(1) + '%';
    document.getElementById('timeBar').style.width = tPct + '%';
    document.getElementById('epLbl').textContent = `Vòng ${t.epoch || 0} / ${t.accepted_updates || 0} lần lưu`;
    document.getElementById('etaLbl').textContent = `${fmtTime(elSec)} / ${t.eta_sec > 0 ? fmtTime(t.eta_sec) : '0 giây'}`;
    document.getElementById('ramLbl').textContent = `${(t.ram_mb||0).toFixed(1)} MB / ${t.checksum || '-'}`;

    renderChart(t.history || []);
    renderWeights('w4Grid', t.weights_4x4);
    renderWeights('w5Grid', t.weights_5x5);

    const logBox = document.getElementById('logBox');
    logBox.innerHTML = (t.logs || []).slice(-30).reverse().map(l => `<div>${l.replace(/</g,'&lt;')}</div>`).join('');
  } catch (e) {}
}

async function copyLan() {
  const txt = document.getElementById('lanUrl').textContent;
  if (!txt || txt === '-') return;
  try {
    await navigator.clipboard.writeText(txt);
    document.getElementById('ctrlNote').textContent = 'Đã copy link điện thoại: ' + txt;
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
            raw = json.dumps(obj, ensure_ascii=False).encode("utf-8")
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
                self._send_json(404, {"error": "Không tìm thấy đường dẫn"})

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
                self._send_json(404, {"error": "Không tìm thấy đường dẫn"})

    return Handler


def main():
    parser = argparse.ArgumentParser(description="RubikStudioPro Overnight PC Trainer Dashboard")
    parser.add_argument("--host", default="0.0.0.0", help="Host lắng nghe (0.0.0.0 để điện thoại cùng Wi-Fi truy cập được)")
    parser.add_argument("--port", type=int, default=8765, help="Cổng HTTP (mặc định 8765)")
    args = parser.parse_args()

    ctrl = TrainerController(args.port)
    srv = ThreadingHTTPServer((args.host, args.port), make_handler(ctrl))
    lan = get_lan_ip()
    print("Dashboard Train RubikStudioPro đang chạy:")
    print(f"  Trên máy tính : http://localhost:{args.port}")
    print(f"  Trên điện thoại: http://{lan}:{args.port}  (cùng mạng Wi-Fi/LAN)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nĐã tắt máy chủ Dashboard.")
        srv.shutdown()


if __name__ == "__main__":
    main()
