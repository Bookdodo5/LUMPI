/* LUMPI Teacher Dashboard - SVG Chart Drawing */
/* No fetch, no knowledge of nodes - pure drawing functions */

// Chart constants
const PADDING = { left: 44, right: 12, top: 12, bottom: 24 };
const LINE_COLOR = "var(--ink)";
const LINE_WIDTH = 2;
const AREA_OPACITY = 0.08;
const GRID_COLOR = "#ebe8e3";
const THRESHOLD_COLOR = "var(--red)";
const THRESHOLD_WIDTH = 1.5;
const DOT_RADIUS = 4;
const DOT_STROKE = 2;
const AXIS_FONT = "11px 'Noto Sans JP', system-ui, sans-serif";
const AXIS_COLOR = "var(--grey)";
const TOOLTIP_BG = "var(--ink)";
const TOOLTIP_COLOR = "var(--white)";

// Activity strip colors
const ACTIVITY_COLORS = {
  idle: "#f4f2ee",
  default: "#ebe8e3",
  recording: "#d9d6d1",
  waiting_ai: "#d9d6d1",
  feedback: "#d9d6d1",
  stopped: "var(--red)",
};
const ACTIVITY_LABEL_COLOR = "var(--ink)";
const BAND_HEIGHT = 26;
const BAND_RADIUS = 3;
const BAND_SPACING = 2;

// Timeline constants
const TIMELINE_ROW_HEIGHT = 30;
const TIMELINE_LABEL_WIDTH = 70;
const TIMELINE_TICK_WIDTH = 2;

// Nice number scale for Y axis
function niceMax(maxVal, threshold = null) {
  if (maxVal <= 0) return 1;
  const maxWithMargin = Math.max(maxVal * 1.1, (threshold || 0) * 1.1);
  const exp = Math.floor(Math.log10(maxWithMargin));
  const frac = maxWithMargin / Math.pow(10, exp);
  let niceFrac;
  if (frac <= 1) niceFrac = 1;
  else if (frac <= 2) niceFrac = 2;
  else if (frac <= 2.5) niceFrac = 2.5;
  else if (frac <= 5) niceFrac = 5;
  else niceFrac = 10;
  return niceFrac * Math.pow(10, exp);
}

