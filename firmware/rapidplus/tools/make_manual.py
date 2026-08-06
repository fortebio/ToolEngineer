#!/usr/bin/env python3
"""Build the Word user manual from the Markdown source.

    python tools/make_manual.py [src.md] [out.docx]

Deliberately a SUBSET of Markdown - only what the manual actually uses. Anything
fancier belongs in the .md as plain prose, not in a bigger parser here.

Images are sized to FIT THE PAGE by aspect ratio, not by a fixed width: the phone
screenshots are full-page captures (tall and narrow) and a fixed width pushes them
off the bottom of the page.
"""
import re
import sys
from pathlib import Path

from PIL import Image
from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Inches, Pt, RGBColor

ROOT = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "manual" / "huong-dan-su-dung-web.md"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "docs" / "manual" / "HDSD-Web-FBT-RAPID.docx"

# Usable area of an A4 page with the margins set below.
MAX_W = Inches(6.3)
MAX_H = Inches(7.4)

BRAND = RGBColor(0x13, 0x75, 0x7A)   # --brand-ink: the only logo-derived colour that
BRAND_DEEP = RGBColor(0x08, 0x33, 0x36)  # passes AA on white (see CLAUDE.md Brand)
NOTE_FILL = "EAF6F7"
WARN_FILL = "FDECEA"


