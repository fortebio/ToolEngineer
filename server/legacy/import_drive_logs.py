#!/usr/bin/env python3
"""Nạp 1 lần kho log tải từ Google Drive (thư mục các file .txt) vào PostgreSQL.

received_at lấy từ THỜI ĐIỂM ĐO trong tên file (payload không có mốc thời gian):
  - ISO   : Log_RPL02013-2026-07-08T03_32_22.479Z.txt  -> UTC
  - Khác  : Log_RA-05-05-2025 14_41_21.txt              -> giờ VN (DD-MM-YYYY)
Trùng nội dung (kể cả bản 'Copy of') tự loại nhờ body_sha256 UNIQUE.

Chạy trên server:  ~/fbt_server/venv/bin/python scripts/import_drive_logs.py <thư_mục>
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # để import package app
from app.db import insert_session
from app.logic import parse_ts, payload_time  # thời điểm đo: payload 'time' > tên file


def main(folder: str) -> int:
    ins = dup = skip = nots = 0
    for p in sorted(Path(folder).rglob("*.txt")):
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError, OSError):
            skip += 1
            continue
        if not (isinstance(data, dict) and str(data.get("id_device", "")).strip()):
            skip += 1
            continue
        ts = payload_time(data) or parse_ts(p.name)  # ưu tiên 'time' trong payload, rồi tên file
        if ts is None:
            nots += 1
        try:
            if insert_session(data, received_at=ts, dedup=True):
                ins += 1
            else:
                dup += 1
        except Exception as e:  # 1 file lỗi DB không chặn phần còn lại
            print(f"SKIP {p.name}: {e}")
            skip += 1
    print(f"inserted={ins} duplicate={dup} skipped={skip} no_timestamp={nots}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
