# Ràng buộc phát triển firmware khi code bằng Claude — học từ `crosspoint-reader`

**Ngày:** 2026-09-07 · **Trạng thái:** báo cáo + kế hoạch, **chưa thực thi gì** · **Nhánh đo:** `v2.4.4A` (`cf6e66e`)
**Nguồn tham chiếu:** [github.com/crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
(firmware ESP32-C3 open-source cho máy đọc sách Xteink X4, ~3400 PR, nhiều người đóng góp, khai báo công khai
là codebase AI-assisted)

Tài liệu này trả lời: *cùng là firmware ESP32 phát triển bằng AI agent, họ ràng buộc bằng cái gì mà ta chưa có.*

---

## 0. Phát hiện chặn cứng: lớp ràng buộc của ta có một phần KHÔNG TỒN TẠI

Đây là kết quả đo hôm nay, không phải nhận định. Phải xử lý trước mọi đề xuất bên dưới.

| Đo | Kết quả |
| --- | --- |
| Guard `tools/*` được CLAUDE.md viện dẫn | **40** |
| Trong đó có thật trên nhánh này | 28 |
| **Chưa từng tồn tại trong git history — mọi nhánh, mọi commit** | **12** |
| Link tài liệu nội bộ trong CLAUDE.md | 52 |
| **Link chết** | **10** (19%) |
| `python tools/check.py` chạy thật | 15 guard: **10 PASS · 0 FAIL · 5 SKIP** (`toolchain not installed`) |
| **Exit code khi có 8 guard vắng mặt** | **0** — không chặn gì |

Nghĩa là: trên một máy dev điển hình, **10 trên 40 guard được viện dẫn thực sự chạy (25%)**. 5 guard C++ im
lặng SKIP vì máy không có `g++`. 12 guard được mô tả kèm cả lý do tồn tại và bất biến chúng khoá —
`test_hotlid_pwm_cap.py`, `test_upload_targets.py`, `test_result_string_fits.py`, `test_algo_accuracy.py`,
`test_slot_label_reset.py`, `test_ota_md5.js`, … — **không có dòng code nào**.

**Vì sao đây là vấn đề nghiêm trọng hơn "thiếu test":** CLAUDE.md được nạp vào *mọi phiên* và Claude **tin
nó**. Một agent đọc câu *"Guard `python tools/test_hotlid_pwm_cap.py`"* sẽ kết luận trần duty nắp nhiệt đang
được bảo vệ, và **không kiểm lại** khi sửa `PIDControl.cpp`. Tài liệu không chỉ vô dụng ở chỗ đó — nó **chủ
động tạo cảm giác an toàn sai** trên đúng những bất biến nguy hiểm nhất (nhiệt độ, chuỗi kết quả upload, nhãn
slot, độ chính xác thuật toán chẩn đoán).

Ba đường sinh ra tình trạng này, cả ba đều là hệ quả trực tiếp của việc **không có gì cưỡng chế**:

1. `check.py` **đã phát hiện** 8/12 guard vắng mặt và in ra — rồi **exit 0**. Nó báo cáo, không chặn.
2. 4 guard còn lại (`audit_logs.py`, `test_break_trim.sh`, `replay_algo.cpp`, `algo_labels.tsv`) **lọt hẳn**
   vì `check.py` chỉ quét mẫu `tools/test_*`.
3. **Không ai kiểm link tài liệu.** 10 link chết đều thuộc giai đoạn 07–20/08 — tức là **drift đang tăng
   tốc**, không phải nợ cũ.

Đối chiếu: crosspoint không thể rơi vào trạng thái này, vì bộ test **là** nguồn sự thật (`ctest` liệt kê được
suite nào tồn tại) và CI có **một job bắt buộc duy nhất** tổng hợp mọi job con. Một guard được nhắc mà không
có sẽ đỏ ngay, hoặc đơn giản là không ai nhắc tới nó.

---

## 1. crosspoint-reader ràng buộc bằng gì

Mười cơ chế, xếp theo giá trị chuyển giao được cho ta.

### 1.1 Tách "tra cứu luôn nạp" khỏi "phán đoán nạp theo nhu cầu"

| | crosspoint | FBT-DXD |
| --- | --- | --- |
| Luôn nạp | `AGENTS.md` — **1030 dòng**, thuần tra cứu | `CLAUDE.md` — **1583 dòng**, trộn tra cứu + phán đoán + tường thuật sự cố |
| Nạp theo nhu cầu | `.skills/` — **5 skill × ~55 dòng** | không có |
| Trỏ | `CLAUDE.md` = **một dòng**, nội dung là `AGENTS.md` | — |

`.skills/README.md` nói rõ ranh giới, và câu này đáng chép nguyên văn:

> *"These are the applied decision procedures that load on demand and add the judgment layer `AGENTS.md` does
> not carry. […] Do not restate `AGENTS.md`; add the judgment that file cannot afford to carry. Trigger
> quality lives in the `description` field: it must name the situations that should pull the skill in, in the
> words a contributor's task would use."*

5 skill của họ: `heap-discipline` · `control-flow-clarity` · `hal-and-abstractions` · `scope-discipline` ·
`refactor-for-review`.

### 1.2 Self-review checklist — agent tự chấm diff CỦA CHÍNH NÓ trước khi giao

Mỗi skill kết thúc bằng một checklist. Ví dụ `heap-discipline`:

```
- [ ] No bare new/new[]. Every fallible alloc is makeUniqueNoThrow, or a raw alloc with an owner comment.
- [ ] Every allocation is null-checked with LOG_ERR before the error return.
- [ ] No allocation inside a loop or render path that could be hoisted.
- [ ] Every push_back loop has a preceding reserve.
- [ ] Anything allocated in onEnter is released in onExit.
- [ ] Each new allocation carries a one-line size + why-not-stack/static note.
```

README ghi thêm: *"Reviewing a PR? Those checklists double as a fast rubric."* — cùng một danh sách phục vụ
hai vai.

**Đây là lớp ta thiếu hoàn toàn.** Guard bắt được cái *đo được bằng máy*. Checklist bắt được cái không đo
được: một-concern-mỗi-commit, comment nói *tại sao* chứ không nói *cái gì*, đã justify cấp phát chưa, đã nêu
worst-case size chưa.

### 1.3 Hợp đồng nhận thức cho agent (`AI Agent Identity and Cognitive Rules`)

Mục đầu tiên của `AGENTS.md`, trước cả kiến trúc:

- **Evidence-Based Reasoning** — *"Before proposing a change, you MUST cite the specific file path and line
  numbers that justify the modification."*
- **Anti-Hallucination** — không giả định API tồn tại; không chắc thì đọc source SDK trước.
- **No Unfounded Claims** — *"Do not claim performance gains or memory savings without explaining the
  technical mechanism (e.g., DRAM vs IRAM usage)."*
- **Resource Justification** — *"You must justify any new heap allocation … or explain why a stack/static
  alternative was rejected."*
- **Verification** — *"After suggesting a fix, instruct the user on how to verify it."*

Ta có quy tắc **làm việc** (docs/history, 1 commit 1 thay đổi). Ta **không** có quy tắc **suy luận**.

### 1.4 Testing checklist tách rõ: agent verify được gì / người phải thử gì trên máy

```
AI agent scope (what you CAN verify):
1. ✅ Build   — build MỘT lần sau lần sửa code cuối. Không clean mặc định, không lặp target đã xanh,
                không rebuild sau thay đổi chỉ-comment / chỉ-tài-liệu.
2. ✅ Quality — pio check + ./bin/clang-format-fix -g
3. ✅ Format  — commit message, không stage file bị .gitignore loại
4. ✅ CI      — sửa GitHub Actions đỏ TRƯỚC khi xin review
5. ✅ Review  — kiểm switch/case phủ đủ 4 hướng màn hình

Human tester scope (flag these for the user):
6. 🔲 Device · 7. 🔲 Orientations · 8. 🔲 Heap >50KB, no leaks · 9. 🔲 Cache
```

Rất hợp với ta: phần lớn bất biến của FBT (heap liền mạch 42 KB, mutex EEPROM, treo `async_tcp`, upload TLS)
**chỉ máy thật mới lộ**. Hiện ranh giới này là ngầm hiểu; ở họ nó là danh sách bàn giao.

### 1.5 CI là lớp cưỡng chế, hội tụ về MỘT required check

`.github/workflows/ci.yml`, chạy trên mọi PR:

| Job | Làm gì |
| --- | --- |
| `clang-format` | chạy formatter rồi `git diff --exit-code` — đỏ nếu code chưa format |
| `cppcheck` | `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` |
| `build` (matrix ×4) | build `default` / `sticky` / `x4pro` / `papermono`, **in RAM+Flash vào `$GITHUB_STEP_SUMMARY`**, upload `.bin` artifact |
| `unit-tests` | `cmake -S test -B build/test -G Ninja` → `ctest --output-on-failure -j` |
| **`test-status`** | `needs: [build, clang-format, cppcheck, unit-tests]`, `if: always()`, đỏ nếu bất kỳ job nào failure/cancelled |

Comment trong file giải thích vì sao có `test-status`: *"used as the PR required actions check, allows for
changes to other steps in the future without breaking PR requirements."* — Một tên duy nhất để cấu hình branch
protection; thêm/bớt job không phải sửa lại luật merge.

Đáng chú ý: **`--fail-on-defect low`**. Họ chấp nhận cppcheck ở mức thấp nhất. Ta chỉ có `-Wformat`.

### 1.6 Test host-side: CMake + gtest, **link thẳng file `.cpp` thật**

`test/` — 18 suite, chạy bằng `ctest -j`. gtest kéo bằng `FetchContent` (ghim `v1.17.0`). Một `INTERFACE` lib
chung bật `-Wall -Wextra -pedantic`. Mỗi suite là một thư mục có `CMakeLists.txt` riêng:

```cmake
add_executable(CssParserTest
  CssParserTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp)     # source firmware THẬT, không phải bản chép

target_include_directories(CssParserTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs                 # stub cho phần Arduino
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/Memory)

target_link_libraries(CssParserTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CssParserTest)
```

**Đây chính là mẫu ta cần**, vì CLAUDE.md của ta đã tự ghi nhận đúng rủi ro mà mẫu này giải:

> *"`audit_logs.py` KHÔNG kiểm được thay đổi trong `sensor6035.cpp` — nó chỉ link `Alg/Algo.cpp` và **tự mang
> bản sao khối trim**"* · *"guard `test_break_trim.sh` … phần kiểm hành vi **tự mang bản sao** của nó — revert
> `sensor6035.cpp` thì phần đó **vẫn xanh**"* · *"bản chép sẽ pass trong khi bản chạy thật đã hỏng"*

Ta đã biết bệnh và đã viết ra ba lần. Thứ còn thiếu là **hạ tầng** để không phải chép: một thư mục `stubs/`
cho Arduino + `add_executable` trỏ vào `src/*.cpp` thật. Đổi lại, 5 lệnh `g++` rời rạc (mỗi lệnh một bộ cờ,
một lệnh cần `-I.pio/libdeps/esp32dev/ArduinoJson/src`, tất cả đều SKIP im lặng khi thiếu toolchain) gộp
thành `ctest`.

### 1.7 Công cụ ĐO, để agent không phải đoán

- **`scripts/firmware_size_history.py`** — build firmware tại nhiều commit, parse `RAM:` / `Flash:` từ output
  PlatformIO, in bảng delta hoặc CSV. `--range HEAD~5 HEAD` hoặc `--commits <ref>...`.
- **`scripts/script_profile_mem.sh`** — `objdump -t` trên `.elf`, xếp hạng symbol lớn nhất theo từng section:
  `.dram0.bss`, `.dram0.data`, `.flash.rodata`, `.flash.text`, `.iram0.text`.

Skill `scope-discipline` **bắt buộc** dùng chúng: *"Quantify with `firmware_size_history.py` and
`script_profile_mem.sh` rather than guessing."*

Ta ở **74.8% flash**, có trần cứng `parastructure` **402 B**, ngân sách TLS **42 KB liền mạch**, và CLAUDE.md
đòi bằng chứng đo được ở khắp nơi ("đo thật", "đo được") — nhưng **không có công cụ nào sinh ra con số đó**.
Mọi số trong tài liệu là đo tay một lần rồi chép vào prose, không lặp lại được.

### 1.8 Hook local + CI cùng một lệnh

`.githooks/pre-commit` chạy formatter rồi **re-stage đúng những path đã staged**, không kéo theo thay đổi
tracked khác trong working tree. Cùng script `./bin/clang-format-fix` mà CI gọi → hook và CI không thể lệch.

### 1.9 SCOPE.md + cổng phạm vi + PR template

`SCOPE.md` là nguồn sự thật về cái gì trong/ngoài phạm vi. `scope-discipline` biến nó thành cổng 4 câu hỏi
**trả lời theo thứ tự, dừng ở câu đầu tiên fail**, và kết luận rất thẳng:

> *"If a request fails the gate, push back with the specific reason and the `SCOPE.md` basis, and offer the
> in-scope alternative. **Make the call and say why; do not just hand over a menu.**"*

Kèm hai nguyên tắc: *"Settings are not free"* (mỗi setting là một field phải persist, migrate, validate,
translate, render, cộng gánh nặng test tổ hợp) và *"the cheapest feature is the one already built"*.

PR template có checklist phạm vi, và mục cuối:
**`Did you use AI tools to help write this code? < YES | PARTIALLY | NO >`**.

### 1.10 Vài luật nhỏ đáng lấy nguyên văn

- **Comment style:** *"write them for the merged state, as if the code had always worked this way. Remove
  before/after narration, investigation measurements, and rationale that belongs in the commit message."*
  → tường thuật sự cố thuộc về `docs/`, không thuộc về comment. (Ta đang làm đúng ở `docs/history/`; luật này
  chốt phía code.)
- **Build kỷ luật:** build **một lần** sau lần sửa cuối; không `clean` mặc định; **không** rebuild sau thay
  đổi chỉ-comment hoặc chỉ-tài-liệu. Tiết kiệm thời gian lẫn token mỗi phiên.
- **Git:** không bao giờ push / mở PR khi chưa được duyệt rõ ràng. *(Họ còn cấm agent tự ghi `Co-Authored-By`
  cho chính nó — ta đang làm ngược lại theo cấu hình harness. Ghi lại như một khác biệt có chủ ý, không đề
  xuất đổi.)*

---

## 2. Kế hoạch

Lớp cưỡng chế đã chốt: **GitHub Actions**. Thứ tự dưới đây theo tỷ lệ giá-trị/công-sức, và Phase 0 là điều
kiện tiên quyết — không có nó thì mọi thứ sau chỉ là thêm tài liệu lên một nền tài liệu đã không đáng tin.

### Phase 0 — Làm cho CLAUDE.md nói thật (chặn cứng, làm trước)

Không viết thêm cơ chế nào. Đóng khoảng cách giữa cái tài liệu tuyên bố và cái repo có.

1. **Quyết định từng guard trong 12 guard vắng mặt: viết, hay gỡ tuyên bố.** Không có lựa chọn thứ ba. Ưu
   tiên viết (không gỡ) với những cái khoá bất biến an toàn: `test_hotlid_pwm_cap.py` (trần duty nắp nhiệt),
   `test_result_string_fits.py` (chuỗi upload nuốt chữ kết luận), `test_upload_targets.py` (lỗi chỉ tới 1/3
   đích), `test_slot_label_reset.py` (upload nhãn bệnh của run trước).
2. **Sửa 10 link chết**, hoặc gỡ, hoặc viết tài liệu còn thiếu.
3. **`check.py` phải exit ≠ 0** khi (a) có guard documented-but-absent, (b) có link `.md` chết trong
   CLAUDE.md, (c) có guard SKIP vì thiếu toolchain **khi chạy trên CI**. Mở rộng mẫu quét khỏi `tools/test_*`
   để bắt cả `audit_logs.py`, `replay_algo.cpp`, `algo_labels.tsv`, `test_break_trim.sh`.

> **Xong khi:** `python tools/check.py` xanh **và** exit 0 **và** dòng `documented-but-absent` = 0 **và** 0
> link chết. Con số 25% coverage thật lên đúng bằng con số tài liệu tuyên bố.

### Phase 1 — CI GitHub Actions

Một workflow, chạy trên mọi push/PR. Bốn job + một `test-status` tổng hợp làm required check duy nhất.

| Job | Nội dung | Ghi chú |
| --- | --- | --- |
| `guards` | `python tools/check.py` (ubuntu **có** `g++`, `node`, `python`) | **Riêng job này đã nâng coverage thật từ 10 → 15 guard**, vì 5 guard C++ hết SKIP |
| `guards-mock` | `python tools/check.py --mock` | các guard cần `sse_test_server.py` + Chromium/Edge headless |
| `build` | `pio run -e esp32dev`, **đỏ nếu output chứa `warning:`** | chốt đúng luật GOTCHA 0 của ta ("cảnh báo lúc nào cũng hiện là cảnh báo bị bỏ qua"); in `RAM:`/`Flash:` vào `$GITHUB_STEP_SUMMARY` để mỗi PR thấy delta 74.8% |
| `cppcheck` | `pio check --fail-on-defect medium --fail-on-defect high` | bắt đầu ở `medium` cho đỡ ồn, siết xuống `low` sau. Lịch sử repo — `printf` LoadProhibited, `strcpy` đè `slopes`, `readCommand` tràn `recvData[2048]`, VLA QR tràn stack DisplayTask — đúng loại cppcheck nhắm tới |
| `test-status` | `needs:` cả bốn, `if: always()` | required check duy nhất trong branch protection |

Chi phí: ~5 phút build ESP32/lần trên Actions của repo private. Cache `~/.platformio` theo hash
`platformio.ini` như họ làm.

> **Xong khi:** một PR cố tình gieo lỗi (thêm `WiFi.begin()` runtime, hoặc hạ `HOTLID_PWM_MAX` xuống 90) bị
> CI chặn mà không cần ai nhớ phải chạy gì.

### Phase 2 — `.claude/skills/`, kèm self-review checklist

5 skill, mỗi cái ~50–60 dòng, YAML frontmatter `name` + `description`, kết bằng checklist. **Không nhắc lại
CLAUDE.md** — chỉ mang lớp phán đoán. Đề xuất, ánh xạ vào đúng những chỗ FBT đã trả giá:

| Skill | Nạp khi | Khoá cái gì |
| --- | --- | --- |
| `heap-and-tls-budget` | cấp phát, dựng payload, thêm route trả nhiều dữ liệu | 42 KB liền mạch · nhả BT sớm · không materialise payload trong handler (dùng `beginChunkedResponse`) · đo bằng `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` **không** phải `ESP.getMaxAllocHeap()` |
| `rtos-and-eeprom-safety` | đụng task, mutex, EEPROM, WiFi, AsyncTCP | task nào sở hữu cái gì · `eepromLock/Unlock` không lồng · AsyncTCP chỉ validate + enqueue · không `WiFi.begin()` runtime · bẫy self-deadlock `_client_queue_lock` |
| `guard-discipline` | thêm/sửa guard, hoặc thêm bất biến mới | mọi thay đổi hành vi phải có guard **chạy được** · guard **không được mang bản sao** logic firmware · phải **gọi hàm thật** · **negative test bắt buộc** (ta đã có 2 ca guard tự mù: regex `perLoopMs`, `code_only()` nuốt `http://`) |
| `web-ui-contract` | sửa `data/script.js`, `style.css`, route dashboard | `fillStatus` ↔ `fillActions` cùng tập state · phase mới không được là `"idle"`/`"finished"` tuỳ tiện · disabled bằng `grayscale` không phải `opacity` · breakpoint xét **cả hai** chiều · `minmax(0,1fr)` · thứ tự đăng ký route |
| `change-hygiene` | chuẩn bị commit / bàn giao | 1 commit 1 concern · subject trả lời *tại sao* · **`docs/history/YYYY-MM-DD-slug.md` viết ngay trong cùng lần thay đổi + link vào CLAUDE.md** · comment nói *tại sao* · không "while I'm here" |

Lưu ý vị trí: Claude Code đọc project skill ở **`.claude/skills/<name>/SKILL.md`** (crosspoint dùng `.skills/`
cho tooling khác của họ).

> **Xong khi:** mỗi skill có checklist, và chất lượng `description` đủ để tự kích hoạt đúng lúc — thử bằng
> vài task mẫu ("sửa PID nắp", "thêm route trả bảng", "chuẩn bị bàn giao").

### Phase 3 — Harness host-side + công cụ đo

1. **`test/host/` + CMake + gtest**, gộp 5 guard C++ hiện tại, mỗi suite `add_executable` trỏ vào **`src/*.cpp`
   thật** + `test/host/stubs/` (Arduino, `EEPROM`, `Preferences`). Bước đầu tiên và đáng giá nhất: **`Alg/` +
   khối cổng break/cắt cửa sổ của `sensor6035.cpp`** — xoá đúng bản sao mà `audit_logs.py` và
   `test_break_trim.sh` đang phải tự mang. Job `unit-tests` vào CI, `ctest --output-on-failure -j`.
2. **`tools/size_history.py`** theo mẫu `firmware_size_history.py`: build tại nhiều commit, bảng delta
   RAM/Flash. Trả lời được "thay đổi này tốn bao nhiêu flash" bằng số, tự động.
3. **`tools/profile_mem.sh`** theo mẫu `script_profile_mem.sh`: `objdump -t` xếp hạng symbol theo section. Với
   repo ở 74.8% flash, đây là thứ chỉ ra *cái gì* đang chiếm chỗ.

> **Xong khi:** `ctest` thay được mọi lệnh `g++ ... && ./t` trong CLAUDE.md, và không guard nào còn mang bản
> sao logic firmware.

---

## 3. Cố ý KHÔNG làm

- **Không tách CLAUDE.md.** 1583 dòng là tri thức mua bằng sự cố thật. Việc tách (giữ tra cứu, đẩy phán đoán
  xuống skills) là đúng hướng và crosspoint chứng minh nó chạy được — nhưng đó là một diff lớn cần chủ dự án
  review từng đoạn, **không phải việc agent tự làm**. Phase 2 thêm skills **cạnh** CLAUDE.md, không rút gì ra.
  Cân nhắc lại sau khi Phase 0–1 xong.
- **Không đổi luật attribution commit.** Harness đang bắt ghi `Co-Authored-By: Claude`; crosspoint cấm điều
  ngược lại. Đây là lựa chọn của dự án, không phải lỗi.
- **Không thêm `SCOPE.md`.** Thiết bị thương mại có đặc tả riêng; cổng phạm vi kiểu open-source không ánh xạ
  thẳng. Phần dùng được — "settings are not free", "rẻ nhất là tính năng đã có" — đã gấp vào `change-hygiene`.
- **Không chạm `clang-format`.** Repo chưa có style file; áp formatter lên 29 457 dòng `src/` sẽ tạo một diff
  nuốt trọn mọi `git blame`. Nếu muốn, phải là một commit riêng, không đi kèm gì khác.

---

## Phụ lục A — 12 guard được viện dẫn nhưng chưa từng tồn tại

`test_algo_accuracy.py` · `test_hotlid_pwm_cap.py` · `test_ota_md5.js` · `test_ota_release_manifest.py` ·
`test_outcome_reset.py` · `test_result_string_fits.py` · `test_slot_label_reset.py` · `test_upload_targets.py` ·
`audit_logs.py` · `replay_algo.cpp` · `algo_labels.tsv` · `test_break_trim.sh`

*(8 cái đầu được `check.py --list` báo; 4 cái sau lọt vì nằm ngoài mẫu quét `tools/test_*`.)*

## Phụ lục B — 10 link tài liệu chết trong CLAUDE.md

```
docs/history/2026-08-07-calibration-invariance-check.md
docs/history/2026-08-07-hotlid-pwm-cap.md
docs/history/2026-08-07-tach-nguong-break-khoi-min-increase.md
docs/history/2026-08-07-wokwi-tft-simulation.md
docs/history/2026-08-11-break-trim-off-by-one.md
docs/history/2026-08-14-b2-chon-nhanh-cat-cua-so.md
docs/history/2026-08-20-bao-cap-nhat-thanh-cong.md
docs/history/2026-08-20-result-string-mat-chu-ket-luan.md
docs/history/2026-08-20-slot-label-reset-per-run.md
docs/plan/2026-08-07-nang-do-chinh-xac-thuat-toan.md
```

## Phụ lục C — Cách tái lập số liệu mục 0

```bash
# guard được viện dẫn vs guard có thật
grep -oE 'tools/[A-Za-z0-9_./-]+\.(py|js|cpp|sh|tsv)' CLAUDE.md | sort -u \
  | while read -r f; do [ -e "$f" ] || echo "MISSING: $f"; done

# link tài liệu chết
grep -o '](\([^)]*\.md\)[^)]*)' CLAUDE.md | sed 's/^](//; s/)$//' | sort -u \
  | while read -r l; do case "$l" in http*) continue;; esac; [ -e "$l" ] || echo "MISSING: $l"; done

# guard chạy thật
python tools/check.py; echo "EXIT=$?"
```
