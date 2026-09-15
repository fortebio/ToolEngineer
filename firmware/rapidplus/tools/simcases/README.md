# Bộ kịch bản mô phỏng đánh giá kết quả (sinh từ `tools/sim_cases.py gen`)

Sinh ở 90 vòng × 20 s. **Đừng sửa tay** — sửa recipe trong `tools/sim_cases.py` rồi chạy lại `gen`. Cột *mirror* là bản MIRROR trên PC, **không phải** máy; máy thật là `tools/run_sim_cases.py COM7`. Mỗi kịch bản có ba file: `.cal.txt` (đã calibrate, phát lại được bằng `sse_test_server.py --slots <file> --reboot`), `.raw.txt` (đếm thô ở slope danh nghĩa, cho `send_slots.py`), `.expect.json` (kỳ vọng từng giếng + số của mirror).

Ký hiệu: **kỳ vọng** = chữ theo thiết kế giếng · **chấp nhận** = chữ khác cũng được (giếng cố ý sát biên) · *(máy hiện nay: X)* = điểm yếu đã biết, máy hôm nay trả X — không tính đậu/rớt.

## S01_positive_ladder - Dilution ladder: eight clean positives Ct 5..20 min, two NTC

Ct accuracy and the P/S boundary from the reagent side: a run the lab could in principle reproduce. Sharpness 25..38, increase 60..110 - well clear of every gate.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **P** |  | 4.0..6.0 | P | 4.7 | 119 | 49.6 | 0 | Ct 5.0, PC-like |
| 2 | **P** |  | 6.0..8.0 | P | 6.7 | 94 | 34.1 | 0 | Ct 7.0, slots.txt slot 6 |
| 3 | **P** |  | 8.0..10.0 | P | 8.7 | 87 | 24.7 | 0 | Ct 9.0 |
| 4 | **P** |  | 10.0..12.0 | P | 11.0 | 80 | 24.6 | 0 | Ct 11.0 |
| 5 | **P** |  | 12.0..14.0 | P | 12.7 | 79 | 20.1 | 0 | Ct 13.0, high level |
| 6 | **P** |  | 14.0..16.0 | P | 14.7 | 75 | 21.1 | 0 | Ct 15.0, low level |
| 7 | **P** |  | 17.0..19.0 | P | 17.7 | 71 | 18.8 | 0 | Ct 18.0 |
| 8 | **P** |  | 19.0..21.0 | P | 20.0 | 64 | 20.9 | 0 | Ct 20.0, last P before S |
| 9 | **N** |  |  | N | 13.7 | 3 | 2.8 | 0 | NTC flat |
| 10 | **N** |  |  | N | 8.7 | 4 | 4.7 | 0 | NTC flat, noisy |

## S02_negative_field - Ten negatives as the fleet actually records them

Nothing here amplifies. Each well is one negative behaviour measured on real captures: creep, late knee, big warm-up, sync step, high noise, a small bump.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **N** |  |  | N | 3.3 | 2 | 2.3 | 0 | flat, sync step at round 6 |
| 2 | **N** |  |  | N | 23.0 | 4 | 2.9 | 0 | creep +21 over 30 min (slots.txt 9) |
| 3 | **N** |  |  | N | 26.0 | 7 | 4.1 | 0 | creep with a late knee (slots.txt 3) |
| 4 | **N** |  |  | N | 16.3 | 2 | 1.8 | 0 | near-flat, low noise |
| 5 | **N** |  |  | N | 5.0 | 2 | 3.5 | 0 | warm-up 27 -> 148 (slots.txt 3) |
| 6 | **N** |  |  | N | 13.3 | 6 | 6.6 | 0 | noisy, sigma 3.8 |
| 7 | **N** |  |  | N | 17.3 | 1 | 1.0 | 0 | slow downward drift |
| 8 | **N** |  |  | N | 13.7 | 9 | 6.3 | 0 | 2-min bump of +7 at 15 min |
| 9 | **N** |  |  | N | 19.0 | 4 | 4.4 | 0 | quadratic creep, +18 by the end |
| 10 | **N** |  |  | N | 21.7 | 3 | 4.9 | 0 | high level 558, flat |

## S03_slight_positive_boundary - Late risers straddling min_slight_positive_time = 22 min

