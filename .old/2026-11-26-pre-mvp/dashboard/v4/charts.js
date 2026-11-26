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
  // 24 h "14:05" is shorter than "2:05 PM", so more ticks fit.
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: false });
}

function longTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour12: false });
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
// points: [{t, value}]
// options: {start, end, unit, name, threshold, thresholdError, thresholdLabel,
//           yMax (fixed top of the scale), step (true = hold each value until the next),
//           staleFrom (time of the last message when the node is not online)}
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
  if (options.yMax !== undefined) {
    top = options.yMax;
  }

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

  const lastPoint = points.length > 0 ? points[points.length - 1] : null;

  if (options.threshold !== undefined) {
    // The limit is grey while the value is fine, and red only when the latest
    // value is inside or below the band. Red always means "look here".
    const error = options.thresholdError || 0;
    let alert = "";
    if (lastPoint !== null && lastPoint.value !== null && lastPoint.value <= options.threshold + error) {
      alert = " alert";
    }
    // Error band: shaded area between threshold - error and threshold + error,
    // dashed line on each edge. Never below 0, the bottom of the chart.
    if (error > 0) {
      const yHigh = yOf(options.threshold + error);
      const yLow = yOf(Math.max(0, options.threshold - error));
      svg.append(svgElement("rect", { x: PAD_LEFT, y: yHigh, width: plotWidth, height: yLow - yHigh, class: `threshold-band${alert}` }));
      svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: yHigh, y2: yHigh, class: `threshold-edge${alert}` }));
      svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: yLow, y2: yLow, class: `threshold-edge${alert}` }));
    }
    const y = yOf(options.threshold);
    svg.append(svgElement("line", { x1: PAD_LEFT, x2: width - PAD_RIGHT, y1: y, y2: y, class: `threshold${alert}` }));
    svg.append(svgText(width - PAD_RIGHT, yOf(options.threshold + error) - 5, options.thresholdLabel, `threshold-label${alert}`, "end"));
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

  // No data since the last message: shade it, so the line does not just stop.
  if (options.staleFrom !== undefined) {
    const x1 = xOf(Math.max(options.start, options.staleFrom));
    const x2 = width - PAD_RIGHT;
    svg.append(svgElement("rect", { x: x1, y: PAD_TOP, width: Math.max(0, x2 - x1), height: plotHeight, class: "stale-span" }));
    svg.append(svgElement("line", { x1: x1, x2: x1, y1: PAD_TOP, y2: PAD_TOP + plotHeight, class: "stale-edge" }));
    if (x2 - x1 > 90) {
      svg.append(svgText((x1 + x2) / 2, PAD_TOP + plotHeight / 2 + 4, `no data since ${shortTime(options.staleFrom)}`, "stale-label", "middle"));
    }
  }

  for (const part of runs) {
    // Step line: hold the old value until the next reading, then jump.
    // A book on/off is a jump, not a slope.
    let corners = part;
    if (options.step) {
      corners = [];
      part.forEach((point, index) => {
        if (index > 0) {
          corners.push({ t: point.t, value: part[index - 1].value });
        }
        corners.push(point);
      });
    }
    const line = corners.map((point) => `${xOf(point.t).toFixed(1)},${yOf(point.value).toFixed(1)}`).join(" ");
    const baseline = yOf(0).toFixed(1);
    const first = xOf(part[0].t).toFixed(1);
    const last = xOf(part[part.length - 1].t).toFixed(1);
    svg.append(svgElement("polygon", { points: `${first},${baseline} ${line} ${last},${baseline}`, class: "area" }));
    svg.append(svgElement("polyline", { points: line, class: "line" }));
  }

  if (lastPoint !== null && lastPoint.value !== null && options.staleFrom === undefined) {
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
// points: [{t, activity}]  options: {start, end, names, staleFrom}
function drawActivityStrip(wrapper, points, options) {
  const width = chartWidth(wrapper);
  const height = 30;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img" });
  const plotWidth = width - PAD_LEFT - PAD_RIGHT;
  const xOf = (t) => PAD_LEFT + ((t - options.start) / (options.end - options.start)) * plotWidth;

  // A node that is not online: the last band stops at its last message.
  let lastEnd = options.end;
  if (options.staleFrom !== undefined) {
    lastEnd = options.staleFrom;
  }

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
    bands.push({ activity: point.activity, start: point.t, end: lastEnd });
  }

  for (const band of bands) {
    const x1 = xOf(band.start);
    // 2px of surface between bands keeps neighbours apart without borders.
    const width = Math.max(1, xOf(band.end) - x1 - 2);
    const name = options.names[band.activity] || "Unknown";
    const rect = svgElement("rect", { x: x1, y: 2, width: width, height: 26, rx: 3, class: `band band-${band.activity}` });
    const title = svgElement("title", {});
    title.textContent = `${name}: ${longTime(band.start)} to ${longTime(band.end)}`;
    rect.append(title);
    svg.append(rect);

    if (width > name.length * 7 + 8) {
      svg.append(svgText(x1 + 6, 19, name, `band-label band-label-${band.activity}`));
    }
  }

  const current = bands.length > 0 ? options.names[bands[bands.length - 1].activity] : "no data";
  svg.setAttribute("aria-label", `Activity over time. Now: ${current}. ${bands.length} changes.`);
  wrapper.replaceChildren(svg);
}

