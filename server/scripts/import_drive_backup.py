#!/usr/bin/env python3
"""Nạp log Google Drive vào kho BACKUP (bảng drive_sessions) — TÁCH khỏi 'sessions' (device POST).

received_at: payload 'time' > tên file > now(). Dedup theo nội dung (idempotent, chạy lại an toàn).
Chạy:  ~/fbt_server/venv/bin/python scripts/import_drive_backup.py <thư_mục>
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from app.db import insert_drive_session
from app.logic import parse_ts, payload_time


def main(folder: str) -> int:
    ins = dup = skip = 0
    for p in sorted(Path(folder).rglob("*.txt")):
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError, OSError):
            skip += 1
            continue
        if not (isinstance(data, dict) and str(data.get("id_device", "")).strip()):
            skip += 1
            continue
        ts = payload_time(data) or parse_ts(p.name)
        try:
            if insert_drive_session(data, received_at=ts):
                ins += 1
            else:
                dup += 1
        except Exception as e:  # 1 file lỗi DB không chặn phần còn lại
            print(f"SKIP {p.name}: {e}")
            skip += 1
    print(f"inserted={ins} duplicate={dup} skipped={skip}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
