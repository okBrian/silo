#!/usr/bin/env python3
"""Generate report tables and a count figure from independently replayed facts."""
import json
from pathlib import Path
from reportlab.graphics import renderPDF, renderSVG
from reportlab.graphics.shapes import Drawing, Rect, String, Line
from reportlab.lib.colors import HexColor

OUT = Path(__file__).resolve().parent / "generated"
facts = json.loads((OUT / "facts.json").read_text())


def table(name, columns, header, rows):
    text = "\\begin{tabular}{" + columns + "}\n\\toprule\n"
    text += " & ".join(header) + " \\\\\n\\midrule\n"
    text += "".join(" & ".join(map(str, row)) + " \\\\\n" for row in rows)
    text += "\\bottomrule\n\\end{tabular}\n"
    (OUT / name).write_text(text)


table("smoke.tex", "llrrrrrr", ["Policy", "W/R", "Commits", "Short", "Q2", "Window", "Drain", "Handoffs"],
      [("Wait" if r["policy"] == "wait" else "Coop.", f'{r["workers"]}/{r["repeat"]}',
        r["commits"], r["shorts"], r["q2"], r["window"], r["drain"], r["handoffs"]) for r in facts["smoke"]])
table("latency.tex", "llrrrrr", ["Policy", "Type", "$n$", "p50", "p90", "p99", "p99.9*"],
      [("Wait" if r["policy"] == "wait" else "Coop.", r["type"], r["end_to_end_n"],
        *(f'{float(r[f"end_to_end_{p}_us"]):.1f}' for p in ("p50", "p90", "p99", "p99_9")))
       for r in facts["per_type"] if r["workers"] == r["repeat"] == "1"])
table("pressure.tex", "rrrrrrrr", ["Budget", "W", "Q2", "Short", "Reject", "Capture", "Return", "Block"],
      [(f'{r["threshold"]}\\%', r["workers"], r["q2_commits"], r["short_commits"], r["rejected"],
        r["async_captures"], r["forced_returns"], r["admission_blocks"]) for r in facts["pressure"]])

d = Drawing(480, 205)
blue, gray, ink = map(HexColor, ("#276281", "#d6dce1", "#172f40"))
d.add(String(0, 191, "Linux-signal pressure: every scheduled short request accounted for", fontSize=10, fillColor=ink))
left, scale = 92, 340 / 24000
for i, r in enumerate(facts["pressure"]):
    y = 151 - i * 33
    d.add(String(0, y + 6, f'{r["threshold"]}% / {r["workers"]} worker' + ("s" if r["workers"] == 2 else ""), fontSize=9))
    a, b = r["short_commits"] * scale, r["rejected"] * scale
    d.add(Rect(left, y, a, 20, fillColor=blue, strokeColor=None))
    d.add(Rect(left + a, y, b, 20, fillColor=gray, strokeColor=None))
    d.add(String(left + a / 2, y + 7, f'{r["short_commits"]:,}', fontSize=8, fillColor=HexColor("#ffffff"), textAnchor="middle"))
    d.add(String(left + a + b / 2, y + 7, f'{r["rejected"]:,}', fontSize=8, fillColor=ink, textAnchor="middle"))
for x in (0, 12000, 24000):
    d.add(String(left + x * scale, 25, f"{x:,}", fontSize=8, textAnchor="middle"))
d.add(Line(left, 39, left + 340, 39, strokeColor=gray))
d.add(Rect(92, 2, 9, 9, fillColor=blue, strokeColor=None))
d.add(String(106, 2, "Committed", fontSize=9))
d.add(Rect(205, 2, 9, 9, fillColor=gray, strokeColor=None))
d.add(String(219, 2, "Queue-full rejection", fontSize=9))
renderPDF.drawToFile(d, str(OUT / "pressure-outcomes.pdf"))
renderSVG.drawToFile(d, str(OUT / "pressure-outcomes.svg"))
print("PASS generated three tables and pressure count figure from facts.json")
