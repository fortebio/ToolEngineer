# RAPID ERP — External Device API

**Phien ban:** v1.0 | **Ngay:** 2026-06-26  
**Base URL:** `https://api.fortebio.tech/api/v1/results`

---

## Xac thuc (Authentication)

Tat ca request can header `X-API-Key`:

```
X-API-Key: <key do admin cung cap>
```

> Dung chung key voi POST `/ingest` ma firmware dang su dung.

---

## 1. Lay danh sach ket qua theo may

```
GET /external/device/{device_id_raw}/results
```

**Path param:**
| Tham so | Mo ta | Vi du |
|---------|-------|-------|
| `device_id_raw` | Ma may (instrument ID) | `RPL02013` |

**Query params (tuy chon):**
| Tham so | Mac dinh | Mo ta |
|---------|----------|-------|
| `limit` | 50 | So ket qua toi da (1-200) |
| `offset` | 0 | Vi tri bat dau (phan trang) |
| `date_from` | — | Loc tu ngay (ISO datetime) |
| `date_to` | — | Loc den ngay (ISO datetime) |

**Vi du:**

```bash
curl -H "X-API-Key: YOUR_KEY" \
  "https://api.fortebio.tech/api/v1/results/external/device/RPL02013/results?limit=10"
```

**Response:**

```json
{
  "items": [
    {
      "id": "c8acd09c-ccee-4864-a0fa-857c1f6d1834",
      "device_id_raw": "RPL02013",
      "firmware_version": "v2.4.2",
      "kit_id": "0.00",
      "upload_type": "MANUAL",
      "test_timestamp": null,
      "received_at": "2026-06-26T07:47:17.064097+00:00",
      "result_codes": {
        "0": "22.3 | N",
        "1": "23.0 | N",
        "2": "37.0 | N",
        "3": "33.0 | N",
        "4": "33.7 | N",
        "5": "26.0 | N",
        "6": "33.7 | N",
        "7": "26.0 | N",
        "8": "35.3 | N",
        "9": "16.0 | N"
      }
    }
  ],
  "total": 3,
  "limit": 10,
  "offset": 0,
  "device_id_raw": "RPL02013"
}
```

**Giai thich `result_codes`:**
- Key `"0"` den `"9"` = channel index (slot 1-10)
- Gia tri `"22.3 | N"` = CT value 22.3, ket qua **Negative**
- `"15.3 | P"` = CT value 15.3, ket qua **Positive**
- `"! | E"` = **Error**

---

## 2. Chi tiet 1 ket qua

```
GET /external/results/{result_id}/detail
```

**Vi du:**

```bash
curl -H "X-API-Key: YOUR_KEY" \
  "https://api.fortebio.tech/api/v1/results/external/results/c8acd09c-ccee-4864-a0fa-857c1f6d1834/detail"
```

**Response:** Tat ca thong tin 10 channel, bao gom:
- `ct_value`, `result_code`
- `calibration_slope`, `calibration_origin`, `led_power`
- `peak_main`, `peak_right_arm`, `peak_left_arm`
- `transition_time`, `plateau_point`, `increase_value`
- `amplification_data` (mang so lieu khuech dai)
- `raw_payload` (JSON goc tu firmware)

---

## 3. Bieu do khuech dai (PNG)

```
GET /external/results/{result_id}/chart
```

Tra ve file **PNG** — bieu do amplification 10 kenh.

**Vi du:**

```bash
curl -H "X-API-Key: YOUR_KEY" \
  -o chart.png \
  "https://api.fortebio.tech/api/v1/results/external/results/c8acd09c-ccee-4864-a0fa-857c1f6d1834/chart"
```

---

## 4. Bao cao PDF

```
GET /external/results/{result_id}/pdf
```

Tra ve file **PDF** — bao cao day du.

**Query params (tuy chon):**
| Tham so | Mac dinh | Mo ta |
|---------|----------|-------|
| `lang` | theo may | Ngon ngu: `en`, `vi`, `th`, `id`, `zh` |

**Vi du:**

```bash
curl -H "X-API-Key: YOUR_KEY" \
  -o report.pdf \
  "https://api.fortebio.tech/api/v1/results/external/results/c8acd09c-ccee-4864-a0fa-857c1f6d1834/pdf?lang=vi"
```

---

## Tong hop cac Endpoint

| # | Method | Path | Mo ta |
|---|--------|------|-------|
| 1 | GET | `/external/device/{device_id_raw}/results` | Danh sach ket qua theo may |
| 2 | GET | `/external/results/{result_id}/detail` | Chi tiet 1 ket qua (10 channel) |
| 3 | GET | `/external/results/{result_id}/chart` | Bieu do khuech dai PNG |
| 4 | GET | `/external/results/{result_id}/pdf` | Bao cao PDF |

---

## Ma loi (Error Codes)

| HTTP Code | Mo ta |
|-----------|-------|
| 200 | Thanh cong |
| 401 | Sai hoac thieu `X-API-Key` |
| 404 | Khong tim thay ket qua |
| 422 | Tham so khong hop le |

---

## Luong su dung trong app

```
1. POST /ingest          (firmware gui ket qua len server)
        |
        v
2. GET /external/device/RPL02013/results?limit=1
        |                (lay result_id moi nhat)
        v
3. GET /external/results/{id}/detail
        |                (doc chi tiet 10 kenh)
        v
4. GET /external/results/{id}/chart   (hien thi bieu do)
   GET /external/results/{id}/pdf     (tai bao cao)
```

---

**Lien he:** Neu gap loi, lien he admin de kiem tra API key va ket noi server.
