// Small SVG charts for the dashboard. No library.
// Every text goes in with textContent, so data from the network cannot add HTML.
// Rules followed: ink lines on paper, no colour for normal data, a line never
// crosses a silence longer than staleAfter, no area fills, and every chart has a
// text summary and a tooltip that works with mouse, touch and keyboard.

const SVG_NS = "http://www.w3.org/2000/svg";

const PAD_LEFT = 44;
const PAD_RIGHT = 12;
const PAD_TOP = 8;
const PAD_BOTTOM = 20;
// Activity row names are long, so that chart (and the pressure chart under it) starts further right.
const PAD_LEFT_WIDE = 92;
const PAD_LEFT_TIMELINE = 64;

// A time label closer than this to the right edge would hit the "now" label.
const NOW_LABEL_SPACE = 48;
// Two time labels need this much room between their centres.
const TIME_LABEL_SPACE = 44;
// A region must be this wide before its "no messages" label fits.
const REGION_LABEL_MIN = 90;
const TIMELINE_LABEL_MIN = 120;

// True while any tooltip is on screen. drawTrends (app.js) waits while the
// mouse rests on a chart with its tooltip open, so the tooltip is not wiped.
let chartTipOpen = false;
// True while a redraw (drawTrends in app.js) gives focus back to a chart.
let restoringFocus = false;
// Time of the point a keyboard user is reading, or null. After a redraw the
// tooltip opens again at this time, so keyboard focus never freezes the charts.
let keyboardTipTime = null;

// Each chart is drawn at its real pixel width, so text is the same size in all charts.
function chartWidth(wrapper) {
  return wrapper.clientWidth || 600;
}

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

// 24-hour clock everywhere: shorter labels, same as the server logs.
function shortTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: false });
}

function longTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour12: false });
}

// Tick every 1, 5 or 15 minutes, so there are about 4-5 labels on the axis.
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

function plural(count, word) {
  if (count === 1) {
    return `${count} ${word}`;
  }
  return `${count} ${word}s`;
}

// Vertical grid line at each tick. The right edge is labelled "now".
function drawTimeAxis(svg, start, end, height, xOf, left, right) {
  const bottom = height - PAD_BOTTOM;
  let lastLabelX = -Infinity;
  for (const tick of timeTicks(start, end)) {
    const x = xOf(tick);
    svg.append(svgElement("line", { x1: x, x2: x, y1: PAD_TOP, y2: bottom, class: "grid" }));
    // Skip labels that would hit "now" on the right, the y labels on the left,
    // or the label before (narrow charts at 5 min).
    if (x > right - NOW_LABEL_SPACE || x < left + 16 || x - lastLabelX < TIME_LABEL_SPACE) {
      continue;
    }
    svg.append(svgText(x, height - 5, shortTime(tick), "axis-label", "middle"));
    lastLabelX = x;
  }
  svg.append(svgText(right, height - 5, "now", "axis-label", "end"));
}

// Split points into runs the line may join. A new run starts at a null value
// or after a silence longer than staleAfter, so the line never invents data.
// points: [{t, value}] in time order.
// Returns runs (lists of points), gaps ({from, to}) and nulls (points with no reading).
function splitRuns(points, staleAfter, end) {
  const runs = [];
  const gaps = [];
  const nulls = [];
  let run = [];
  let previous = null;

  for (const point of points) {
    if (previous !== null && point.t - previous.t > staleAfter) {
      gaps.push({ from: previous.t, to: point.t });
      if (run.length > 0) {
        runs.push(run);
      }
      run = [];
    }
    if (point.value === null) {
      nulls.push(point);
      if (run.length > 0) {
        runs.push(run);
      }
      run = [];
    } else {
      run.push(point);
    }
    previous = point;
  }
  if (run.length > 0) {
    runs.push(run);
  }

  // Silence from the last message until now.
  if (previous !== null && end - previous.t > staleAfter) {
    gaps.push({ from: previous.t, to: end });
  }
  return { runs: runs, gaps: gaps, nulls: nulls };
}

