// Small SVG charts for the dashboard. No library.
// Every text goes in with textContent, so data from the network cannot add HTML.
// Rules followed: one series per chart, 2px line, 1px solid grid, red only for
// warnings, hover crosshair with tooltip, and an aria-label summary per chart.

const SVG_NS = "http://www.w3.org/2000/svg";

// Each chart is drawn at its real pixel width, so text is the same size in all charts.
function chartWidth(wrapper) {
  return wrapper.clientWidth || 600;
}
const PAD_LEFT = 44;
const PAD_RIGHT = 12;
const PAD_TOP = 12;
const PAD_BOTTOM = 24;

function svgElement(name, attributes) {
  const element = document.createElementNS(SVG_NS, name);
  for (const [key, value] of Object.entries(attributes)) {
    element.setAttribute(key, value);
  }
  return element;
}

function svgText(x, y, text, className, anchor) {
  const element = svgElement("text", { x: x, y: y, class: className, "text-anchor": anchor || "start" });
  element.textContent = text;
  return element;
}

function shortTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
}

function longTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString();
}

// Tick every 1, 5 or 15 minutes, so there are about 5 labels on the axis.
function timeTicks(start, end) {
  const minutes = (end - start) / 60;
  let step = 60;
  if (minutes > 10) {
    step = 300;
  }
  if (minutes > 30) {
    step = 900;
  }
  const ticks = [];
  for (let t = Math.ceil(start / step) * step; t <= end; t += step) {
    ticks.push(t);
  }
  return ticks;
}

function niceMax(value) {
  if (value <= 0) {
    return 1;
  }
  const power = Math.pow(10, Math.floor(Math.log10(value)));
  for (const step of [1, 2, 2.5, 5, 10]) {
    if (step * power >= value) {
      return step * power;
    }
  }
  return 10 * power;
}

function drawTimeAxis(svg, start, end, height, xOf) {
  for (const tick of timeTicks(start, end)) {
    const x = xOf(tick);
    svg.append(svgElement("line", { x1: x, x2: x, y1: PAD_TOP, y2: height - PAD_BOTTOM, class: "grid" }));
    svg.append(svgText(x, height - 6, shortTime(tick), "axis-label", "middle"));
  }
}

// The tooltip is one HTML box per chart, placed over the SVG.
function makeTooltip(wrapper) {
  const tip = document.createElement("div");
  tip.className = "chart-tip";
  tip.hidden = true;
  wrapper.append(tip);
  return tip;
}

function placeTooltip(tip, wrapper, x, lines) {
  tip.replaceChildren();
  for (const line of lines) {
    const row = document.createElement("div");
    row.textContent = line;
    tip.append(row);
  }
  tip.hidden = false;

  let left = x + 12;
  if (left + tip.offsetWidth > wrapper.clientWidth) {
    left = x - tip.offsetWidth - 12;
  }
  tip.style.left = `${Math.max(0, left)}px`;
}

function pointerX(svg, event) {
  return event.clientX - svg.getBoundingClientRect().left;
}