function formatTime(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

function formatTimeFull(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function getXPos(t, now, minutes, width) {
  const span = minutes * 60;
  const rel = (now - t) / span;
  return PADDING.left + (1 - rel) * (width - PADDING.left - PADDING.right);
}

function getYPos(val, minVal, maxVal, height) {
  if (val === null || val === undefined) return null;
  const ratio = (val - minVal) / (maxVal - minVal);
  return PADDING.top + (1 - ratio) * (height - PADDING.top - PADDING.bottom);
}

// Draw grid lines and labels
function drawGrid(svg, width, height, minVal, maxVal, minutes, now, threshold) {
  const ns = "http://www.w3.org/2000/svg";
  const gridGroup = document.createElementNS(ns, "g");
  gridGroup.setAttribute("class", "grid");

  // Y grid lines (0, 50%, 100% of nice max)
  const steps = [0, 0.5, 1];
  const yMax = niceMax(maxVal, threshold);
  const yMin = minVal < 0 ? -yMax : 0; // for distance, could be negative? no, distance is positive

  steps.forEach(step => {
    const val = yMin + step * (yMax - yMin);
    const y = getYPos(val, yMin, yMax, height);
    if (y !== null && y >= PADDING.top && y <= height - PADDING.bottom) {
      const line = document.createElementNS(ns, "line");
      line.setAttribute("x1", PADDING.left);
      line.setAttribute("x2", width - PADDING.right);
      line.setAttribute("y1", y);
      line.setAttribute("y2", y);
      line.setAttribute("stroke", GRID_COLOR);
      line.setAttribute("stroke-width", "1");
      gridGroup.appendChild(line);

      // Y label
      const text = document.createElementNS(ns, "text");
      text.setAttribute("x", PADDING.left - 8);
      text.setAttribute("y", y + 4);
      text.setAttribute("text-anchor", "end");
      text.setAttribute("font", AXIS_FONT);
      text.setAttribute("fill", AXIS_COLOR);
      text.setAttribute("font-variant-numeric", "tabular-nums");
      // Format: show 1 decimal for lux, integer for distance
      text.textContent = (val % 1 === 0) ? val.toFixed(0) : val.toFixed(1);
      gridGroup.appendChild(text);
    }
  });

  // Threshold line (for lux chart)
  if (threshold !== null && threshold !== undefined) {
    const y = getYPos(threshold, yMin, yMax, height);
    if (y !== null && y >= PADDING.top && y <= height - PADDING.bottom) {
      const line = document.createElementNS(ns, "line");
      line.setAttribute("x1", PADDING.left);
      line.setAttribute("x2", width - PADDING.right);
      line.setAttribute("y1", y);
      line.setAttribute("y2", y);
      line.setAttribute("stroke", THRESHOLD_COLOR);
      line.setAttribute("stroke-width", THRESHOLD_WIDTH);
      line.setAttribute("stroke-dasharray", "4 4");
      gridGroup.appendChild(line);

      const label = document.createElementNS(ns, "text");
      label.setAttribute("x", width - PADDING.right);
      label.setAttribute("y", y - 6);
      label.setAttribute("text-anchor", "end");
      label.setAttribute("font", "10px 'Noto Sans JP', system-ui, sans-serif");
      label.setAttribute("fill", THRESHOLD_COLOR);
      label.textContent = "dark below 100 lux";
      gridGroup.appendChild(label);
    }
  }

  // X grid lines and time labels (~5 labels)
  const span = minutes * 60;
  const numTicks = Math.min(5, minutes <= 5 ? 5 : minutes <= 15 ? 5 : 6);
  const tickInterval = span / (numTicks - 1);

  for (let i = 0; i < numTicks; i++) {
    const t = now - span + i * tickInterval;
    if (t > now) continue;
    const x = getXPos(t, now, minutes, width);

    // Vertical grid line
    const line = document.createElementNS(ns, "line");
    line.setAttribute("x1", x);
    line.setAttribute("x2", x);
    line.setAttribute("y1", PADDING.top);
    line.setAttribute("y2", height - PADDING.bottom);
    line.setAttribute("stroke", GRID_COLOR);
    line.setAttribute("stroke-width", "1");
    gridGroup.appendChild(line);

    // Time label
    const text = document.createElementNS(ns, "text");
    text.setAttribute("x", x);
    text.setAttribute("y", height - PADDING.bottom + 16);
    text.setAttribute("text-anchor", i === 0 ? "start" : (i === numTicks - 1 ? "end" : "middle"));
    text.setAttribute("font", AXIS_FONT);
    text.setAttribute("fill", AXIS_COLOR);
    text.setAttribute("font-variant-numeric", "tabular-nums");
    text.textContent = formatTime(t);
    gridGroup.appendChild(text);
  }

  svg.appendChild(gridGroup);
}

// Draw line chart with area
function drawLineChart(svg, points, width, height, minVal, maxVal, minutes, now, threshold, seriesName) {
  const ns = "http://www.w3.org/2000/svg";
  const yMax = niceMax(maxVal, threshold);
  const yMin = minVal < 0 ? -yMax : 0;

  // Group for this series
  const group = document.createElementNS(ns, "g");
  group.setAttribute("class", "series");

  // Split points into runs (null breaks the line)
  const runs = [];
  let currentRun = [];
  points.forEach(p => {
    if (p.val !== null && p.val !== undefined) {
      currentRun.push(p);
    } else {
      if (currentRun.length > 0) {
        runs.push(currentRun);
        currentRun = [];
      }
    }
  });
  if (currentRun.length > 0) runs.push(currentRun);

  // Draw area and line for each run
  runs.forEach(run => {
    if (run.length < 2) return;

    // Area path
    const areaPath = document.createElementNS(ns, "path");
    let d = `M ${run[0].x} ${height - PADDING.bottom}`;
    run.forEach(p => {
      d += ` L ${p.x} ${p.y}`;
    });
    d += ` L ${run[run.length - 1].x} ${height - PADDING.bottom} Z`;
    areaPath.setAttribute("d", d);
    areaPath.setAttribute("fill", LINE_COLOR);
    areaPath.setAttribute("fill-opacity", AREA_OPACITY);
    group.appendChild(areaPath);

    // Line path
    const linePath = document.createElementNS(ns, "path");
    let ld = `M ${run[0].x} ${run[0].y}`;
    for (let i = 1; i < run.length; i++) {
      ld += ` L ${run[i].x} ${run[i].y}`;
    }
    linePath.setAttribute("d", ld);
    linePath.setAttribute("stroke", LINE_COLOR);
    linePath.setAttribute("stroke-width", LINE_WIDTH);
    linePath.setAttribute("fill", "none");
    linePath.setAttribute("stroke-linejoin", "round");
    linePath.setAttribute("stroke-linecap", "round");
    group.appendChild(linePath);

    // Dots at each point
    run.forEach(p => {
      const circle = document.createElementNS(ns, "circle");
      circle.setAttribute("cx", p.x);
      circle.setAttribute("cy", p.y);
      circle.setAttribute("r", DOT_RADIUS);
      circle.setAttribute("fill", LINE_COLOR);
      circle.setAttribute("stroke", "var(--white)");
      circle.setAttribute("stroke-width", DOT_STROKE);
      circle.setAttribute("class", "chart-dot");
      circle.setAttribute("data-time", p.t);
      circle.setAttribute("data-value", p.val);
      group.appendChild(circle);
    });
  });

  svg.appendChild(group);
}

// Prepare points for line chart
function prepareLinePoints(rawPoints, width, height, minutes, now, threshold) {
  const span = minutes * 60;
  const yMax = niceMax(
    Math.max(...rawPoints.filter(p => p.val !== null).map(p => p.val), 0),
    threshold
  );
  const yMin = 0;

  return rawPoints.map(p => ({
    t: p.t,
    val: p.val,
    x: getXPos(p.t, now, minutes, width),
    y: getYPos(p.val, yMin, yMax, height)
  }));
}

// Main line chart drawing function
function drawLineChartFull(wrapperId, historyData, minutes, now, options = {}) {
  const wrapper = document.getElementById(wrapperId);
  if (!wrapper) return;

  const width = wrapper.clientWidth || 600;
  const height = 200; // fixed height for line charts

  // Clear previous SVG
  wrapper.innerHTML = "";

  const ns = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "auto");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", options.ariaLabel || "Chart");

  // Filter points within time range
  const cutoff = now - minutes * 60;
  const points = historyData
    .filter(p => p.t >= cutoff)
    .map(p => ({ t: p.t, val: p.data[options.valueKey] }))
    .sort((a, b) => a.t - b.t);

  if (points.length === 0) {
    // Draw empty state
    const text = document.createElementNS(ns, "text");
    text.setAttribute("x", width / 2);
    text.setAttribute("y", height / 2);
    text.setAttribute("text-anchor", "middle");
    text.setAttribute("font", "14px 'Noto Sans JP', system-ui, sans-serif");
    text.setAttribute("fill", "var(--grey)");
    text.textContent = "No data yet";
    svg.appendChild(text);
    wrapper.appendChild(svg);
    return;
  }

  // Calculate min/max
  const vals = points.filter(p => p.val !== null).map(p => p.val);
  const maxVal = vals.length > 0 ? Math.max(...vals) : 0;
  const minVal = vals.length > 0 ? Math.min(...vals) : 0;

  // Prepare points with screen coordinates
  const prepared = prepareLinePoints(points, width, height, minutes, now, options.threshold);

  // Draw grid
  drawGrid(svg, width, height, minVal, maxVal, minutes, now, options.threshold);

  // Draw line chart
  drawLineChart(svg, prepared, width, height, minVal, maxVal, minutes, now, options.threshold, options.seriesName);

  // Update aria-label with summary
  if (vals.length > 0) {
    const latest = vals[vals.length - 1];
    const lowest = Math.min(...vals);
    const highest = Math.max(...vals);
    const unit = options.unit || "";
    svg.setAttribute("aria-label",
      `${options.seriesName || "Value"}: latest ${latest}${unit}, lowest ${lowest}${unit}, highest ${highest}${unit}`);
  }

  wrapper.appendChild(svg);

  // Attach hover handler
  attachChartHover(wrapper, svg, prepared, minutes, now, options.valueKey, options.unit);
}

// Hover tooltip for line charts
let hoverTimeout = null;
function attachChartHover(wrapper, svg, points, minutes, now, valueKey, unit) {
  const tip = document.getElementById("chart-tip");
  if (!tip) return;

  const rect = wrapper.getBoundingClientRect();

  function onPointerMove(e) {
    const x = e.clientX - rect.left;
    // Find nearest point by x
    let nearest = null;
    let minDist = Infinity;
    points.forEach(p => {
      if (p.val === null) return;
      const dist = Math.abs(p.x - x);
      if (dist < minDist) {
        minDist = dist;
        nearest = p;
      }
    });

    if (nearest && minDist < 30) {
      // Show crosshair
      showCrosshair(svg, nearest.x, height);
      // Show dot highlight
      showDotHighlight(svg, nearest.x, nearest.y);
      // Show tooltip
      const timeStr = formatTimeFull(nearest.t);
      const valStr = nearest.val !== null ? `${nearest.val}${unit ? " " + unit : ""}` : "no reading";
      tip.textContent = `${timeStr}  ${valueKey}: ${valStr}`;
      tip.classList.add("visible");

      // Position tooltip
      const tipRect = tip.getBoundingClientRect();
      let tipX = e.clientX + 12;
      if (tipX + tipRect.width > window.innerWidth - 12) {
        tipX = e.clientX - tipRect.width - 12;
      }
      tip.style.left = tipX + "px";
      tip.style.top = (e.clientY - tipRect.height - 8) + "px";
    } else {
      hideHover(svg);
    }
  }

  function onPointerLeave() {
    hideHover(svg);
  }

  wrapper.addEventListener("pointermove", onPointerMove);
  wrapper.addEventListener("pointerleave", onPointerLeave);

  // Store handlers for cleanup
  wrapper._hoverHandlers = { onPointerMove, onPointerLeave };
}

function showCrosshair(svg, x, height) {
  const ns = "http://www.w3.org/2000/svg";
  // Remove old crosshair
  const old = svg.querySelector(".crosshair");
  if (old) old.remove();

  const line = document.createElementNS(ns, "line");
  line.setAttribute("class", "crosshair");
  line.setAttribute("x1", x);
  line.setAttribute("x2", x);
  line.setAttribute("y1", PADDING.top);
  line.setAttribute("y2", height - PADDING.bottom);
  line.setAttribute("stroke", "var(--grey)");
  line.setAttribute("stroke-width", "1");
  line.setAttribute("stroke-dasharray", "2 2");
  line.setAttribute("pointer-events", "none");
  svg.appendChild(line);
}

function showDotHighlight(svg, x, y) {
  const ns = "http://www.w3.org/2000/svg";
  const old = svg.querySelector(".dot-highlight");
  if (old) old.remove();

  const circle = document.createElementNS(ns, "circle");
  circle.setAttribute("class", "dot-highlight");
  circle.setAttribute("cx", x);
  circle.setAttribute("cy", y);
  circle.setAttribute("r", 6);
  circle.setAttribute("fill", "none");
  circle.setAttribute("stroke", "var(--ink)");
  circle.setAttribute("stroke-width", 2);
  circle.setAttribute("pointer-events", "none");
  svg.appendChild(circle);
}

function hideHover(svg) {
  const tip = document.getElementById("chart-tip");
  if (tip) tip.classList.remove("visible");
  const crosshair = svg.querySelector(".crosshair");
  if (crosshair) crosshair.remove();
  const highlight = svg.querySelector(".dot-highlight");
  if (highlight) highlight.remove();
}

// Timeline chart (messages received)
function drawTimeline(wrapperId, historyData, minutes, now, staleAfter, nodeNames) {
  const wrapper = document.getElementById(wrapperId);
  if (!wrapper) return;

  const width = wrapper.clientWidth || 600;
  const numNodes = 3; // Nest, Doll 1, Doll 2
  const height = PADDING.top + numNodes * TIMELINE_ROW_HEIGHT + PADDING.bottom + 40; // extra for counters

  wrapper.innerHTML = "";

  const ns = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "auto");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", "Messages received timeline");

  const cutoff = now - minutes * 60;
  const span = minutes * 60;

  // Group by node
  const nodes = [0, 1, 2];
  let totalGaps = 0;

  nodes.forEach((nid, row) => {
    const points = historyData[nid] || [];
    const nodePoints = points
      .filter(p => p.t >= cutoff)
      .sort((a, b) => a.t - b.t);

    const y = PADDING.top + row * TIMELINE_ROW_HEIGHT + TIMELINE_ROW_HEIGHT / 2;
    const labelY = y + 4;

    // Node label
    const label = document.createElementNS(ns, "text");
    label.setAttribute("x", TIMELINE_LABEL_WIDTH - 10);
    label.setAttribute("y", labelY);
    label.setAttribute("text-anchor", "end");
    label.setAttribute("font", "12px 'Noto Sans JP', system-ui, sans-serif");
    label.setAttribute("fill", "var(--ink)");
    label.textContent = nodeNames[nid] || `Node ${nid}`;
    svg.appendChild(label);

    if (nodePoints.length === 0) {
      // No messages - show gap from start to now
      const gapStart = cutoff;
      const gapEnd = now;
      const x1 = getXPos(gapStart, now, minutes, width);
      const x2 = getXPos(gapEnd, now, minutes, width);
      drawGap(svg, x1, x2, y - TIMELINE_ROW_HEIGHT / 2 + 4, TIMELINE_ROW_HEIGHT - 8, gapEnd - gapStart);
      totalGaps++;
      return;
    }

    // Draw ticks for each message
    nodePoints.forEach(p => {
      const x = getXPos(p.t, now, minutes, width);
      const line = document.createElementNS(ns, "line");
      line.setAttribute("x1", x);
      line.setAttribute("x2", x);
      line.setAttribute("y1", y - TIMELINE_ROW_HEIGHT / 2 + 4);
      line.setAttribute("y2", y + TIMELINE_ROW_HEIGHT / 2 - 4);
      line.setAttribute("stroke", "var(--ink)");
      line.setAttribute("stroke-width", TIMELINE_TICK_WIDTH);
      line.setAttribute("class", "timeline-tick");
      line.setAttribute("data-time", p.t);
      svg.appendChild(line);
    });

    // Draw gaps between consecutive messages AND from last message to now
    let lastT = cutoff;
    nodePoints.forEach(p => {
      if (p.t - lastT > staleAfter) {
        const x1 = getXPos(lastT, now, minutes, width);
        const x2 = getXPos(p.t, now, minutes, width);
        drawGap(svg, x1, x2, y - TIMELINE_ROW_HEIGHT / 2 + 4, TIMELINE_ROW_HEIGHT - 8, p.t - lastT);
        totalGaps++;
      }
      lastT = p.t;
    });
    // Gap from last message to now
    if (now - lastT > staleAfter) {
      const x1 = getXPos(lastT, now, minutes, width);
      const x2 = getXPos(now, now, minutes, width);
      drawGap(svg, x1, x2, y - TIMELINE_ROW_HEIGHT / 2 + 4, TIMELINE_ROW_HEIGHT - 8, now - lastT);
      totalGaps++;
    }
  });

  // Counters at bottom
  let counterY = PADDING.top + numNodes * TIMELINE_ROW_HEIGHT + 20;
  nodes.forEach((nid, i) => {
    const points = historyData[nid] || [];
    const nodePoints = points.filter(p => p.t >= cutoff);
    const added = nodePoints.length;
    const duplicate = 0; // we don't track duplicates in history, only in counts

    const text = document.createElementNS(ns, "text");
    text.setAttribute("x", TIMELINE_LABEL_WIDTH + i * 180 + 20);
    text.setAttribute("y", counterY);
    text.setAttribute("font", "12px 'Noto Sans JP', system-ui, sans-serif");
    text.setAttribute("fill", "var(--grey)");
    text.textContent = `${nodeNames[nid]}: ${added} received, ${duplicate} duplicate`;
    svg.appendChild(text);
  });

  // Caption
  const caption = document.createElementNS(ns, "text");
  caption.setAttribute("x", TIMELINE_LABEL_WIDTH);
  caption.setAttribute("y", counterY + 20);
  caption.setAttribute("font", "11px 'Noto Sans JP', system-ui, sans-serif");
  caption.setAttribute("fill", "var(--grey)");
  caption.textContent = "Messages received — red = no message for over 30 s";
  svg.appendChild(caption);

  // Update aria-label
  svg.setAttribute("aria-label", `Messages received. ${totalGaps} gaps.`);

  wrapper.appendChild(svg);
}

