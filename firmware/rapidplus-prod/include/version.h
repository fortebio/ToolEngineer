// version.h — phiên bản firmware.
#pragma once

// PHẢI khớp git tag khi phát hành (vX.Y.Z). CI chặn nếu lệch — cloud đọc version
// từ binary làm nguồn canonical. Xem docs/RELEASE.md.
#define FBT_FW_VERSION "0.1.0"

// PCB version mà bản build này hỗ trợ. OTA từ chối nếu máy đích không khớp —
// nạp nhầm firmware cho PCB khác = hỏng phần cứng. Xem docs/OTA.md.
#define FBT_PCB_VERSION "1.3"

// Model thiết bị, gửi lên cloud trong telemetry.
#define FBT_DEVICE_MODEL "RapidPlus"