// Line chart of one value over time. A null value breaks the line, so a
// failed sensor shows as a gap instead of a fake drop to zero.
// points: [{t, value}]  options: {start, end, unit, name, threshold, thresholdError, thresholdLabel}
function drawLineChart(wrapper, points, options) {
  const width = chartWidth(wrapper);
  const height = 120;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img" });
  const values = points.filter((point) => point.value !== null).map((point) => point.value);

  let top = values.length > 0 ? Math.max(...values) : 0;
  if (options.threshold !== undefined) {
    top = Math.max(top, options.threshold + (options.thresholdError || 0));
  }
  top = niceMax(top * 1.1);

  const plotWidth = width - PAD_LEFT - PAD_RIGHT;
  const plotHeight = height - PAD_TOP - PAD_BOTTOM;
  const xOf = (t) => PAD_LEFT + ((t - options.start) / (options.end - options.start)) * plotWidth;
  const yOf = (value) => PAD_TOP + plotHeight - (value / top) * plotHeight;

  for (const fraction of [0, 0.5, 1]) {
    const y = yOf(top * fraction);
    svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: y, y2: y, class: "grid" }));
    svg.append(svgText(PAD_LEFT - 6, y + 4, String(Math.round(top * fraction)), "axis-label", "end"));
  }
  drawTimeAxis(svg, options.start, options.end, height, xOf);

  if (options.threshold !== undefined) {
    // Error band: shaded area between threshold - error and threshold + error,
    // dashed line on each edge. Never below 0, the bottom of the chart.
    const error = options.thresholdError || 0;
    if (error > 0) {
      const yHigh = yOf(options.threshold + error);
      const yLow = yOf(Math.max(0, options.threshold - error));
      svg.append(svgElement("rect", { x: PAD_LEFT, y: yHigh, width: plotWidth, height: yLow - yHigh, class: "threshold-band" }));
      svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: yHigh, y2: yHigh, class: "threshold-edge" }));
      svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: yLow, y2: yLow, class: "threshold-edge" }));
    }
    const y = yOf(options.threshold);
    svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: y, y2: y, class: "threshold" }));
    svg.append(svgText(width - PAD_RIGHT, yOf(options.threshold + error) - 5, options.thresholdLabel, "axis-label", "end"));
  }

  // Split into runs at each null, then draw each run as area + line.
  const runs = [];
  let run = [];
  for (const point of points) {
    if (point.value === null) {
      if (run.length > 0) {
        runs.push(run);
      }
      run = [];
    } else {
      run.push(point);
    }
  }
  if (run.length > 0) {
    runs.push(run);
  }

  for (const part of runs) {
    const line = part.map((point) => `${xOf(point.t).toFixed(1)},${yOf(point.value).toFixed(1)}`).join(" ");
    const baseline = yOf(0).toFixed(1);
    const first = xOf(part[0].t).toFixed(1);
    const last = xOf(part[part.length - 1].t).toFixed(1);
    svg.append(svgElement("polygon", { points: `${first},${baseline} ${line} ${last},${baseline}`, class: "area" }));
    svg.append(svgElement("polyline", { points: line, class: "line" }));
  }

  const lastPoint = points.length > 0 ? points[points.length - 1] : null;
  if (lastPoint !== null && lastPoint.value !== null) {
    svg.append(svgElement("circle", { cx: xOf(lastPoint.t), cy: yOf(lastPoint.value), r: 4.5, class: "end-dot" }));
  }

  // Summary for screen readers.
  let summary = `${options.name}, no data in this time range`;
  if (values.length > 0) {
    summary = `${options.name}: latest ${showNumber(lastPoint.value)}, lowest ${Math.min(...values)}, highest ${Math.max(...values)} ${options.unit}`;
  }
  svg.setAttribute("aria-label", summary);

  // Hover: a crosshair snaps to the nearest point.
  const crosshair = svgElement("line", { y1: PAD_TOP, y2: height - PAD_BOTTOM, class: "crosshair", visibility: "hidden" });
  const hoverDot = svgElement("circle", { r: 4, class: "hover-dot", visibility: "hidden" });
  svg.append(crosshair, hoverDot);

  wrapper.replaceChildren(svg);
  const tip = makeTooltip(wrapper);

  svg.addEventListener("pointermove", (event) => {
    if (points.length === 0) {
      return;
    }
    const x = pointerX(svg, event);
    let nearest = points[0];
    for (const point of points) {
      if (Math.abs(xOf(point.t) - x) < Math.abs(xOf(nearest.t) - x)) {
        nearest = point;
      }
    }
    const nx = xOf(nearest.t);
    crosshair.setAttribute("x1", nx);
    crosshair.setAttribute("x2", nx);
    crosshair.setAttribute("visibility", "visible");

    if (nearest.value === null) {
      hoverDot.setAttribute("visibility", "hidden");
    } else {
      hoverDot.setAttribute("cx", nx);
      hoverDot.setAttribute("cy", yOf(nearest.value));
      hoverDot.setAttribute("visibility", "visible");
    }

    placeTooltip(tip, wrapper, nx, [longTime(nearest.t), `${options.name}: ${showNumber(nearest.value)} ${nearest.value === null ? "" : options.unit}`]);
  });

  svg.addEventListener("pointerleave", () => {
    crosshair.setAttribute("visibility", "hidden");
    hoverDot.setAttribute("visibility", "hidden");
    tip.hidden = true;
  });
}