// Grey hatched area over each silence, labelled when there is room.
function drawNoDataRegions(svg, gaps, xOf, top, bottom) {
  for (const gap of gaps) {
    const x1 = xOf(gap.from);
    const x2 = xOf(gap.to);
    svg.append(svgElement("rect", { x: x1, y: top, width: Math.max(1, x2 - x1), height: bottom - top, class: "no-data", fill: "url(#hatch-grey)" }));
    if (x2 - x1 > REGION_LABEL_MIN) {
      svg.append(svgText((x1 + x2) / 2, (top + bottom) / 2 + 4, "no messages", "no-data-label", "middle"));
    }
  }
}

function gapItems(gaps, xOf) {
  return gaps.map((gap) => ({
    x1: xOf(gap.from),
    x2: xOf(gap.to),
    y: null,
    t: gap.from,
    title: "No messages",
    gap: true,
    time: `${longTime(gap.from)} – ${longTime(gap.to)}`,
  }));
}

function drawEmptyText(svg, width, top, bottom, rangeText) {
  svg.append(svgText(width / 2, (top + bottom) / 2 + 5, `No data in the last ${rangeText}`, "empty-text", "middle"));
}

// Crosshair, dot and an HTML tooltip box for one chart.
// items: [{x1, x2, y, t, title, time}] sorted by x. A point has x1 === x2;
// a range (silence, activity step) has x1 < x2. y null means no dot.
// t is the item's start time, used to find it again after a redraw.
function addTooltip(wrapper, svg, items, width, top, bottom) {
  const crosshair = svgElement("line", { y1: top, y2: bottom, class: "crosshair", visibility: "hidden" });
  const hoverDot = svgElement("circle", { r: 4, class: "hover-dot", visibility: "hidden" });
  svg.append(crosshair, hoverDot);

  const tip = document.createElement("div");
  tip.className = "chart-tip";
  tip.hidden = true;
  wrapper.append(tip);

  let index = items.length - 1;

  function show(item, x) {
    crosshair.setAttribute("x1", x);
    crosshair.setAttribute("x2", x);
    crosshair.setAttribute("visibility", "visible");

    if (item.y === null) {
      hoverDot.setAttribute("visibility", "hidden");
    } else {
      hoverDot.setAttribute("cx", x);
      hoverDot.setAttribute("cy", item.y);
      hoverDot.setAttribute("visibility", "visible");
    }

    const value = document.createElement("div");
    value.className = "tip-value";
    value.textContent = item.title;
    const time = document.createElement("div");
    time.className = "tip-time";
    time.textContent = item.time;
    tip.replaceChildren(value, time);
    tip.hidden = false;

    // 12px right of the crosshair; flip to the left side if it would stick out.
    const scale = wrapper.clientWidth / width;
    const screenX = x * scale;
    let left = screenX + 12;
    if (left + tip.offsetWidth > wrapper.clientWidth) {
      left = screenX - tip.offsetWidth - 12;
    }
    tip.style.left = `${Math.max(0, left)}px`;
    chartTipOpen = true;
  }

  function hide() {
    crosshair.setAttribute("visibility", "hidden");
    hoverDot.setAttribute("visibility", "hidden");
    tip.hidden = true;
    chartTipOpen = false;
    keyboardTipTime = null;
  }

  // Keyboard: a point at its own x, a range at its middle.
  function showIndex() {
    const item = items[index];
    show(item, (item.x1 + item.x2) / 2);
    keyboardTipTime = item.t;
  }

  // Pick the item under the pointer. A point within 6px wins over a range,
  // so the samples at the edges of a silence can still be read.
  // Example: pointer x 100, point at x 104 scores 4, a silence 80-200 scores
  // 0 + 6 = 6, so the point wins. Pointer at 150: point 46, silence 6, silence wins.
  function onPointer(event) {
    if (items.length === 0) {
      return;
    }
    const box = svg.getBoundingClientRect();
    const x = ((event.clientX - box.left) * width) / box.width;
    let best = null;
    let bestScore = Infinity;
    items.forEach((item, i) => {
      let score = 0;
      if (x < item.x1) {
        score = item.x1 - x;
      } else if (x > item.x2) {
        score = x - item.x2;
      }
      if (item.x1 !== item.x2) {
        score += 6;
      }
      if (score < bestScore) {
        bestScore = score;
        best = i;
      }
    });
    index = best;
    const item = items[index];
    show(item, Math.min(item.x2, Math.max(item.x1, x)));
    keyboardTipTime = null;
  }

  svg.addEventListener("pointermove", onPointer);
  svg.addEventListener("pointerdown", onPointer);
  svg.addEventListener("pointerleave", hide);
  svg.addEventListener("pointercancel", hide);
  svg.addEventListener("pointerup", (event) => {
    if (event.pointerType === "touch") {
      hide();
    }
  });

  svg.addEventListener("focus", () => {
    if (items.length === 0) {
      return;
    }
    // After a redraw: open the tooltip again on the item the user was reading.
    if (restoringFocus) {
      if (keyboardTipTime === null) {
        return;
      }
      index = 0;
      items.forEach((item, i) => {
        if (Math.abs(item.t - keyboardTipTime) < Math.abs(items[index].t - keyboardTipTime)) {
          index = i;
        }
      });
      showIndex();
      return;
    }
    // Start at the newest point, not at a silence that may end at "now".
    index = items.length - 1;
    while (index > 0 && items[index].gap) {
      index -= 1;
    }
    showIndex();
  });
  svg.addEventListener("blur", hide);
  svg.addEventListener("keydown", (event) => {
    if (items.length === 0) {
      return;
    }
    if (event.key === "ArrowLeft") {
      index = Math.max(0, index - 1);
    } else if (event.key === "ArrowRight") {
      index = Math.min(items.length - 1, index + 1);
    } else if (event.key === "Home") {
      index = 0;
    } else if (event.key === "End") {
      index = items.length - 1;
    } else if (event.key === "Escape") {
      hide();
      return;
    } else {
      return;
    }
    event.preventDefault();
    showIndex();
  });
}

