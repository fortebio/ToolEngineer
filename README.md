# ToolEngineer - he thong san pham Forte Biotech (monorepo)

Mot repo cho ca ba tang: **server** (Engineer Server, FastAPI) - **client** (app FBT_RAPID, Flutter
desktop + web) - **thiet bi** (firmware ESP32 tung san pham). Nguon su that ve san pham:
`system/products.yaml`. Ban do va quy tac: `CLAUDE.md`; phuong an gom: `docs/plan/monorepo-mot-he-thong.md`.

| Thu muc | Noi dung |
|---|---|
| `system/` | registry san pham + hop dong du lieu (JSON Schema) |
| `server/` | Engineer Server (FastAPI + Postgres) - deploy len box `hub.fortebio.tech` |
| `apps/fbt_rapid/` | App FBT_RAPID (Flutter) - desktop Windows + web `/app/` |
| `firmware/rapidplus/` | Firmware Forte Rapid+ (PlatformIO, ESP32) |
| `firmware/rapidplus-prod/` | Firmware Rapid+ viet lai theo IEC 62304 (dev) |
| `firmware/reader/` | Firmware Forte Rapid Reader |
| `legacy/` | Apps Script (getData.js CON SONG cho fleet cu), port Cloudflare Workers chua deploy |
| `tools/` | registry_check.py, script lap monorepo |
| `docs/` | tai lieu xuyen phan (plan, history); tai lieu tung phan nam trong thu muc cua no |