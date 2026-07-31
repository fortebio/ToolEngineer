#!/usr/bin/env python3
"""Nạp bù file JSON trong data_plus/ vào Postgres — idempotent nhờ dedup theo nội dung (dedup=True),
chạy lại bao nhiêu lần cũng không tạo bản ghi trùng.

Chạy trên server:  ~/fbt_server/venv/bin/python scripts/reconcile.py
Dùng khi: sau khoảng thời gian DB down mà thiết bị vẫn POST (file đã ghi nhưng chưa vào DB).
"""
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))  # để import package app
from app.config import DATA_DIR
from app.db import insert_session
from app.logic import validate


def main() -> int:
    inserted = duplicate = skipped = 0
    for p in sorted(DATA_DIR.glob("*.json")):
        try:
            data = json.loads(p.read_bytes())
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            print(f"SKIP {p.name}: invalid json: {e}")
            skipped += 1
            continue
        err = validate(data)
        if err:
            print(f"SKIP {p.name}: {err}")
            skipped += 1
            continue
        # ponytail: received_at lấy theo mtime file — đủ đúng cho backfill, khỏi parse tên file
        ts = datetime.fromtimestamp(p.stat().st_mtime, tz=timezone.utc)
        try:
            sid = insert_session(data, received_at=ts, dedup=True)
        except Exception as e:  # 1 file hỏng (jsonb từ chối...) không được chặn các file sau
            print(f"SKIP {p.name}: db error: {e}")
            skipped += 1
            continue
        if sid:
            inserted += 1
        else:
            duplicate += 1
    print(f"inserted {inserted}, duplicate {duplicate}, skipped {skipped}")
    return 0 if skipped == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