// Line chart of one value over time.
// points: [{t, value}]
// options: {start, end, staleAfter, height, padLeft, name, unit, yMax,
//   yTicks: [{value, label}], zoneTop, zoneLabel, refValue, live, rangeText, step}
// step: true holds each value flat until the next message (book on / off is a jump, not a slope).
function drawLineChart(wrapper, points, options) {
  const width = chartWidth(wrapper);
  const height = options.height;
  const left = options.padLeft;
  const right = width - PAD_RIGHT;
  const bottom = height - PAD_BOTTOM;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img", tabindex: "0" });

  const xOf = (t) => left + ((t - options.start) / (options.end - options.start)) * (right - left);
  // Values above the top are drawn at the top; the tooltip shows the true value.
  const yOf = (value) => bottom - (Math.min(value, options.yMax) / options.yMax) * (bottom - PAD_TOP);

  if (options.zoneTop !== undefined) {
    const zoneY = yOf(options.zoneTop);
    svg.append(svgElement("rect", { x: left, y: zoneY, width: right - left, height: bottom - zoneY, class: "zone" }));
  }

  for (const tick of options.yTicks) {
    const y = yOf(tick.value);
    svg.append(svgElement("line", { x1: left, x2: right, y1: y, y2: y, class: "grid" }));
    svg.append(svgText(left - 6, y + 4, tick.label, "axis-label", "end"));
  }
  drawTimeAxis(svg, options.start, options.end, height, xOf, left, right);

  if (options.zoneTop !== undefined) {
    svg.append(svgText(left + 4, bottom - 4, options.zoneLabel, "axis-label"));
  }

  if (options.refValue !== undefined) {
    const y = yOf(options.refValue);
    svg.append(svgElement("line", { x1: left, x2: right, y1: y, y2: y, class: "ref-line" }));
  }

  const split = splitRuns(points, options.staleAfter, options.end);
  drawNoDataRegions(svg, split.gaps, xOf, PAD_TOP, bottom);

  for (const run of split.runs) {
    if (run.length === 1) {
      svg.append(svgElement("circle", { cx: xOf(run[0].t), cy: yOf(run[0].value), r: 2, class: "dot" }));
      continue;
    }
    const corners = [];
    run.forEach((point, i) => {
      const x = xOf(point.t).toFixed(1);
      // Step: first go across at the old value, then up or down to the new one.
      if (options.step && i > 0) {
        corners.push(`${x},${yOf(run[i - 1].value).toFixed(1)}`);
      }
      corners.push(`${x},${yOf(point.value).toFixed(1)}`);
    });
    svg.append(svgElement("polyline", { points: corners.join(" "), class: "line" }));
  }

  // Sensor failed but the device is alive: a short tick on the x axis.
  for (const point of split.nulls) {
    const x = xOf(point.t);
    svg.append(svgElement("line", { x1: x, x2: x, y1: bottom - 6, y2: bottom, class: "rug" }));
  }

  const lastPoint = points.length > 0 ? points[points.length - 1] : null;
  if (options.live && lastPoint !== null && lastPoint.value !== null) {
    svg.append(svgElement("circle", { cx: xOf(lastPoint.t), cy: yOf(lastPoint.value), r: 3.5, class: "end-dot" }));
  }

  if (points.length === 0) {
    drawEmptyText(svg, width, PAD_TOP, bottom, options.rangeText);
  }

  // Summary for screen readers, without the parts that are zero.
  const values = points.filter((point) => point.value !== null).map((point) => point.value);
  let summary = `${options.name}, last ${options.rangeText}: no data.`;
  if (points.length > 0) {
    let latest = "no reading";
    if (lastPoint.value !== null) {
      latest = `${lastPoint.value} ${options.unit}`;
    }
    summary = `${options.name}, last ${options.rangeText}: latest ${latest}`;
    if (values.length > 0) {
      summary += `, lowest ${Math.min(...values)}, highest ${Math.max(...values)}`;
    }
    summary += ".";
    if (split.gaps.length > 0) {
      summary += ` ${plural(split.gaps.length, "gap")} with no messages.`;
    }
    if (split.nulls.length > 0) {
      summary += ` ${plural(split.nulls.length, "failed reading")}.`;
    }
  }
  svg.setAttribute("aria-label", summary);

  const items = points.map((point) => ({
    x1: xOf(point.t),
    x2: xOf(point.t),
    y: point.value === null ? null : yOf(point.value),
    t: point.t,
    title: point.value === null ? "No reading" : `${point.value} ${options.unit}`,
    time: longTime(point.t),
  }));
  items.push(...gapItems(split.gaps, xOf));
  items.sort((a, b) => a.x1 - b.x1);

  wrapper.replaceChildren(svg);
  addTooltip(wrapper, svg, items, width, PAD_TOP, bottom);
}

