// SVG chart drawing only. No fetch and no knowledge of nodes: callers pass points and options.

const CHART_HEIGHT = 180;
const PAD = { left: 44, right: 12, top: 12, bottom: 24 };
const TIMELINE_ROW_H = 30;
const TIMELINE_LABEL_W = 70;
const ACTIVITY_H = 44;
const ACTIVITY_BAND_Y = 9;
const ACTIVITY_BAND_H = 26;
// Tick every N minutes, so each window shows about 5 labels.
const TICK_STEP_MIN = { 5: 1, 15: 5, 60: 15 };
const BAND_CLASS = {
  idle: "band-idle",
  recording: "band-busy",
  waiting_ai: "band-busy",
  feedback: "band-busy",
  stopped: "band-stopped",
};

function svgEl(name, attrs) {
  const el = document.createElementNS("http://www.w3.org/2000/svg", name);
  for (const [key, value] of Object.entries(attrs || {})) {
    el.setAttribute(key, String(value));
  }
  return el;
}

function svgText(x, y, text, className, anchor) {
  const el = svgEl("text", { x: x, y: y, class: className, "text-anchor": anchor || "start" });
  el.textContent = text;
  return el;
}

function svgTitle(text) {
  const el = svgEl("title");
  el.textContent = text;
  return el;
}

function pad2(n) {
  return String(n).padStart(2, "0");
}

function clockHM(seconds) {
  const d = new Date(seconds * 1000);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
}