function drawGap(svg, x1, x2, y, h, gapSeconds) {
  const ns = "http://www.w3.org/2000/svg";
  const rect = document.createElementNS(ns, "rect");
  rect.setAttribute("x", x1);
  rect.setAttribute("y", y);
  rect.setAttribute("width", Math.max(1, x2 - x1));
  rect.setAttribute("height", h);
  rect.setAttribute("fill", "var(--red)");
  rect.setAttribute("fill-opacity", "0.3");
  svg.appendChild(rect);

  // Add title tooltip
  if (x2 - x1 > 60) {
    const title = document.createElementNS(ns, "title");
    const startTime = new Date((Date.now() - (gapSeconds * 1000))).toLocaleTimeString();
    const endTime = new Date().toLocaleTimeString();
    title.textContent = `Gap: ${Math.round(gapSeconds)} s (${startTime} to ${endTime})`;
    rect.appendChild(title);
  }
}

// Activity strip
function drawActivityStrip(wrapperId, historyData, minutes, now, nodeId, nodeName) {
  const wrapper = document.getElementById(wrapperId);
  if (!wrapper) return;

  const width = wrapper.clientWidth || 600;
  const height = 100; // activity strip + pressure line

  wrapper.innerHTML = "";

  const ns = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "auto");
  svg.setAttribute("role", "img");
  svg.setAttribute("aria-label", `${nodeName} activity`);

  const cutoff = now - minutes * 60;
  const points = historyData[nodeId] || [];
  const nodePoints = points
    .filter(p => p.t >= cutoff && p.activity !== null)
    .sort((a, b) => a.t - b.t);

  if (nodePoints.length === 0) {
    const text = document.createElementNS(ns, "text");
    text.setAttribute("x", width / 2);
    text.setAttribute("y", height / 2);
    text.setAttribute("text-anchor", "middle");
    text.setAttribute("font", "14px 'Noto Sans JP', system-ui, sans-serif");
    text.setAttribute("fill", "var(--grey)");
    text.textContent = "No activity yet";
    svg.appendChild(text);
    wrapper.appendChild(svg);
    return;
  }

  // Merge consecutive same-activity points into bands
  const bands = [];
  let currentBand = null;
  nodePoints.forEach(p => {
    if (!currentBand || currentBand.activity !== p.activity) {
      if (currentBand) {
        currentBand.end = p.t;
        bands.push(currentBand);
      }
      currentBand = {
        activity: p.activity,
        start: p.t,
        end: p.t
      };
    } else {
      currentBand.end = p.t;
    }
  });
  if (currentBand) {
    currentBand.end = now; // extend last band to now
    bands.push(currentBand);
  }

  // Draw activity bands (top portion)
  const bandTop = 8;
  const bandHeight = BAND_HEIGHT;

  bands.forEach(band => {
    const x1 = getXPos(band.start, now, minutes, width);
    const x2 = getXPos(band.end, now, minutes, width);
    const bandWidth = Math.max(2, x2 - x1);

    const color = ACTIVITY_COLORS[band.activity] || ACTIVITY_COLORS.default;
    const rect = document.createElementNS(ns, "rect");
    rect.setAttribute("x", x1);
    rect.setAttribute("y", bandTop);
    rect.setAttribute("width", bandWidth);
    rect.setAttribute("height", bandHeight);
    rect.setAttribute("rx", BAND_RADIUS);
    rect.setAttribute("fill", color);
    svg.appendChild(rect);

    // Label inside band if it fits
    const label = ACTIVITY_TEXT[band.activity] || band.activity;
    const charWidth = 7;
    const minWidth = label.length * charWidth + 8;
    if (bandWidth >= minWidth) {
      const text = document.createElementNS(ns, "text");
      text.setAttribute("x", x1 + bandWidth / 2);
      text.setAttribute("y", bandTop + bandHeight / 2 + 4);
      text.setAttribute("text-anchor", "middle");
      text.setAttribute("font", "11px 'Noto Sans JP', system-ui, sans-serif");
      text.setAttribute("fill", band.activity === "stopped" ? "var(--white)" : "var(--ink)");
      text.setAttribute("pointer-events", "none");
      text.textContent = label;
      svg.appendChild(text);
    }

    // Tooltip
    const title = document.createElementNS(ns, "title");
    const startStr = formatTimeFull(band.start);
    const endStr = band.end >= now ? "now" : formatTimeFull(band.end);
    title.textContent = `${label}: ${startStr} to ${endStr}`;
    rect.appendChild(title);
  });

  // Sub-caption: "Doll N pressure"
  const captionY = bandTop + bandHeight + 16;
  const caption = document.createElementNS(ns, "text");
  caption.setAttribute("x", PADDING.left);
  caption.setAttribute("y", captionY);
  caption.setAttribute("font", "12px 'Noto Sans JP', system-ui, sans-serif");
  caption.setAttribute("fill", "var(--grey)");
  caption.textContent = `${nodeName} pressure`;
  svg.appendChild(caption);

  // Pressure line chart below
  const pressurePoints = points
    .filter(p => p.t >= cutoff && p.data.fsr !== undefined)
    .map(p => ({ t: p.t, val: p.data.fsr }))
    .sort((a, b) => a.t - b.t);

  if (pressurePoints.length > 0) {
    const vals = pressurePoints.map(p => p.val).filter(v => v !== null);
    const maxVal = vals.length > 0 ? Math.max(...vals) : 100;
    const minVal = 0;
    const chartTop = captionY + 8;
    const chartHeight = height - chartTop - PADDING.bottom;

    // Grid for pressure (simple)
    const yMax = niceMax(maxVal);
    const steps = [0, 0.5, 1];
    steps.forEach(step => {
      const val = minVal + step * (yMax - minVal);
      const y = chartTop + PADDING.top + (1 - step) * (chartHeight - PADDING.top - PADDING.bottom);
      const line = document.createElementNS(ns, "line");
      line.setAttribute("x1", PADDING.left);
      line.setAttribute("x2", width - PADDING.right);
      line.setAttribute("y1", y);
      line.setAttribute("y2", y);
      line.setAttribute("stroke", GRID_COLOR);
      line.setAttribute("stroke-width", "1");
      svg.appendChild(line);
    });

    // Pressure line
    const prepared = pressurePoints.map(p => ({
      t: p.t,
      val: p.val,
      x: getXPos(p.t, now, minutes, width),
      y: chartTop + PADDING.top + (1 - (p.val || 0) / yMax) * (chartHeight - PADDING.top - PADDING.bottom)
    }));

    // Split into runs
    const runs = [];
    let currentRun = [];
    prepared.forEach(p => {
      if (p.val !== null && p.val !== undefined) {
        currentRun.push(p);
      } else {
        if (currentRun.length > 0) { runs.push(currentRun); currentRun = []; }
      }
    });
    if (currentRun.length > 0) runs.push(currentRun);

    runs.forEach(run => {
      if (run.length < 2) return;
      const path = document.createElementNS(ns, "path");
      let d = `M ${run[0].x} ${run[0].y}`;
      for (let i = 1; i < run.length; i++) d += ` L ${run[i].x} ${run[i].y}`;
      path.setAttribute("d", d);
      path.setAttribute("stroke", "var(--ink)");
      path.setAttribute("stroke-width", LINE_WIDTH);
      path.setAttribute("fill", "none");
      path.setAttribute("stroke-linejoin", "round");
      path.setAttribute("stroke-linecap", "round");
      svg.appendChild(path);

      run.forEach(p => {
        const circle = document.createElementNS(ns, "circle");
        circle.setAttribute("cx", p.x);
        circle.setAttribute("cy", p.y);
        circle.setAttribute("r", 3);
        circle.setAttribute("fill", "var(--ink)");
        circle.setAttribute("stroke", "var(--white)");
        circle.setAttribute("stroke-width", 1.5);
        svg.appendChild(circle);
      });
    });
  }

  // Count activity changes for aria-label
  svg.setAttribute("aria-label", `Activity over time. Now: ${ACTIVITY_TEXT[nodePoints[nodePoints.length - 1]?.activity] || "unknown"}. ${bands.length} changes.`);

  wrapper.appendChild(svg);
}

// Activity text mapping (must match app.js)
const ACTIVITY_TEXT = {
  idle: "Idle",
  book_on: "Book on the mat",
  recording: "Recording",
  waiting_ai: "Waiting for AI",
  feedback: "Giving feedback",
  quiet_study: "Quiet study",
  stopped: "Stopped (STOP button)",
};

// Export drawing functions
window.drawLineChartFull = drawLineChartFull;
window.drawTimeline = drawTimeline;
window.drawActivityStrip = drawActivityStrip;