// Step chart: one row per activity, so the activity is read from position, not shade.
// points: [{t, value}] where value is an activity code or null.
// options: {start, end, staleAfter, name, rows, rowNames, names, live, rangeText}
function drawActivityChart(wrapper, points, options) {
  const rowHeight = 14;
  const width = chartWidth(wrapper);
  const height = PAD_TOP + options.rows.length * rowHeight + PAD_BOTTOM;
  const left = PAD_LEFT_WIDE;
  const right = width - PAD_RIGHT;
  const bottom = height - PAD_BOTTOM;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img", tabindex: "0" });

  const xOf = (t) => left + ((t - options.start) / (options.end - options.start)) * (right - left);
  const yOf = (activity) => PAD_TOP + options.rows.indexOf(activity) * rowHeight + rowHeight / 2;

  const lastPoint = points.length > 0 ? points[points.length - 1] : null;
  const current = lastPoint === null ? null : lastPoint.value;

  options.rows.forEach((activity) => {
    const y = yOf(activity);
    svg.append(svgElement("line", { x1: left, x2: right, y1: y, y2: y, class: "grid" }));
    let className = "row-label";
    if (options.live && activity === current) {
      className += " now";
    }
    svg.append(svgText(left - 6, y + 4, options.rowNames[activity], className, "end"));
  });
  drawTimeAxis(svg, options.start, options.end, height, xOf, left, right);

  const split = splitRuns(points, options.staleAfter, options.end);
  drawNoDataRegions(svg, split.gaps, xOf, PAD_TOP, bottom);

  // Each step lasts until the next message. Equal steps in a row merge into one.
  // Example: idle@10, idle@20, recording@30, recording@40
  //   -> steps idle 10-30, recording 30-40 (2 steps, so "1 change").
  const steps = [];
  split.runs.forEach((run) => {
    const runSteps = [];
    run.forEach((point, i) => {
      let to = point.t;
      if (i + 1 < run.length) {
        to = run[i + 1].t;
      }
      const previous = runSteps[runSteps.length - 1];
      if (previous !== undefined && previous.value === point.value) {
        previous.to = to;
      } else {
        runSteps.push({ value: point.value, from: point.t, to: to });
      }
    });
    steps.push(runSteps);
  });

  // The device is still reporting, so its last activity holds until now.
  const stillReporting = lastPoint !== null && lastPoint.value !== null && options.end - lastPoint.t <= options.staleAfter;
  if (stillReporting) {
    const lastRun = steps[steps.length - 1];
    lastRun[lastRun.length - 1].to = options.end;
  }

  for (const runSteps of steps) {
    const corners = [];
    for (const step of runSteps) {
      corners.push(`${xOf(step.from).toFixed(1)},${yOf(step.value)}`);
      corners.push(`${xOf(step.to).toFixed(1)},${yOf(step.value)}`);
    }
    const only = runSteps[0];
    if (runSteps.length === 1 && only.from === only.to) {
      svg.append(svgElement("circle", { cx: xOf(only.from), cy: yOf(only.value), r: 2, class: "dot" }));
      continue;
    }
    svg.append(svgElement("polyline", { points: corners.join(" "), class: "line" }));
  }

  if (options.live && stillReporting) {
    svg.append(svgElement("circle", { cx: xOf(options.end), cy: yOf(current), r: 3.5, class: "end-dot" }));
  }

  if (points.length === 0) {
    drawEmptyText(svg, width, PAD_TOP, bottom, options.rangeText);
  }

  const allSteps = steps.flat();
  let summary = `${options.name}, last ${options.rangeText}: no data.`;
  if (points.length > 0) {
    const name = options.names[current] || "unknown";
    if (options.live) {
      summary = `${options.name}, last ${options.rangeText}: now ${name}.`;
    } else {
      summary = `${options.name}, last ${options.rangeText}: last known ${name}.`;
    }
    summary += ` ${plural(Math.max(0, allSteps.length - 1), "change")}.`;
    if (split.gaps.length > 0) {
      summary += ` ${plural(split.gaps.length, "gap")} with no messages.`;
    }
  }
  svg.setAttribute("aria-label", summary);

  const items = allSteps.map((step) => {
    let time = `${longTime(step.from)} – ${longTime(step.to)}`;
    if (step.from === step.to) {
      time = longTime(step.from);
    }
    return { x1: xOf(step.from), x2: xOf(step.to), y: yOf(step.value), t: step.from, title: options.names[step.value], time: time };
  });
  items.push(...gapItems(split.gaps, xOf));
  items.sort((a, b) => a.x1 - b.x1);

  wrapper.replaceChildren(svg);
  addTooltip(wrapper, svg, items, width, PAD_TOP, bottom);
}

