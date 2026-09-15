# CLAUDE.md

Quy tắc làm việc + chỉ dẫn dự án nằm trong **`AGENTS.md`** (nguồn chính, chuẩn đa-tool).
Đọc file đó trước — đặc biệt **§0 (đây là thiết bị chẩn đoán)** và **§1 (bất biến)**.

Tài liệu:
- `docs/SAFETY.md` — hard-limit nhiệt, fail-safe. **Đọc trước khi chạm bất cứ thứ gì liên quan gia nhiệt.**
- `docs/ARCHITECTURE.md` — ranh giới module; tại sao logic an toàn phải test được trên host.
- `docs/CONVENTIONS.md` — quy ước code bắt buộc.
- `docs/HARDWARE.md` — pin map, sensor, heater.
- `docs/OTA.md` — verify sha256 + cert + pcb_version, rollback.
- `docs/TESTING.md` — native vs on-device vs máy thật.
- `docs/RELEASE.md` — quy trình phát hành + checklist.
- `docs/PROGRESS.md` — việc đang dở + bài học đã verify.