S is decided by ONE number, the Ct, against 22.0. Wells sit at 20.5 / 21.3 / 22.7 / 24 / 26 / 27.5 so both sides of the line are exercised; the 21.3 and 22.7 wells are close enough that either letter is tolerated on the unit (the design Ct is analytic, SG smoothing moves it by up to a round).

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **P** | PS | 19.5..21.5 | P | 20.3 | 83 | 25.0 | 0 | Ct 20.5 |
| 2 | **P** | PS | 20.3..22.3 | P | 21.0 | 81 | 25.7 | 0 | Ct 21.3 - boundary |
| 3 | **S** | SP | 21.7..23.7 | S | 22.7 | 77 | 27.6 | 0 | Ct 22.7 - boundary |
| 4 | **S** |  | 23.0..25.0 | S | 23.7 | 85 | 27.1 | 0 | Ct 24.0 |
| 5 | **S** |  | 25.0..27.0 | S | 26.3 | 70 | 27.0 | 0 | Ct 26.0, plateau not reached |
| 6 | **S** | SN | 26.2..28.8 | S | 27.0 | 54 | 28.4 | 0 | Ct 27.5, rise cut by end of run |
| 7 | **S** |  | 23.0..25.0 | S | 23.7 | 41 | 12.8 | 0 | Ct 24.0, weak (A 45) |
| 8 | **P** |  | 18.0..20.0 | P | 19.0 | 84 | 27.6 | 0 | Ct 19.0, control P |
| 9 | **N** |  |  | N | 20.3 | 4 | 3.0 | 0 | creep, no rise |
| 10 | **N** |  |  | N | 13.0 | 3 | 3.4 | 0 | flat |

## S04_weak_and_threshold_band - Weak amplification around min_increase 25 / min_sharpness 8 and the v2.4.3 band

F (threshold band) exists for wells that were Positive under 20/5 and are not under 25/8. Each well is placed on one side of one gate with a margin the pre-screen reports. ASF-like kinetics (sharpness 4..8, increase 40..70) are here because docs 2026-08-21 says they must read F on this build - that is the documented cost.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **P** |  | 9.0..11.0 | P | 9.7 | 56 | 20.3 | 0 | weak-but-clear: sharp 17, inc 45 |
| 2 | **P** |  | 9.0..11.0 | P | 10.0 | 34 | 15.4 | 0 | just above both gates: sharp ~12, inc ~31 |
| 3 | **F** | FN |  | F | 9.7 | 23 | 9.2 | 0 | increase in (20,25), sharp ~10 -> band |
| 4 | **F** | FP |  | F | 11.0 | 49 | 6.6 | 0 | ASF-like: sharp 5..8, inc ~55 -> band |
| 5 | **N** | NF |  | N | 22.0 | 8 | 3.7 | 0 | sharp ~3.5 fails legacy 5 too -> N |
| 6 | **N** |  |  | N | 10.0 | 14 | 6.4 | 0 | increase ~12 fails legacy 20 too -> N |
| 7 | **P** |  | 7.0..9.0 | P | 7.7 | 169 | 70.6 | 0 | strong control: sharp 75, inc 150 |
| 8 | **P** | PF |  | P | 11.7 | 44 | 9.5 | 0 | slow but real: sharp ~7.5-8.5 -> BORDERLINE, either |
| 9 | **N** |  |  | N | 18.0 | 4 | 3.3 | 0 | creep +27 over the run, no knee |
| 10 | **N** |  |  | N | 17.3 | 1 | 1.7 | 0 | flat |

## S05_early_rise_margin - Rises at and before the 4-min detection margin and the 3.0-min Ct floor

Three different answers live within two minutes of each other: P (Ct >= 3.0 with a lag phase), F/2 (real reaction, Ct < 3.0), E (no left arm - the rise was already under way when measurement began). This is the branch d7775b1 moved; it has never been exercised on a unit.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **P** | PF | 3.0..5.0 | P | 3.7 | 120 | 49.3 | 0 | Ct 4.0 - on the margin |
| 2 | **P** | PFE | 2.4..4.4 | P | 3.3 | 113 | 43.8 | 0 | Ct 3.4 - inside the ERP-corrected band 3.0..3.7 |
| 3 | **F** |  | 1.0..2.9 | F | 1.7 | 150 | 45.1 | 0 | two-stage rise: lead-in at 2.8, main rise at 5.6 -> Ct ~1.7 with a left arm -> F reason 2 |
| 4 | **E** | EF |  | E | 4.0 | 46 | 18.7 | 0 | rising from t=0, saturating by 10 min: peak at the margin, no left arm |
| 5 | **E** | ENF |  | N | 4.0 | 12 | 8.1 | 0 | same shape, smaller (A 90): increase ~20 -> may fall to N |
| 6 | **P** |  | 5.0..7.0 | P | 5.7 | 103 | 44.0 | 0 | Ct 6.0 under a big warm-up (150) |
| 7 | **E** | EN |  | E | 1.7 | 173 | 61.0 | 0 | Ct 2.0: still rising at the margin, left arm before it -> E |
| 8 | **F** | FP | 2.0..3.0 | F | 2.7 | 176 | 64.4 | 0 | two-stage rise, Ct ~2.7: the last round under the 3.0 floor -> F reason 2 |
| 9 | **N** |  |  | N | 4.7 | 3 | 2.8 | 0 | warm-up only (200, tau 2.5 rounds) |
| 10 | **N** | NE |  | N | 4.0 | 6 | 19.3 | 0 | Ct 2.0, k 2.5: the whole rise is over before 4 min -> invisible, N |

