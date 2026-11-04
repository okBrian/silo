#!/usr/bin/env python3
"""Render I05 analysis JSON as SVG charts using ReportLab."""

import argparse
import json
from pathlib import Path

from reportlab.graphics import renderSVG
from reportlab.graphics.charts.barcharts import VerticalBarChart
from reportlab.graphics.shapes import Drawing, Rect, String
from reportlab.lib.colors import HexColor


PALETTE = [HexColor("#235789"), HexColor("#b54f25")]
INK = HexColor("#243447")


def bar_chart(path, title, detail, units, labels, series):
    width, height = 780, 375
    drawing = Drawing(width, height)
    drawing.add(Rect(0, 0, width, height, strokeColor=None, fillColor=HexColor("#ffffff")))
    drawing.add(String(28, 345, title, fontName="Helvetica-Bold", fontSize=17, fillColor=INK))
    drawing.add(String(28, 323, detail, fontSize=10, fillColor=INK))
    drawing.add(String(28, 291, units, fontSize=11, fillColor=INK))

    chart = VerticalBarChart()
    chart.x, chart.y, chart.width, chart.height = 88, 76, 660, 195
    chart.data = [values for _, values in series]
    chart.valueAxis.valueMin = 0
    chart.valueAxis.valueMax = max(map(max, chart.data)) * 1.12 or 1
    chart.valueAxis.labelTextFormat = lambda tick: f"{tick:,g}"
    chart.valueAxis.labels.dx = -4
    chart.valueAxis.visibleGrid = True
    chart.valueAxis.gridStrokeColor = HexColor("#dce3e9")
    chart.categoryAxis.categoryNames = labels
    chart.categoryAxis.labels.fontSize = 10
    chart.categoryAxis.labels.boxAnchor = "n"
    chart.categoryAxis.labels.dy = -8
    for index in range(len(series)):
        chart.bars[index].fillColor = PALETTE[index]
        chart.bars[index].strokeColor = None
    drawing.add(chart)

    for index, (name, _) in enumerate(series):
        x = 88 + index * 142
        drawing.add(Rect(x, 24, 12, 12, strokeColor=None, fillColor=PALETTE[index]))
        drawing.add(String(x + 18, 25, name, fontSize=10, fillColor=INK))
    renderSVG.drawToFile(drawing, str(path), svgAttrs={"role": "img", "aria-label": f"{title}. {detail}"})


def plot(analysis, output):
    output.mkdir(parents=True, exist_ok=True)
    stock = [row for row in analysis["stock_summary"] if row["mean_txn_s"] is not None]
    for offset in range(0, len(stock), 8):
        panel = stock[offset:offset + 8]
        labels = [f"v{r['variant']} {r['workers']}w/{r['warehouses']}wh n={r['throughput_n']}"
                  for r in panel]
        bar_chart(output / f"stock-throughput-{offset // 8 + 1}.svg",
                  "Stock TPC-C throughput",
                  f"{panel[0]['config']} · {panel[0]['runtime_s']} s per run · n is the number of runs",
                  "Committed transactions/s", labels, [("Mean throughput", [r["mean_txn_s"] for r in panel])])

    usable = {"complete", "phase-verified"}
    queued = [row for row in analysis["queued_summary"]
              if row["phase"] == "measurement" and row["type"] == "All"
              and row["status"] in usable and row.get("end_to_end_n")]
    manifests = {run["run_id"]: run for run in analysis["runs"]}
    for offset in range(0, len(queued), 8):
        panel = queued[offset:offset + 8]
        labels = []
        for row in panel:
            manifest = manifests[row["run_id"]]
            label = (f"r{manifest['repeat']}/v{manifest['variant']}" if "variant" in manifest
                     else row["run_id"])
            labels.append(f"{label} · n={row['end_to_end_n']}")
        for metric, title in (("queue", "Queue delay"), ("end_to_end", "End-to-end latency")):
            series = [(rank.upper(), [r[f"{metric}_{rank}_us"] for r in panel]) for rank in ("p50", "p99")]
            bar_chart(output / f"queued-{metric}-{offset // 8 + 1}.svg", f"Queued TPC-C · {title}",
                      "Measured arrival cohort · n counts committed requests, including drain commits",
                      "Microseconds", labels, series)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("analysis", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    plot(json.loads(args.analysis.read_text(encoding="utf-8")), args.output)


if __name__ == "__main__":
    main()
