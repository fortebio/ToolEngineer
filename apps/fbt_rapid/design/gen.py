# Sinh 4 khổ phương án từ CÙNG một khung bảng (lấy nguyên phần <helmet> của Main.dc.html)
# — phần chung không thể lệch nhau, và đổi token thì đổi một chỗ.
import io, os, re

BASE = os.path.dirname(os.path.abspath(__file__))
main = io.open(os.path.join(BASE, "Main.dc.html"), encoding="utf-8").read().replace("\r\n", "\n")
HELMET = re.search(r"<helmet>.*?</helmet>", main, re.S).group(0)

ICON_OTA = ('<svg width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
            'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
            '<path d="M12 13v8"></path><path d="m8 17 4 4 4-4"></path>'
            '<path d="M20.88 13.06A5 5 0 0 0 18 4h-1.26A8 8 0 1 0 4 11.25"></path></svg>')
ICON_PIN = ('<svg width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
            'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
            '<path d="M12 17v5"></path><path d="M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 '
            '15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 '
            '10.76V7a1 1 0 0 1 1-1 2 2 0 0 0 0-4H8a2 2 0 0 0 0 4 1 1 0 0 1 1 1z"></path></svg>')
ICON_WAIT = ('<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
             'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
             '<circle cx="12" cy="12" r="9"></circle><path d="M12 7v5l3 2"></path></svg>')
ICON_OK = ('<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
           'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
           '<circle cx="12" cy="12" r="9"></circle><path d="m8.5 12.5 2.5 2.5 4.5-5"></path></svg>')
CHEV = ('<svg width="{s}" height="{s}" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
        'stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round">'
        '<path d="m6 9 6 6 6-6"></path></svg>')
ICON_TUNE = ('<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" '
             'stroke-width="2" stroke-linecap="round" stroke-linejoin="round">'
             '<path d="M4 6h10"></path><path d="M18 6h2"></path><circle cx="16" cy="6" r="2"></circle>'
             '<path d="M4 18h4"></path><path d="M12 18h8"></path><circle cx="10" cy="18" r="2"></circle></svg>')


def chip(mode, size=12):
    if mode == "pin":
        return ('<span class="chip pin">' + ICON_PIN.format(s=size) + ' Riêng</span>')
    return ('<span class="chip ota">' + ICON_OTA.format(s=size) + ' Chung</span>')


def sub(state, name):
    ic = ICON_OK if state == "ok" else ICON_WAIT
    txt = "Đúng bản" if state == "ok" else "Chờ nạp"
    return ('<div class="sub" style="margin-top:3px">' + ic + ' ' + txt + ' · ' + name + '</div>')


# ---- các biến thể của Ô ĐIỀU KHIỂN ----------------------------------------
def cell_a(mode, state, name, can):
    """A — nút viền riêng, chữ rõ nghĩa."""
    inner = '<div style="min-width:0">' + chip(mode) + sub(state, name) + '</div>'
    if not can:
        return ('<div style="display:flex;align-items:center;gap:10px">' + inner +
                '<span class="lockNote">cần v2.4.4+</span></div>')
    btn = ('<button class="btnOut">' + ICON_TUNE + ' Đổi bản</button>')
    return ('<div style="display:flex;align-items:center;gap:10px;justify-content:space-between">'
            + inner + btn + '</div>')


def cell_b(mode, state, name, can):
    """B — chính CHIP là nút: tô đậm, chevron nằm trong chip, cùng tone."""
    cls = "pin" if mode == "pin" else "ota"
    ic = ICON_PIN if mode == "pin" else ICON_OTA
    lbl = "Riêng" if mode == "pin" else "Chung"
    if not can:
        return ('<div>' + chip(mode) + sub(state, name) + '</div>')
    b = ('<button class="chipBtn ' + cls + '">' + ic.format(s=13) + ' ' + lbl
         + CHEV.format(s=14, w=2.6) + '</button>')
    return '<div>' + b + sub(state, name) + '</div>'


def cell_c(mode, state, name, can):
    """C — nút icon tròn ở mép phải ô."""
    inner = '<div style="min-width:0">' + chip(mode) + sub(state, name) + '</div>'
    if not can:
        return ('<div style="display:flex;align-items:center;gap:10px">' + inner +
                '<span class="lockNote">cần v2.4.4+</span></div>')
    return ('<div style="display:flex;align-items:center;gap:10px;justify-content:space-between">'
            + inner + '<button class="iconBtn" title="Đổi bản">' + ICON_TUNE + '</button></div>')


def cell_d(mode, state, name, can):
    """D — cả ô là một control kiểu ô chọn (giống input của app)."""
    inner = ('<div style="min-width:0">' + chip(mode) + sub(state, name) + '</div>')
    if not can:
        return ('<div class="selBox off">' + inner + '</div>')
    return ('<button class="selBox">' + inner + CHEV.format(s=16, w=2.4) + '</button>')


