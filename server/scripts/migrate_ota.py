"""Xem trước / chạy di cư kho OTA phẳng (trước 2026-09-11) → products/<LEGACY_PRODUCT>/.

Server TỰ di cư lúc khởi động (app/main.py gọi ota.migrate_legacy) nên script này chỉ
để XEM TRƯỚC trên box trước khi restart, hoặc chạy tay khi muốn thấy kết quả ngay:

    cd ~/fbt_server && python3 -m scripts.migrate_ota --dry-run   # chỉ in, không đụng file
    cd ~/fbt_server && python3 -m scripts.migrate_ota             # dời thật

Đọc cùng biến môi trường với service (FBT_OTA_DIR, OTA_LEGACY_PRODUCT) — chạy với
`sudo -E` hoặc export tay nếu env nằm trong /etc/fbt-receiver.env. Idempotent.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app import config, ota  # noqa: E402


def main(argv: list[str]) -> int:
    dry = "--dry-run" in argv
    print(f"OTA_DIR = {config.OTA_DIR}  ·  LEGACY_PRODUCT = {config.LEGACY_PRODUCT}"
          f"{'  ·  DRY-RUN' if dry else ''}")
    actions = ota.migrate_legacy(dry_run=dry)
    if not actions:
        print("gốc kho đã sạch — không có gì để di cư")
        return 0
    for a in actions:
        print(("sẽ: " if dry else "đã: ") + a)
    if any(".conflict" in a or ".migrated" in a for a in actions):
        print("⚠️  có mục cần xử lý tay (xem dòng .conflict / .migrated ở trên)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
