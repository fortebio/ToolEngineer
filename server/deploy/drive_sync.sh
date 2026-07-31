#!/bin/bash
# Đồng bộ log Google Drive về kho BACKUP (bảng drive_sessions) — TÁCH khỏi device POST (sessions).
# rclone copy chỉ tải file .txt mới; import dedup theo nội dung.
# Chạy tay:  bash ~/fbt_server/drive_sync.sh   | Tự động: systemd timer fbt-drive-sync
set -euo pipefail
INBOX="$HOME/drive_backup_inbox"
mkdir -p "$INBOX"

# remote 'data' (Google Drive, đã trỏ vào folder FBT). Kéo file .txt mới:
rclone copy data: "$INBOX" --include "*.txt" --transfers 8

~/fbt_server/venv/bin/python ~/fbt_server/scripts/import_drive_backup.py "$INBOX"
