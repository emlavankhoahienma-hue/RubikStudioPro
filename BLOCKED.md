# BLOCKED.md - Nhật Ký Trở Ngại & Đề Xuất Kỹ Thuật

> **QUY TẮC BẤT BIẾN**:
> - Các file trong `verify/**` (kể cả `verify/thresholds.json`) và `.github/workflows/verify.yml` bị KHÓA.
> - Tuyệt đối không tự ý nới lỏng ngưỡng (thresholds) hoặc hạ chuẩn test. Mọi đề xuất phải được ghi chép minh bạch tại file này.

---

### 1. Trở Ngại Kỹ Thuật (Blocking Issue)
- **Vấn đề**: Điều kiện dừng Giai đoạn 1 yêu cầu **100% correctness** ở **MỌI CỠ CUBE** (2x2..5x5) theo `verify/thresholds.json`.
- **Hiện trạng kiểm chứng thực tế**:
  - `Cube2x2`: **100% Pass** (Optimal IDA* $\le 11$ bước, $0.0005$ ms).
  - `Cube3x3`: **100% Pass** (Kociemba Two-Phase $19–24$ bước, $\approx 230$ ms).
  - `Cube4x4`: **0% – 10% Pass** (Chưa đạt ngưỡng 100%).
  - `Cube5x5`: **10% – 20% Pass** (Chưa đạt ngưỡng 100%).
- **Nguyên nhân gốc (Root Cause)**:
  - Khối 4x4 và 5x5 không thể giải tối ưu tuyệt đối (NP-hard). Mọi ứng dụng thực tế trên thế giới đều dùng phương pháp Reduction đưa về 3x3.
  - Thuật toán Reduction hiện tại sử dụng phương pháp tìm kiếm tổ hợp leo đồi tham lam (Greedy Commutators). Với các scramble ngẫu nhiên sâu, không gian trạng thái $3.2 \times 10^{15}$ của 24 ô tâm và 24 viên cạnh cánh gặp các điểm cực tiểu địa phương (local minima). Tại đó, không có commutator đơn lẻ nào làm giảm số lượng ô tâm sai, dẫn đến việc thuật toán dừng trước khi tâm và cạnh được ghép xong.
  - Để giải quyết triệt để 100% cho 4x4 và 5x5, cần phát triển bộ giải Phased Reduction công nghiệp hoàn chỉnh (Stage 1: Ghép 2 mặt tâm đối diện U/D $\to$ Stage 2: Ghép 4 mặt tâm vòng quanh $\to$ Stage 3: Kỹ thuật 3-2-3 Edge Pairing $\to$ Stage 4: Parity Fix $\to$ Min2Phase 3x3).
- **Hành động tuân thủ chỉ thị của User**:
  - "Hết 30 vòng mà chưa đạt: DỪNG, KHÔNG build IPA, viết báo cáo 'CHƯA ĐẠT' kèm số liệu vào PROGRESS.md."
  - "Khi hoàn tất hoặc bế tắc, tạo file DONE.flag (kèm lý do trong PROGRESS.md)."
  - Hệ thống dừng vòng lặp, tạo `DONE.flag` và báo cáo minh bạch cho người dùng.
