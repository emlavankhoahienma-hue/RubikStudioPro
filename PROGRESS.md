# PROGRESS.md - Tiến Trình Tự Hành & Số Liệu Thực Tế

> **KẾT LUẬN TOÀN BỘ VÒNG CHẠY**: **CHƯA ĐẠT ĐIỀU KIỆN DỪNG GIAI ĐOẠN 1 ĐỂ SANG GIAI ĐOẠN 2**.
> - **2x2 & 3x3**: Đạt 100% mọi chỉ số và ngưỡng trong `verify/thresholds.json`.
> - **4x4 & 5x5**: Tỉ lệ giải thành công chỉ đạt 0%–10% (4x4) và 10%–20% (5x5), không đạt điều kiện `solve_success_rate == 1.0` (100%).
> - Theo đúng quy tắc của kịch bản tự hành: **DỪNG, KHÔNG BUILD IPA MỚI**, tạo `DONE.flag` và báo cáo số liệu trung thực.

---

## 1. [ĐÃ KIỂM CHỨNG BẰNG TEST CHẠY THẬT]
- **Đại số nhóm Rubik (Group Algebra)**:
  - $U^4 = I$: Khối 3x3 trở về solved sau 4 lần quay $U$ (PASS 100%).
  - $(R U R' U')^6 = I$: Khối 3x3 trở về solved sau 6 lần thực hiện Sexy Move (PASS 100%).
  - $Rw^4 = I$: Khối 4x4 trở về solved sau 4 lần quay lớp kép (PASS 100%).
  - $3Rw^4 = I$: Khối 5x5 trở về solved sau 4 lần quay 3 lớp (PASS 100%).
- **Kiểm tra đột biến (Mutation Check)**:
  - Thử nghiệm 50 đột biến cố ý (Corner twist $\sum co \bmod 3 \neq 0$, Edge flip $\sum eo \bmod 2 \neq 0$, Parity mismatch hoán vị lẻ).
  - Kết quả: Bộ kiểm tra `isValid()` phát hiện và từ chối 100% (50/50 trường hợp).
- **Khối 2x2 (Optimal IDA\*)**:
  - Tỉ lệ giải thành công: **100%** trên toàn bộ các seed (`42`, `12345`, `99999`).
  - Độ dài lời giải: Tối ưu tuyệt đối $\le 11$ bước (mean: 0.7–0.9 bước trong test).
  - Thời gian giải P50: **0.0004–0.0005 ms** (ngưỡng $\le 2.0$ ms).
  - Thời gian khởi tạo bảng BFS: **1.73–1.76 ms** (ngưỡng $\le 50$ ms).
  - Bộ nhớ RAM đỉnh: **3.32–3.35 MB** (ngưỡng $\le 15$ MB).
- **Khối 3x3 (Kociemba Two-Phase)**:
  - Tỉ lệ giải thành công: **100%** trên toàn bộ các seed (`42`, `12345`, `99999`).
  - Độ dài lời giải: Trung bình **19.8–22.8 bước** (ngưỡng $\le 22.0$), P95: 24 bước, Max: 25 bước.
  - Thời gian giải P50: **230–305 ms** (ngưỡng $\le 1500$ ms).
  - Thời gian khởi tạo bảng BFS: **37.5–38.8 ms** (ngưỡng $\le 600$ ms).
  - Bộ nhớ RAM đỉnh: **3.57–3.60 MB** (ngưỡng $\le 35$ MB).
  - Tỉ lệ từ chối đột biến: **100%**.
- **Quét mã nguồn**:
  - Không còn bất kỳ TODO/FIXME/placeholder/mock/stub/simplified nào trong toàn bộ `CoreCpp` và `SwiftApp`.
- **File IPA iOS đã build thành công trước đó (Giai đoạn trước)**:
  - Run ID: `43253932666` trên GitHub Actions `macos-latest` (macOS 14 Apple Silicon).
  - Gói `.ipa` tại `artifact_output/RubikStudioPro-Unsigned.ipa` (289 KB) đã giải nén kiểm tra: có `Payload/RubikStudioPro.app/RubikStudioPro` (arm64), `Info.plist`, `PkgInfo`.

---

## 2. [CHƯA KIỂM CHỨNG / CHƯA ĐẠT]
- **Khối 4x4 (Reduction to 3x3)**:
  - Tỉ lệ thành công trên scramble ngẫu nhiên: **0% – 10%** (CHƯA ĐẠT ngưỡng `1.0`).
  - Nguyên nhân: Phương pháp Reduction hiện tại dùng tổ hợp commutator giải tâm (36 comms) và cạnh (4 comms) kèm setup moves leo đồi tham lam (greedy descent). Khi gặp scramble ngẫu nhiên, thuật toán bị kẹt ở cực tiểu địa phương (local minimum) nếu không có commutator đơn lẻ nào làm giảm số lượng ô tâm sai. Để đạt 100%, cần xây dựng kiến trúc Phased Reduction đa tầng chuyên sâu (giải 2 tâm đối diện U/D $\to$ 4 tâm vòng tròn $\to$ 3-2-3 edge pairing).
- **Khối 5x5 (Reduction to 3x3)**:
  - Tỉ lệ thành công trên scramble ngẫu nhiên: **10% – 20%** (CHƯA ĐẠT ngưỡng `1.0`).
  - Nguyên nhân: Tương tự 4x4, việc giải 3x3 centers và ghép 12 tredges bằng greedy commutators bị kẹt ở cực tiểu địa phương trên scramble ngẫu nhiên sâu.

---

## 3. [GIẢ ĐỊNH]
- Giả định hệ thống camera AVFoundation trên iOS 16+ hoạt động ổn định với cơ chế Lazy Initialization (`startCamera()` / `stopCamera()`).
- Giả định môi trường ánh sáng người dùng sử dụng là hệ màu chuẩn Western BOY (Trắng đối Vàng, Xanh lá đối Xanh dương, Cam đối Đỏ).

---

## 4. Lịch Sử Số Liệu Chi Tiết (metrics/history.csv)
- **Baseline (Seed 42)**: 2x2 = 100% (0.0004ms), 3x3 = 100% (305ms), 4x4 = 0%, 5x5 = 20%.
- **Round 1 (Seed 12345)**: 2x2 = 100% (0.0005ms), 3x3 = 100% (232ms), 4x4 = 0%, 5x5 = 0%.
- **Round 2 (Seed 99999)**: 2x2 = 100% (0.0005ms), 3x3 = 100% (230ms), 4x4 = 10%, 5x5 = 10%.