## S06_breaks_and_climbs - Steps, spikes and dropouts on negatives: Break vs neutralised climb

BREAK_JUMP_THRESHOLD 20 splits a step into two fates: >= 20 on a flat trace is a Break; 8..20 is repaired by neutralise_climbs and the well is read as what is left. Each well is one row of that table, including the RPL01004 staircase that used to read Positive.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **N** |  |  | N | 9.0 | 3 | 2.3 | 1 | +32 at 10 min on a quiet trace: repaired as an offset, NOT a Break (repair runs first, no upper size) |
| 2 | **N** |  |  | N | 22.7 | 1 | 1.4 | 1 | +12 at 10 min (sigma 0.7) -> offset repaired |
| 3 | **N** |  |  | N | 26.3 | 1 | 1.4 | 1 | RPL01004 s2: +19.3 at 5 min, sigma 0.8 -> repaired, not P |
| 4 | **N** |  |  | N | 10.3 | 2 | 3.2 | 1 | +26 single-reading spike -> repaired |
| 5 | **N** |  |  | N | 12.7 | 3 | 3.0 | 1 | dropout -40 for 3 readings -> repaired |
| 6 | **N** |  |  | N | 27.3 | 1 | 1.1 | 3 | staircase 3 x +18 -> repaired |
| 7 | **N** |  |  | N | 9.3 | 2 | 2.6 | 1 | -30 at 10 min: invisible to checkJump, repaired |
| 8 | **N** |  |  | N | 25.7 | 2 | 2.7 | 1 | +25 in the last 6 readings -> tail hold |
| 9 | **B** | BN |  | B | 0.0 | - | - | 0 | +32 on sigma 3: 2.5x < jump < 4x local range -> Break |
| 10 | **B** | BN (máy hiện nay: P) |  | P | 16.7 | 48 | 30.4 | 0 | +40 on sigma 4.5: dodges the climb gate (4x range) AND checkJump (post-step slope > 1.0) -> the scorer reads the smoothed edge as a rise |

## S07_artefacts_on_positives - Positives that also carry a step, spike, dropout or tail step

Repair exists so a well with real amplification underneath keeps a readable result instead of a Break, and a break inside the plateau trims the end instead of the reaction. Ct must survive every repair.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **P** |  | 8.0..10.0 | P | 8.7 | 87 | 24.0 | 1 | offset +18 at 5 min, then P at Ct 9 |
| 2 | **P** |  | 8.0..10.0 | P | 9.0 | 82 | 26.8 | 1 | dropout in the plateau |
| 3 | **P** |  | 8.0..10.0 | P | 8.7 | 83 | 25.5 | 1 | step in the last 5 readings: Ct must not slide to the end |
| 4 | **P** | PB | 7.0..9.0 | P | 8.0 | 81 | 26.1 | 1 | +40 at 20 min after the plateau: end-trim, P kept |
| 5 | **P** | PB | 15.0..17.0 | P | 16.0 | 80 | 25.0 | 1 | +40 at 8 min BEFORE the rise: start-trim, P kept |
| 6 | **P** | PBF | 8.0..10.0 | P | 10.0 | 66 | 28.9 | 0 | +25 spike on the rising flank |
| 7 | **P** |  | 8.0..10.0 | P | 9.0 | 80 | 25.5 | 0 | sigma 4 noise on a real rise |
| 8 | **P** |  | 8.0..10.0 | P | 8.7 | 85 | 24.2 | 1 | -18 at 5 min then P |
| 9 | **P** |  | 8.0..10.0 | P | 9.0 | 83 | 24.6 | 0 | control P |
| 10 | **N** |  |  | N | 17.0 | 2 | 2.5 | 0 | control N |

## S08_noise_stress - Flat wells at sigma 1 .. 6 calibrated units, plus positives under the same noise

