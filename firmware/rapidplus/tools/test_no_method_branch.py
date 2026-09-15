#!/usr/bin/env python3
"""
Guard the invariant that stopped WiFi forget/connect from working on the device:

    A dashboard route handler must NOT branch on req->method() (or request->method()).

WHY THIS IS THE RIGHT TEST FOR THIS BUG
This build hits GOTCHA 3: project headers pull WebServer.h (which sets WEBSERVER_H) BEFORE
ESPAsyncWebServer.h, so the HTTP_GET/HTTP_POST *symbols* resolve to WebServer.h's sequential
HTTPMethod values while `request->method()` returns AsyncWebServer's bit-flag enum. The two
representations DON'T MATCH, so `req->method() == HTTP_POST` is effectively always false.

handleWifiList branched on `req->method() == HTTP_POST`; every POST fell through to the GET
branch, so wifiStoreRemove()/connect never ran - "I click Forget but the WiFi stays" and
"Connect does nothing / web out of sync". POST /wifi worked only because it never compared
method() (it reads getParam(..., true) directly).

The permanent fix: dispatch on the PRESENCE OF PARAMS, not the method. Register one HTTP_ANY
handler per URI (like /rename) and branch on hasParam("remove"|"connect"|"action"). This test
enforces that no handler compares req->method(), so the mistake can't come back silently 40
minutes into a bench session.

Runs on the host, no hardware:  python tools/test_no_method_branch.py
Exit 0 = pass, 1 = fail.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"


def strip_comments(text: str) -> str:
    def _blank(m):
        return "".join(ch if ch == "\n" else " " for ch in m.group(0))

    text = re.sub(r"/\*.*?\*/", _blank, text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def main() -> int:
    # Any comparison of ->method() against something: `req->method() == HTTP_POST`,
    # `request->method() != HTTP_GET`, etc. The bit-flag/sequential mismatch makes these
    # unreliable in this build, so they are banned outright in route handlers.
    rx = re.compile(r"->method\s*\(\s*\)\s*(==|!=|>=|<=|>|<)")
    hits = []
    for path in sorted(SRC.rglob("*.cpp")) + sorted(SRC.rglob("*.h")):
        code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for i, line in enumerate(code.splitlines(), 1):
            if rx.search(line):
                hits.append((path.name, i, line.strip()))

    print(f"->method() comparisons found: {len(hits)}")
    for f, ln, s in hits:
        print(f"  !! {f}:{ln}: {s}")

    if hits:
        print(
            "\nFAIL: a handler compares req->method(). In this build (GOTCHA 3) the "
            "HTTP_GET/HTTP_POST symbols and request->method() use different enums, so the "
            "comparison is effectively always false and the POST branch never runs "
            "(WiFi forget/connect silently did nothing). Register the route as HTTP_ANY "
            "with one handler and dispatch on hasParam(...) instead."
        )
        return 1

    print("PASS: no route handler branches on req->method().")
    return 0


if __name__ == "__main__":
    sys.exit(main())