function showNumber(value) {
  if (value === null) {
    return "no reading";
  }
  return String(value);
}

// One row per node, one tick per message. A gap longer than the stale limit
// is shaded red: that is where Wi-Fi or power was lost.
// rows: [{label, times}]  options: {start, end, staleAfter}
function drawTimeline(wrapper, rows, options) {
  const width = chartWidth(wrapper);
  const rowHeight = 30;
  const labelWidth = 70;
  const height = PAD_TOP + rows.length * rowHeight + PAD_BOTTOM;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img" });

  const left = PAD_LEFT + labelWidth - 44;
  const plotWidth = width - left - PAD_RIGHT;
  const xOf = (t) => left + ((t - options.start) / (options.end - options.start)) * plotWidth;

  drawTimeAxis(svg, options.start, options.end, height, xOf);

  const gapsText = [];
  rows.forEach((row, index) => {
    const y = PAD_TOP + index * rowHeight;
    const middle = y + rowHeight / 2;
    svg.append(svgText(0, middle + 4, row.label, "row-label"));
    svg.append(svgElement("line", { x1: left, x2: width - PAD_RIGHT, y1: middle, y2: middle, class: "grid" }));

    // Gaps between messages, and from the last message until now.
    const edges = [...row.times];
    if (edges.length > 0) {
      edges.push(options.end);
    }
    let gapCount = 0;
    for (let i = 1; i < edges.length; i++) {
      const gap = edges[i] - edges[i - 1];
      if (gap > options.staleAfter) {
        gapCount += 1;
        const x1 = xOf(edges[i - 1]);
        const x2 = xOf(edges[i]);
        const band = svgElement("rect", { x: x1, y: y + 4, width: Math.max(2, x2 - x1), height: rowHeight - 8, class: "gap" });
        const title = svgElement("title", {});
        title.textContent = `${row.label}: no message for ${Math.round(gap)} s (${longTime(edges[i - 1])} to ${longTime(edges[i])})`;
        band.append(title);
        svg.append(band);
        if (x2 - x1 > 60) {
          svg.append(svgText((x1 + x2) / 2, middle + 4, `gap ${Math.round(gap)} s`, "gap-label", "middle"));
        }
      }
    }
    if (gapCount > 0) {
      gapsText.push(`${row.label} ${gapCount} gap${gapCount > 1 ? "s" : ""}`);
    }

    for (const t of row.times) {
      const x = xOf(t);
      svg.append(svgElement("line", { x1: x, x2: x, y1: y + 7, y2: y + rowHeight - 7, class: "tick" }));
    }
  });

  let summary = "Messages received. No gaps.";
  if (gapsText.length > 0) {
    summary = `Messages received. ${gapsText.join(", ")}.`;
  }
  svg.setAttribute("aria-label", summary);
  wrapper.replaceChildren(svg);
}

// A band per activity, labelled with text so it never depends on colour.
// points: [{t, activity}]  options: {start, end, names}
function drawActivityStrip(wrapper, points, options) {
  const width = chartWidth(wrapper);
  const height = 44;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img" });
  const plotWidth = width - PAD_LEFT - PAD_RIGHT;
  const xOf = (t) => PAD_LEFT + ((t - options.start) / (options.end - options.start)) * plotWidth;

  // Join runs of the same activity into one band.
  const bands = [];
  for (const point of points) {
    const lastBand = bands[bands.length - 1];
    if (lastBand !== undefined && lastBand.activity === point.activity) {
      continue;
    }
    if (lastBand !== undefined) {
      lastBand.end = point.t;
    }
    bands.push({ activity: point.activity, start: point.t, end: options.end });
  }

  for (const band of bands) {
    const x1 = xOf(band.start);
    // 2px of surface between bands keeps neighbours apart without borders.
    const width = Math.max(1, xOf(band.end) - x1 - 2);
    const name = options.names[band.activity] || "Unknown";
    const rect = svgElement("rect", { x: x1, y: 6, width: width, height: 26, rx: 3, class: `band band-${band.activity}` });
    const title = svgElement("title", {});
    title.textContent = `${name}: ${longTime(band.start)} to ${longTime(band.end)}`;
    rect.append(title);
    svg.append(rect);

    if (width > name.length * 7 + 8) {
      svg.append(svgText(x1 + 6, 23, name, `band-label band-label-${band.activity}`));
    }
  }

  const current = bands.length > 0 ? options.names[bands[bands.length - 1].activity] : "no data";
  svg.setAttribute("aria-label", `Activity over time. Now: ${current}. ${bands.length} changes.`);
  wrapper.replaceChildren(svg);
}