ROWS = [
    ("RPL03003", "v2.4.5", "pin", "wait", "fbt_v2.4.4.bin", True, True),
    ("RPL02013", "v2.4.5", "ota", "ok", "fbt_v2.4.5.bin", True, True),
    ("RPL01001", "v2.4.3", "ota", "wait", "fbt_v2.4.5.bin", False, False),
]

EXTRA_CSS = """
  <style>
    .btnOut { display:inline-flex; align-items:center; gap:6px; flex:none;
      padding:6px 12px; border-radius:14px; border:1px solid #57858E;
      background:#FFFFFF; color:#0F5F63; font-family:inherit;
      font-size:12.5px; font-weight:600; cursor:pointer; }
    .btnOut:hover { background:#EAF3F5; border-color:#0F5F63; }
    .chipBtn { display:inline-flex; align-items:center; gap:5px;
      padding:5px 8px 5px 10px; border-radius:999px; cursor:pointer;
      font-family:inherit; font-size:12px; font-weight:700; }
    .chipBtn.ota { color:#0E6E32; background:rgba(14,110,50,.14); border:1px solid rgba(14,110,50,.55); }
    .chipBtn.pin { color:#C21C1C; background:rgba(194,28,28,.14); border:1px solid rgba(194,28,28,.55); }
    .chipBtn.ota:hover { background:rgba(14,110,50,.22); }
    .chipBtn.pin:hover { background:rgba(194,28,28,.22); }
    .iconBtn { flex:none; width:32px; height:32px; display:inline-flex;
      align-items:center; justify-content:center; border-radius:999px;
      border:1px solid #57858E; background:#FFFFFF; color:#0F5F63; cursor:pointer; }
    .iconBtn:hover { background:#EAF3F5; border-color:#0F5F63; }
    .selBox { display:flex; align-items:center; justify-content:space-between; gap:10px;
      width:100%; text-align:left; padding:6px 10px; border-radius:14px;
      border:1px solid #57858E; background:#FFFFFF; color:#4F5A68;
      font-family:inherit; cursor:pointer; }
    .selBox:hover { background:#EAF3F5; border-color:#0F5F63; }
    .selBox.off { border-style:dashed; border-color:#C0D6DA; cursor:default; }
    .lockNote { flex:none; font-size:10.5px; color:#4F5A68; font-style:italic; }
  </style>
"""

OPTS = [
    ("OptionA", "A · Nút viền “Đổi bản”", cell_a,
     "Không thể nhầm: có chữ, có viền, đọc ra ngay là nút. Đổi lại nó ăn ~96px bề ngang mỗi hàng và lặp 95 lần xuống dưới."),
    ("OptionB", "B · Chính chip là nút", cell_b,
     "Một vật thể thay vì hai: chip vừa báo chế độ vừa mở menu, chevron cùng tone nên nổi hẳn. Gọn nhất, nhưng vẫn phải học một lần rằng chip bấm được."),
    ("OptionC", "C · Nút icon ở mép phải", cell_c,
     "Rẻ bề ngang nhất (32px) và thẳng hàng nên quét mắt dễ. Yếu nhất về nghĩa: icon một mình không nói nó sửa cái gì."),
    ("OptionD", "D · Cả ô là ô chọn", cell_d,
     "Mượn đúng hình dạng ô nhập của app nên đọc ra là control theo thói quen sẵn có. Vùng bấm lớn nhất; đổi lại mỗi hàng trông nặng hơn."),
]

TPL = """<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <script src="./support.js"></script>
</head>
<body>
<x-dc>
{helmet}
{extra}
<div style="padding: 24px 24px 28px">
  <p class="title">{title}</p>
  <p class="lede">{note}</p>
  <div class="card">
    <div class="head"><div>Máy</div><div>Firmware</div><div>Trạng thái update</div></div>
{rows}
  </div>
</div>
</x-dc>
<script data-dc-script data-props='{{}}'>
class Component extends DCLogic {{
  renderVals() {{ return {{}}; }}
}}
</script>
</body>
</html>
"""

for stem, title, fn, note in OPTS:
    rows = []
    for dev, fw, mode, state, name, can, fresh in ROWS:
        dot = '<span class="dot"></span>' if fresh else '<span class="dot off"></span>'
        rows.append(
            '    <div class="row">\n'
            '      <div class="dev">' + dot + dev + '</div>\n'
            '      <div class="mono">' + fw + '</div>\n'
            '      ' + fn(mode, state, name, can) + '\n'
            '    </div>')
    out = TPL.format(helmet=HELMET, extra=EXTRA_CSS, title=title, note=note,
                     rows="\n".join(rows))
    p = os.path.join(BASE, stem + ".dc.html")
    open(p + ".tmp", "wb").write(out.encode("utf-8"))
    os.replace(p + ".tmp", p)
    print("viet", stem + ".dc.html", len(out), "byte")
