/**
 * asset_pack.h — Download + manage premium asset pack từ server.
 *
 * Flow:
 *   1. GET /api/devices/:macId/asset-pack → { id, version, manifest_url, zip_url }
 *   2. So sánh version với asset_pack_version() trong NVS
 *   3. Khác → download zip → unzip vào /spiffs/asset/
 *   4. Update NVS asset_pack_id + version
 *
 * Trên spiffs cấu trúc:
 *   /spiffs/asset/emo/<name>.png   (21 emotion sprites)
 *   /spiffs/asset/sfx/<name>.opus  (reward / wake sound)
 *   /spiffs/asset/manifest.json
 *
 * Storage budget: ~3 MB partition `asset_spiffs` (xem partitions.csv).
 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t asset_pack_init(void);          /* mount spiffs partition "asset" */
void      asset_pack_sync_async(void);    /* spawn task → kiểm tra + download */
bool      asset_pack_has_file(const char *relpath); /* e.g. "/emo/happy.png" */
const char *asset_pack_version(void);

#ifdef __cplusplus
}
#endif