min_sharpness 8.0 was set where pure noise never reaches (P(>8.0) = 0.000% at today's sigma ~2). Sigma 4..6 is 2-3x worse than any fleet unit; a flat well there may legitimately trip a gate, so those two only tolerate N or F.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **N** |  |  | N | 17.3 | 1 | 1.5 | 0 | sigma 1.0 |
| 2 | **N** |  |  | N | 14.7 | 2 | 3.5 | 0 | sigma 2.0 (fleet typical) |
| 3 | **N** |  |  | N | 17.7 | 7 | 5.3 | 0 | sigma 3.0 |
| 4 | **N** | NF |  | N | 15.7 | 7 | 9.0 | 0 | sigma 4.0 |
| 5 | **N** | NFB |  | N | 14.0 | 8 | 10.4 | 0 | sigma 6.0 - stress |
| 6 | **P** |  | 9.0..11.0 | P | 10.0 | 82 | 23.9 | 0 | P at sigma 2 |
| 7 | **P** |  | 9.0..11.0 | P | 10.0 | 88 | 24.6 | 0 | P at sigma 4 |
| 8 | **P** | PF | 9.0..11.0 | P | 9.7 | 28 | 18.8 | 0 | weak P (A 45) at sigma 4 |
| 9 | **N** |  |  | N | 22.0 | 9 | 7.0 | 0 | low level 150, sigma 3 |
| 10 | **N** |  |  | N | 7.3 | 3 | 5.0 | 0 | high level 560, sigma 3 |

## S09_common_mode - Shared wobble and shared steps across all ten wells, two of them positive

probe_sensor_noise.py measured 64% common component on one unit and 9% on another. Calls are per well; a shared move must not become ten detections, and must not hide the two real ones.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **P** |  | 7.0..9.0 | P | 8.0 | 91 | 33.9 | 0 | P under common-mode |
| 2 | **N** |  |  | N | 16.0 | 6 | 5.5 | 0 | shared only |
| 3 | **N** |  |  | N | 15.7 | 9 | 6.9 | 0 | shared only |
| 4 | **N** |  |  | N | 15.7 | 8 | 7.0 | 0 | shared only |
| 5 | **P** |  | 13.0..15.0 | P | 14.0 | 67 | 24.9 | 0 | P under common-mode |
| 6 | **N** |  |  | N | 21.0 | 8 | 5.2 | 0 | shared only |
| 7 | **N** |  |  | N | 15.7 | 7 | 6.1 | 0 | shared only |
| 8 | **N** |  |  | N | 16.0 | 8 | 7.0 | 0 | shared only |
| 9 | **N** |  |  | N | 16.0 | 5 | 5.3 | 0 | shared only |
| 10 | **N** |  |  | N | 22.0 | 8 | 6.3 | 0 | shared only |

## R01_real_all_negative_RPL250701 - Real run RPL250701 30-07-2025 (v2.2.9), all ten wells Negative

Raw capture, its own slopes; the unit called N x10. Level 210..420 raw, +90 raw sync step at round 6, sigma ~1.5.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **N** |  |  | N | 25.3 | 2 | 3.6 | 0 | unit said N |
| 2 | **N** |  |  | N | 25.7 | 5 | 4.9 | 0 | unit said N |
| 3 | **N** |  |  | N | 25.3 | 7 | 5.0 | 0 | unit said N |
| 4 | **N** |  |  | N | 3.3 | 5 | 3.6 | 0 | unit said N |
| 5 | **N** |  |  | N | 10.0 | 3 | 3.0 | 0 | unit said N |
| 6 | **N** |  |  | N | 25.3 | 6 | 5.4 | 0 | unit said N |
| 7 | **N** |  |  | N | 3.7 | 2 | 2.8 | 0 | unit said N |
| 8 | **N** |  |  | N | 20.0 | 6 | 4.1 | 0 | unit said N |
| 9 | **N** |  |  | N | 25.3 | 2 | 2.1 | 0 | unit said N |
| 10 | **N** |  |  | N | 25.3 | 5 | 4.6 | 0 | unit said N |

## R02_real_mixed_slots_txt - Real calibrated run tools/slots.txt (10 x 120), one clean positive

The run the web mock replays. Slot 6 is the reference positive; three creeping wells are reported, not asserted.

| slot | kỳ vọng | chấp nhận | Ct (phút) | mirror | Ct | inc | sharp | climb | ghi chú |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **N** |  |  | N | 9.0 | 4 | 4.7 | 0 | flat 146 |
| 2 | **N** |  |  | N | 14.0 | 2 | 2.1 | 0 | flat 186, noisy |
| 3 | **?** |  |  | N | 28.3 | 9 | 8.6 | 0 | creep +47 with late knee: reader cannot call it |
| 4 | **N** |  |  | N | 19.3 | 4 | 3.8 | 0 | flat 465 after warm-up |
| 5 | **N** |  |  | N | 24.3 | 3 | 3.3 | 0 | flat 444 |
| 6 | **P** |  | 3.5..5.5 | P | 4.3 | 70 | 23.4 | 0 | clean P, algorithm Ct ~4.3 (40%-of-peak-slope), rise 107 |
| 7 | **N** |  |  | N | 4.7 | 2 | 2.6 | 0 | flat 361 after warm-up |
| 8 | **?** |  |  | N | 26.7 | 5 | 3.1 | 0 | creep +20: reader cannot call it |
| 9 | **?** |  |  | N | 28.3 | 3 | 3.2 | 0 | creep +21: reader cannot call it |
| 10 | **N** |  |  | N | 26.0 | 4 | 4.1 | 0 | flat 558 |