// One value on a card: the number, the zone word, and a thin bar under it.
// The bar zones are greys; only the value mark has colour, and it is red only
// for a problem zone. A node that is not online shows its last value in grey.
// zones: [{upTo, level, text}] in order, the last upTo is the end of the bar.
// options: {name, label, unit, current (true when the node is online), age}
function drawReadout(wrapper, value, zones, options) {
  const max = zones[zones.length - 1].upTo;
  const width = 200;
  const height = 14;
  const zoneOf = (number) => zones.find((zone) => number <= zone.upTo) || zones[zones.length - 1];

  let zoneText = "no reading";
  let level = "none";
  if (value !== null) {
    zoneText = zoneOf(value).text;
    level = zoneOf(value).level;
  }
  if (!options.current) {
    level = "old";
  }

  const title = document.createElement("p");
  title.className = "readout-title";
  title.textContent = options.label;

  const number = document.createElement("p");
  number.className = "readout-value";
  number.textContent = value === null ? "—" : String(value);
  if (value !== null && options.unit !== "") {
    const unit = document.createElement("span");
    unit.className = "readout-unit";
    unit.textContent = ` ${options.unit}`;
    number.append(unit);
  }

  const zone = document.createElement("p");
  zone.className = `readout-zone zone-${level}`;
  zone.textContent = zoneText;
  if (!options.current && value !== null) {
    zone.textContent = `last known: ${zoneText}`;
  }

  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height + 12}`, "aria-hidden": "true" });
  let from = 0;
  for (const part of zones) {
    const x1 = (from / max) * width;
    const x2 = (part.upTo / max) * width;
    svg.append(svgElement("rect", { x: x1, y: 3, width: Math.max(0, x2 - x1 - 1), height: height - 6, class: `bar-zone bar-${part.level}` }));
    from = part.upTo;
  }
  if (value !== null) {
    const x = Math.min(width - 2, Math.max(2, (value / max) * width));
    svg.append(svgElement("rect", { x: x - 2, y: 0, width: 4, height: height, class: `bar-mark mark-${level}` }));
  }
  svg.append(svgText(0, height + 11, "0", "bar-label", "start"));
  svg.append(svgText(width, height + 11, String(max), "bar-label", "end"));

  wrapper.setAttribute("aria-label", `${options.name}: ${showNumber(value)} ${value === null ? "" : options.unit}, ${zone.textContent}`);
  wrapper.setAttribute("role", "img");
  wrapper.replaceChildren(title, number, zone, svg);
}