// One row per device, one tick per message. A silence longer than staleAfter
// is hatched red: that is where Wi-Fi or power was lost.
// rows: [{label, times}]  options: {start, end, staleAfter, rangeText}
function drawTimeline(wrapper, rows, options) {
  const rowHeight = 24;
  const width = chartWidth(wrapper);
  const height = PAD_TOP + rows.length * rowHeight + PAD_BOTTOM;
  const left = PAD_LEFT_TIMELINE;
  const right = width - PAD_RIGHT;
  const svg = svgElement("svg", { viewBox: `0 0 ${width} ${height}`, role: "img", tabindex: "0" });
  const xOf = (t) => left + ((t - options.start) / (options.end - options.start)) * (right - left);

  drawTimeAxis(svg, options.start, options.end, height, xOf, left, right);

  const rowTexts = [];
  rows.forEach((row, index) => {
    const y = PAD_TOP + index * rowHeight;
    const middle = y + rowHeight / 2;
    svg.append(svgText(0, middle + 5, row.label, "timeline-label"));
    svg.append(svgElement("line", { x1: left, x2: right, y1: middle, y2: middle, class: "grid" }));

    // Silences between messages, and from the last message until now.
    const edges = [...row.times];
    if (edges.length > 0) {
      edges.push(options.end);
    }
    let gapCount = 0;
    let silentUntilNow = 0;
    for (let i = 1; i < edges.length; i++) {
      const gap = edges[i] - edges[i - 1];
      if (gap <= options.staleAfter) {
        continue;
      }
      gapCount += 1;
      if (i === edges.length - 1) {
        silentUntilNow = gap;
      }
      const x1 = xOf(edges[i - 1]);
      const x2 = xOf(edges[i]);
      const band = svgElement("rect", { x: x1, y: y + 4, width: Math.max(2, x2 - x1), height: rowHeight - 8, class: "gap", fill: "url(#hatch-red)" });
      const title = svgElement("title", {});
      title.textContent = `${row.label}: no messages for ${Math.round(gap)} s (${longTime(edges[i - 1])} to ${longTime(edges[i])})`;
      band.append(title);
      svg.append(band);
      svg.append(svgElement("line", { x1: x1, x2: x1 + Math.max(2, x2 - x1), y1: y + 4.5, y2: y + 4.5, class: "gap-edge" }));
      svg.append(svgElement("line", { x1: x1, x2: x1 + Math.max(2, x2 - x1), y1: y + rowHeight - 4.5, y2: y + rowHeight - 4.5, class: "gap-edge" }));
      if (x2 - x1 > TIMELINE_LABEL_MIN) {
        // formatAge lives in app.js (loaded after this file, ready by draw time).
        svg.append(svgText((x1 + x2) / 2, middle + 4, `no messages ${formatAge(gap)}`, "gap-label", "middle"));
      }
    }

    for (const t of row.times) {
      const x = xOf(t);
      svg.append(svgElement("line", { x1: x, x2: x, y1: middle - 6, y2: middle + 6, class: "tick" }));
    }

    if (row.times.length === 0) {
      rowTexts.push(`${row.label} no messages.`);
    } else if (silentUntilNow > 0) {
      rowTexts.push(`${row.label} no messages for the last ${formatAge(silentUntilNow)}.`);
    } else if (gapCount > 0) {
      rowTexts.push(`${row.label} ${plural(gapCount, "gap")}.`);
    } else {
      rowTexts.push(`${row.label} no gaps.`);
    }
  });

  svg.setAttribute("aria-label", `Messages received, last ${options.rangeText}. ${rowTexts.join(" ")}`);
  wrapper.replaceChildren(svg);
}