// Half-circle gauge made of segments, like a fuel or power meter.
// Each segment takes the colour of its zone (good, warn, bad, off). Segments up
// to the value are lit, the rest are faded, and a marker points at the value.
// zones: [{upTo, level, text}] in order, the last upTo is the end of the scale.
// options: {name, label, unit}
function drawGauge(wrapper, value, zones, options) {
  const max = zones[zones.length - 1].upTo;
  const segments = 30;
  const cx = 110;
  const cy = 112;
  const radius = 84;
  const svg = svgElement("svg", { viewBox: "0 0 220 142", role: "img" });

  // Fraction 0 is the left end of the arc, 1 is the right end.
  const pointAt = (fraction, r) => {
    const angle = Math.PI * (1 - fraction);
    return [cx + r * Math.cos(angle), cy - r * Math.sin(angle)];
  };
  const zoneOf = (number) => zones.find((zone) => number <= zone.upTo) || zones[zones.length - 1];

  for (let i = 0; i < segments; i++) {
    const start = i / segments + 0.004;
    const end = (i + 1) / segments - 0.004;
    const middleValue = ((i + 0.5) / segments) * max;
    const [x1, y1] = pointAt(start, radius);
    const [x2, y2] = pointAt(end, radius);
    let className = `segment segment-${zoneOf(middleValue).level}`;
    if (value === null || middleValue > value) {
      className += " faded";
    }
    svg.append(svgElement("path", { d: `M ${x1.toFixed(1)} ${y1.toFixed(1)} A ${radius} ${radius} 0 0 1 ${x2.toFixed(1)} ${y2.toFixed(1)}`, class: className }));
  }

  svg.append(svgText(cx - radius, cy + 20, "0", "axis-label", "middle"));
  svg.append(svgText(cx + radius, cy + 20, String(max), "axis-label", "middle"));

  let zoneText = "no reading";
  let level = "off";
  if (value !== null) {
    const zone = zoneOf(value);
    zoneText = zone.text;
    level = zone.level;
    // Marker: a triangle just outside the arc, pointing in at the value.
    const fraction = Math.min(1, Math.max(0, value / max));
    const angle = Math.PI * (1 - fraction);
    const tip = pointAt(fraction, radius + 10);
    const sideA = [cx + (radius + 21) * Math.cos(angle + 0.05), cy - (radius + 21) * Math.sin(angle + 0.05)];
    const sideB = [cx + (radius + 21) * Math.cos(angle - 0.05), cy - (radius + 21) * Math.sin(angle - 0.05)];
    const corners = [tip, sideA, sideB].map((point) => `${point[0].toFixed(1)},${point[1].toFixed(1)}`).join(" ");
    svg.append(svgElement("polygon", { points: corners, class: "gauge-marker" }));
    // Short needle inside the arc, so it never covers the number.
    const [ax, ay] = pointAt(fraction, radius - 36);
    const [bx, by] = pointAt(fraction, radius - 12);
    svg.append(svgElement("line", { x1: ax.toFixed(1), y1: ay.toFixed(1), x2: bx.toFixed(1), y2: by.toFixed(1), class: "gauge-needle" }));
  }

  // "—" fits inside the arc; the zone line under it says "no reading".
  svg.append(svgText(cx, cy - 4, value === null ? "—" : String(value), "gauge-value", "middle"));
  svg.append(svgText(cx, cy + 24, zoneText, `gauge-zone gauge-zone-${level}`, "middle"));
  svg.setAttribute("aria-label", `${options.name}: ${showNumber(value)} ${value === null ? "" : options.unit}, ${zoneText}`);

  const title = document.createElement("p");
  title.className = "gauge-title";
  title.textContent = options.label;
  wrapper.replaceChildren(title, svg);
}