def shade(cell, hex_fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:fill"), hex_fill)
    tc_pr.append(shd)


def add_runs(par, text, bold=False, italic=False):
    """Inline **bold**, `code`, *italic* and _italic_.

    RECURSIVE, because the manual nests them: **`FBT-<id>`** is a bolded code span. A flat
    pass strips the ** and then emits the backticks as literal characters."""
    for part in re.split(r"(\*\*[^*]+\*\*|`[^`]+`|\*[^*]+\*|_[^_]+_)", text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            add_runs(par, part[2:-2], bold=True, italic=italic)
        elif part.startswith("`") and part.endswith("`"):
            r = par.add_run(part[1:-1])
            r.font.name = "Consolas"
            r.font.size = Pt(10)
            r.font.color.rgb = BRAND_DEEP
            r.bold, r.italic = bold, italic
        elif (part.startswith("*") and part.endswith("*")) or (
            part.startswith("_") and part.endswith("_")
        ):
            add_runs(par, part[1:-1], bold=bold, italic=True)
        else:
            r = par.add_run(part)
            r.bold, r.italic = bold, italic


def add_image(doc, path, caption):
    with Image.open(path) as im:
        w, h = im.size
    # Fit inside the page box: whichever of width/height binds first wins.
    width = min(MAX_W, Inches(MAX_H.inches * w / h))
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.add_run().add_picture(str(path), width=width)
    if caption:
        c = doc.add_paragraph()
        c.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = c.add_run(caption)
        r.italic = True
        r.font.size = Pt(9)
        r.font.color.rgb = RGBColor(0x55, 0x5F, 0x66)
        c.paragraph_format.space_after = Pt(14)


def add_box(doc, kind, text):
    fill = WARN_FILL if kind == "!" else NOTE_FILL
    label = "CẢNH BÁO" if kind == "!" else "LƯU Ý"
    t = doc.add_table(rows=1, cols=1)
    t.style = "Table Grid"
    cell = t.cell(0, 0)
    shade(cell, fill)
    p = cell.paragraphs[0]
    r = p.add_run(label + ": ")
    r.bold = True
    r.font.color.rgb = RGBColor(0xB3, 0x26, 0x1E) if kind == "!" else BRAND
    add_runs(p, text)
    doc.add_paragraph().paragraph_format.space_after = Pt(6)


def add_table(doc, rows):
    header, body = rows[0], rows[1:]
    t = doc.add_table(rows=1, cols=len(header))
    t.style = "Light Grid Accent 1"
    for i, h in enumerate(header):
        cell = t.rows[0].cells[i]
        cell.text = ""
        add_runs(cell.paragraphs[0], h)
        for r in cell.paragraphs[0].runs:
            r.bold = True
    for row in body:
        cells = t.add_row().cells
        for i, v in enumerate(row[: len(header)]):
            cells[i].text = ""
            add_runs(cells[i].paragraphs[0], v)
    doc.add_paragraph().paragraph_format.space_after = Pt(6)


def styles(doc):
    n = doc.styles["Normal"]
    n.font.name = "Segoe UI"        # ships with Windows and covers Vietnamese diacritics
    n.font.size = Pt(10.5)
    n.paragraph_format.space_after = Pt(6)
    n.paragraph_format.line_spacing = 1.15
    for name, size, color in (
        ("Heading 1", 20, BRAND_DEEP),
        ("Heading 2", 15, BRAND),
        ("Heading 3", 12, BRAND_DEEP),
    ):
        s = doc.styles[name]
        s.font.name = "Segoe UI"
        s.font.size = Pt(size)
        s.font.bold = True
        s.font.color.rgb = color
    for sec in doc.sections:
        sec.top_margin = sec.bottom_margin = Inches(0.8)
        sec.left_margin = sec.right_margin = Inches(0.9)


def footer(doc, text):
    p = doc.sections[0].footer.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    r = p.add_run(text)
    r.font.size = Pt(8)
    r.font.color.rgb = RGBColor(0x77, 0x80, 0x86)


def build(src_path, out_path):
    raw = src_path.read_text(encoding="utf-8").splitlines()
    base = src_path.parent
    doc = Document()
    styles(doc)

    headings = []       # (level, text) -> static contents page
    toc_anchor = None   # paragraph the contents list is inserted before
    state = {"toc": None}

    ITEM = re.compile(r"^(\s*)(?:([-*])|(\d+)[.)])\s+(.*)$")

    def emit(blk):
        """One blank-line-separated block. Markdown wraps lines inside a paragraph, so
        continuation lines must JOIN the block they belong to - treating every source line
        as its own Word paragraph shatters sentences, notes and list items."""
        first = blk[0].strip()

        if first.startswith("|"):
            rows = []
            for ln in blk:
                cells = [c.strip() for c in ln.strip().strip("|").split("|")]
                if all(re.fullmatch(r":?-{2,}:?", c) for c in cells):
                    continue  # separator row
                rows.append(cells)
            if rows:
                add_table(doc, rows)
            return

        if first == "<!-- pagebreak -->":
            doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)
            return
        if first == "<!-- toc -->":
            state["toc"] = doc.add_paragraph()
            return
        if re.fullmatch(r"-{3,}", first):
            return

        m = re.match(r"^(#{1,3})\s+(.*)$", first)
        if m:
            lvl, text = len(m.group(1)), m.group(2)
            # Every H1 starts a page - except the document title, which IS the first page.
            if lvl == 1 and headings:
                doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)
            doc.add_heading(text, lvl)
            headings.append((lvl, text))
            return

        m = re.match(r"^!\[(.*?)\]\((.*?)\)$", first)
        if m:
            add_image(doc, base / m.group(2), m.group(1))
            return

        if first.startswith(">"):
            cur = None
            for ln in blk:
                t = ln.strip()
                if t.startswith(">"):
                    if cur:
                        add_box(doc, cur[0], cur[1])
                    body = t[1:].strip()
                    kind = ""
                    if body.startswith("!"):
                        kind, body = "!", body[1:].strip()
                    cur = [kind, body]
                elif cur:
                    cur[1] += " " + t
            if cur:
                add_box(doc, cur[0], cur[1])
            return

        if ITEM.match(blk[0]):
            items = []
            for ln in blk:
                mi = ITEM.match(ln)
                if mi:
                    items.append([mi.group(2), mi.group(3), mi.group(4)])
                elif items:
                    items[-1][2] += " " + ln.strip()
            for bullet, num, text in items:
                if bullet:
                    par = doc.add_paragraph(style="List Bullet")
                    add_runs(par, text)
                else:
                    # Literal numbers, not Word auto-numbering: every "List Number"
                    # paragraph in a python-docx document shares ONE numbering sequence,
                    # so the second numbered list in the manual would start at 5.
                    par = doc.add_paragraph()
                    par.paragraph_format.left_indent = Inches(0.35)
                    par.paragraph_format.first_line_indent = Inches(-0.25)
                    par.add_run(num + ". ").bold = True
                    add_runs(par, text)
            return

        par = doc.add_paragraph()
        add_runs(par, " ".join(l.strip() for l in blk))

    block = []
    for ln in raw:
        if ln.strip():
            block.append(ln.rstrip())
        elif block:
            emit(block)
            block = []
    if block:
        emit(block)
    toc_anchor = state["toc"]


    # Static contents: a real TOC field would open as "update this field" and print empty.
    if toc_anchor is not None:
        lab = toc_anchor.insert_paragraph_before()
        lr = lab.add_run("MỤC LỤC")
        lr.bold = True
        lr.font.size = Pt(13)
        lr.font.color.rgb = BRAND_DEEP
        lab.paragraph_format.space_after = Pt(8)
        for lvl, text in headings[1:]:   # [0] is the document title, not a section
            if lvl > 2:
                continue
            p = toc_anchor.insert_paragraph_before()
            p.paragraph_format.left_indent = Inches(0.0 if lvl == 1 else 0.3)
            p.paragraph_format.space_after = Pt(2)
            r = p.add_run(text)
            r.bold = lvl == 1
            r.font.size = Pt(11 if lvl == 1 else 10)
            if lvl == 1:
                r.font.color.rgb = BRAND_DEEP

    footer(doc, "FBT RAPID (RPL) - Hướng dẫn sử dụng web dashboard - Forte Biotech")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    doc.save(out_path)
    print(f"{out_path}  ({out_path.stat().st_size // 1024} KB, {len(headings)} headings)")


if __name__ == "__main__":
    build(SRC, OUT)