function clockHMS(seconds) {
  const d = new Date(seconds * 1000);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}:${pad2(d.getSeconds())}`;
}

function formatNumber(value) {
  return String(Math.round(value * 10) / 10);
}

// Width is the real pixel width, so 11px text keeps its size in every chart.
function wrapperWidth(wrapper) {
  return wrapper.clientWidth || 600;
}

// "Nice" axis maximum: 1, 2, 2.5, 5, 10 times a power of ten, at or above target.
function niceMax(target) {
  for (const scale of [0.1, 1, 10, 100, 1000, 10000, 100000]) {
    for (const step of [1, 2, 2.5, 5, 10]) {
      if (step * scale >= target) {
        return step * scale;
      }
    }
  }
  return target;
}

function timeToX(t, options, width) {
  const plotWidth = width - PAD.left - PAD.right;
  return PAD.left + ((t - options.start) / (options.end - options.start)) * plotWidth;
}

function addTimeLabels(svg, options, width, y) {
  const stepS = (TICK_STEP_MIN[options.minutes] || 5) * 60;
  for (let t = Math.ceil(options.start / stepS) * stepS; t <= options.end; t += stepS) {
    svg.append(svgText(timeToX(t, options, width), y, clockHM(t), "axis-text", "middle"));
  }
}

function nearestPoint(points, x, options, width) {
  let best = null;
  let bestDistance = Infinity;
  for (const point of points) {
    const distance = Math.abs(timeToX(point.t, options, width) - x);
    if (distance < bestDistance) {
      best = point;
      bestDistance = distance;
    }
  }
  return best;
}

// points: [{t, value}] with value null for a failed sensor.
// options: {start, end, minutes, label, unit, threshold, thresholdLabel}
function drawLineChart(wrapper, points, options) {
  const width = wrapperWidth(wrapper);
  const plotBottom = CHART_HEIGHT - PAD.bottom;
  const innerH = plotBottom - PAD.top;
  const values = points.filter((p) => p.value !== null).map((p) => p.value);
  const highest = values.length ? Math.max(...values) : 0;
  const lowest = values.length ? Math.min(...values) : 0;
  const latest = values.length ? points.filter((p) => p.value !== null).pop().value : null;
  const threshold = options.threshold ?? null;
  const top = niceMax(Math.max(highest * 1.1, 1, threshold !== null ? threshold * 1.05 : 0));
  const yOf = (v) => PAD.top + (1 - v / top) * innerH;
  const xOf = (t) => timeToX(t, options, width);

  let summary = `${options.label}: no readings`;
  if (latest !== null) {
    summary = `${options.label}: latest ${latest}, lowest ${lowest}, highest ${highest} ${options.unit}`.trim();
  }
  const svg = svgEl("svg", { viewBox: `0 0 ${width} ${CHART_HEIGHT}`, role: "img", "aria-label": summary });

  for (const value of [0, top / 2, top]) {
    const y = yOf(value);
    svg.append(svgEl("line", { x1: PAD.left, x2: width - PAD.right, y1: y, y2: y, class: "grid-line" }));
    svg.append(svgText(PAD.left - 6, y + 4, formatNumber(value), "axis-text", "end"));
  }
  addTimeLabels(svg, options, width, CHART_HEIGHT - 6);

  if (threshold !== null) {
    const y = yOf(threshold);
    svg.append(svgEl("line", { x1: PAD.left, x2: width - PAD.right, y1: y, y2: y, class: "threshold-line" }));
    svg.append(svgText(width - PAD.right, y - 4, options.thresholdLabel, "threshold-text", "end"));
  }

  // A null value ends the line, so a failed sensor is a gap, never a drop to zero.
  const runs = [];
  let run = [];
  for (const point of points) {
    if (point.value === null) {
      if (run.length) runs.push(run);
      run = [];
    } else {
      run.push(point);
    }
  }
  if (run.length) runs.push(run);

  for (const current of runs) {
    const xy = current.map((p) => [xOf(p.t), yOf(p.value)]);
    const line = xy.map((pt, i) => `${i ? "L" : "M"}${pt[0].toFixed(1)} ${pt[1].toFixed(1)}`).join(" ");
    const last = xy[xy.length - 1][0].toFixed(1);
    const first = xy[0][0].toFixed(1);
    svg.append(svgEl("path", { d: `M${first} ${plotBottom} ${line.replace(/^M/, "L")} L${last} ${plotBottom} Z`, class: "series-area" }));
    svg.append(svgEl("path", { d: line, class: "series" }));
  }

  if (latest !== null) {
    const lastPoint = points.filter((p) => p.value !== null).pop();
    svg.append(svgEl("circle", { cx: xOf(lastPoint.t), cy: yOf(lastPoint.value), r: 4, class: "series-dot" }));
  }

  // Hover: crosshair, dot and tooltip. Only the nearest point by x is shown.
  const crosshair = svgEl("line", { y1: PAD.top, y2: plotBottom, class: "crosshair", visibility: "hidden" });
  const hoverDot = svgEl("circle", { r: 4, class: "hover-dot", visibility: "hidden" });
  svg.append(crosshair, hoverDot);

  const tip = document.createElement("div");
  tip.className = "chart-tip";
  tip.hidden = true;
  wrapper.replaceChildren(svg, tip);

  svg.addEventListener("pointermove", (event) => {
    if (points.length === 0) return;
    const rect = svg.getBoundingClientRect();
    const xInSvg = ((event.clientX - rect.left) * width) / rect.width;
    const point = nearestPoint(points, xInSvg, options, width);
    const x = xOf(point.t);
    crosshair.setAttribute("x1", x);
    crosshair.setAttribute("x2", x);
    crosshair.setAttribute("visibility", "visible");
    if (point.value === null) {
      hoverDot.setAttribute("visibility", "hidden");
      tip.textContent = `${clockHMS(point.t)}  ${options.label}: no reading`;
    } else {
      hoverDot.setAttribute("cx", x);
      hoverDot.setAttribute("cy", yOf(point.value));
      hoverDot.setAttribute("visibility", "visible");
      tip.textContent = `${clockHMS(point.t)}  ${options.label}: ${point.value} ${options.unit}`.trim();
    }
    tip.hidden = false;
    const wrapRect = wrapper.getBoundingClientRect();
    const pointerX = event.clientX - wrapRect.left;
    let left = pointerX + 12;
    if (left + tip.offsetWidth > wrapper.clientWidth) {
      left = pointerX - 12 - tip.offsetWidth;
    }
    tip.style.left = `${Math.max(0, left)}px`;
  });

  svg.addEventListener("pointerleave", () => {
    crosshair.setAttribute("visibility", "hidden");
    hoverDot.setAttribute("visibility", "hidden");
    tip.hidden = true;
  });
}

// rows: [{name, times: [t, ...]}] sorted by time. A gap longer than staleAfter is a red band.
// options: {start, end, minutes, staleAfter, now}
function drawTimeline(wrapper, rows, options) {
  const width = wrapperWidth(wrapper);
  const plotLeft = TIMELINE_LABEL_W;
  const plotRight = width - PAD.right;
  const plotWidth = plotRight - plotLeft;
  const xOf = (t) => plotLeft + ((t - options.start) / (options.end - options.start)) * plotWidth;
  const height = rows.length * TIMELINE_ROW_H + PAD.bottom;
  const svg = svgEl("svg", { viewBox: `0 0 ${width} ${height}`, role: "img" });

  const gapSummaries = [];
  rows.forEach((row, index) => {
    const top = index * TIMELINE_ROW_H + 6;
    const barH = 18;
    svg.append(svgText(0, top + 13, row.name, "row-label"));
    if (row.times.length === 0) return;

    const bands = [];
    for (let i = 1; i < row.times.length; i++) {
      if (row.times[i] - row.times[i - 1] > options.staleAfter) {
        bands.push([row.times[i - 1], row.times[i]]);
      }
    }
    const last = row.times[row.times.length - 1];
    if (options.now - last > options.staleAfter) {
      bands.push([last, options.now]);
    }

    for (const [from, to] of bands) {
      const x = xOf(Math.max(from, options.start));
      const w = Math.max(xOf(to) - x, 1);
      const seconds = Math.round(to - from);
      const band = svgEl("rect", { x: x, y: top, width: w, height: barH, class: "gap-band" });
      band.append(svgTitle(`${row.name}: no message from ${clockHMS(from)} to ${clockHMS(to)} (${seconds} s)`));
      svg.append(band);
      if (w > 60) {
        svg.append(svgText(x + 4, top + 13, `gap ${seconds} s`, "gap-text"));
      }
    }

    for (const t of row.times) {
      if (t < options.start || t > options.end) continue;
      svg.append(svgEl("rect", { x: xOf(t) - 1, y: top, width: 2, height: barH, class: "tick" }));
    }
    if (bands.length) gapSummaries.push(`${row.name} ${bands.length} gaps`);
  });

  addTimeLabels(svg, options, width, height - 6);
  svg.setAttribute("aria-label", gapSummaries.length ? `Messages received. ${gapSummaries.join(", ")}.` : "Messages received. No gaps.");
  wrapper.replaceChildren(svg);
}

// points: [{t, activity}] sorted by time. Same activity in a row becomes one band.
// options: {start, end, minutes, labelFor(activity)}
function drawActivityStrip(wrapper, points, options) {
  const width = wrapperWidth(wrapper);
  const plotLeft = PAD.left;
  const plotRight = width - PAD.right;
  const xOf = (t) => plotLeft + ((t - options.start) / (options.end - options.start)) * (plotRight - plotLeft);

  const bands = [];
  points.forEach((point, index) => {
    const activity = point.activity ?? "none";
    const to = index + 1 < points.length ? points[index + 1].t : options.end;
    const last = bands[bands.length - 1];
    if (last && last.activity === activity) {
      last.end = to;
    } else {
      bands.push({ activity: activity, start: point.t, end: to });
    }
  });

  const svg = svgEl("svg", { viewBox: `0 0 ${width} ${ACTIVITY_H}`, role: "img" });
  for (const band of bands) {
    const x = xOf(band.start) + 1;
    const w = Math.max(xOf(band.end) - xOf(band.start) - 2, 2);
    const name = options.labelFor(band.activity);
    const rect = svgEl("rect", {
      x: x, y: ACTIVITY_BAND_Y, width: w, height: ACTIVITY_BAND_H,
      class: `band ${BAND_CLASS[band.activity] || "band-default"}`,
    });
    rect.append(svgTitle(`${name}: ${clockHMS(band.start)} to ${clockHMS(band.end)}`));
    svg.append(rect);
    if (w > name.length * 7 + 8) {
      const textClass = band.activity === "stopped" ? "band-text on-red" : "band-text";
      svg.append(svgText(x + 6, ACTIVITY_BAND_Y + 17, name, textClass));
    }
  }

  const changes = Math.max(bands.length - 1, 0);
  const now = bands.length ? options.labelFor(bands[bands.length - 1].activity) : "no data";
  svg.setAttribute("aria-label", `Activity over time. Now: ${now}. ${changes} changes.`);
  wrapper.replaceChildren(svg);
}
