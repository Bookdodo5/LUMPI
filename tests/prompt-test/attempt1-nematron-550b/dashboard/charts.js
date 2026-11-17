/* LUMPI Teacher Dashboard - Charts (SVG drawing) */

// Draw a line chart with area under the curve
// options: { wrapper, data, labelKey, valueKey, threshold, thresholdLabel, unit, darkLux, isDarkRoom }
function drawLineChart(options) {
  const { wrapper, data, labelKey, valueKey, threshold, thresholdLabel, unit, darkLux, isDarkRoom } = options;

  // Clear wrapper
  wrapper.innerHTML = "";

  // Compute dimensions
  const width = wrapper.clientWidth || 600;
  const height = 220;
  const padding = { left: 44, right: 12, top: 12, bottom: 24 };
  const plotWidth = width - padding.left - padding.right;
  const plotHeight = height - padding.top - padding.bottom;

  // Collect valid points (non-null values)
  const validPoints = data.filter(p => p[valueKey] !== null && p[valueKey] !== undefined);
  if (validPoints.length === 0) {
    wrapper.innerHTML = '<svg viewBox="0 0 ' + width + ' ' + height + '" width="100%" height="auto" role="img" aria-label="No data"><text x="50%" y="50%" dominant-baseline="middle" text-anchor="middle" fill="var(--grey)" font-size="13px">No data</text></svg>';
    return;
  }

  // Find min/max for y scale
  let minVal = Infinity, maxVal = -Infinity;
  for (const p of validPoints) {
    const v = p[valueKey];
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }
  if (threshold !== undefined) {
    if (threshold < minVal) minVal = threshold;
    if (threshold > maxVal) maxVal = threshold;
  }

  // Nice max (1, 2, 2.5, 5, 10 × power of 10)
  const range = maxVal - minVal;
  const niceMax = getNiceMax(maxVal, range);
  const niceMin = minVal > 0 ? 0 : getNiceMin(minVal, range);

  // X scale: time range
  const times = data.map(p => p.t);
  const tMin = Math.min(...times);
  const tMax = Math.max(...times);
  const timeRange = tMax - tMin || 1;

  // Build SVG
  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "auto");
  svg.setAttribute("role", "img");
  svg.style.cursor = "crosshair";

  // Grid lines (y)
  const gridLines = 3; // 0, 50%, 100%
  for (let i = 0; i <= gridLines; i++) {
    const y = padding.top + (i / gridLines) * plotHeight;
    const line = document.createElementNS("http://www.w3.org/2000/svg", "line");
    line.setAttribute("x1", padding.left);
    line.setAttribute("x2", width - padding.right);
    line.setAttribute("y1", y);
    line.setAttribute("y2", y);
    line.setAttribute("stroke", "#ebe8e3");
    line.setAttribute("stroke-width", "1");
    svg.appendChild(line);

    // Y axis labels
    const val = niceMax - (i / gridLines) * (niceMax - niceMin);
    const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
    text.setAttribute("x", padding.left - 8);
    text.setAttribute("y", y + 4);
    text.setAttribute("font-size", "11");
    text.setAttribute("fill", "var(--grey)");
    text.setAttribute("text-anchor", "end");
    text.setAttribute("font-variant-numeric", "tabular-nums");
    text.textContent = formatNumber(val);
    svg.appendChild(text);
  }

  // X axis labels (time)
  const numXTicks = 5;
  for (let i = 0; i <= numXTicks; i++) {
    const x = padding.left + (i / numXTicks) * plotWidth;
    const t = tMin + (i / numXTicks) * timeRange;
    const date = new Date(t * 1000);
    const label = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
    text.setAttribute("x", x);
    text.setAttribute("y", height - padding.bottom + 16);
    text.setAttribute("font-size", "11");
    text.setAttribute("fill", "var(--grey)");
    text.setAttribute("text-anchor", "middle");
    text.setAttribute("font-variant-numeric", "tabular-nums");
    text.textContent = label;
    svg.appendChild(text);
  }

  // Threshold line
  if (threshold !== undefined && threshold >= niceMin && threshold <= niceMax) {
    const ty = padding.top + plotHeight * (1 - (threshold - niceMin) / (niceMax - niceMin));
    const tline = document.createElementNS("http://www.w3.org/2000/svg", "line");
    tline.setAttribute("x1", padding.left);
    tline.setAttribute("x2", width - padding.right);
    tline.setAttribute("y1", ty);
    tline.setAttribute("y2", ty);
    tline.setAttribute("stroke", "var(--red)");
    tline.setAttribute("stroke-width", "1.5");
    tline.setAttribute("stroke-dasharray", "4,4");
    svg.appendChild(tline);

    // Threshold label
    const tlabel = document.createElementNS("http://www.w3.org/2000/svg", "text");
    tlabel.setAttribute("x", width - padding.right - 4);
    tlabel.setAttribute("y", ty - 4);
    tlabel.setAttribute("font-size", "10");
    tlabel.setAttribute("fill", "var(--red)");
    tlabel.setAttribute("text-anchor", "end");
    tlabel.textContent = thresholdLabel || (threshold + " " + unit);
    svg.appendChild(tlabel);
  }

  // Draw data as separate runs (broken by null)
  const runs = [];
  let currentRun = [];
  for (const p of data) {
    if (p[valueKey] !== null && p[valueKey] !== undefined) {
      currentRun.push(p);
    } else {
      if (currentRun.length > 0) {
        runs.push(currentRun);
        currentRun = [];
      }
    }
  }
  if (currentRun.length > 0) runs.push(currentRun);

  // Area and line for each run
  for (const run of runs) {
    if (run.length < 2) continue;

    // Area path
    let areaPath = `M ${xFor(run[0].t)} ${height - padding.bottom}`;
    for (const p of run) {
      areaPath += ` L ${xFor(p.t)} ${yFor(p[valueKey])}`;
    }
    areaPath += ` L ${xFor(run[run.length - 1].t)} ${height - padding.bottom} Z`;

    const area = document.createElementNS("http://www.w3.org/2000/svg", "path");
    area.setAttribute("d", areaPath);
    area.setAttribute("fill", "var(--ink)");
    area.setAttribute("fill-opacity", "0.08");
    svg.appendChild(area);

    // Line path
    let linePath = `M ${xFor(run[0].t)} ${yFor(run[0][valueKey])}`;
    for (let i = 1; i < run.length; i++) {
      linePath += ` L ${xFor(run[i].t)} ${yFor(run[i][valueKey])}`;
    }

    const line = document.createElementNS("http://www.w3.org/2000/svg", "path");
    line.setAttribute("d", linePath);
    line.setAttribute("stroke", "var(--ink)");
    line.setAttribute("stroke-width", "2");
    line.setAttribute("fill", "none");
    line.setAttribute("stroke-linejoin", "round");
    line.setAttribute("stroke-linecap", "round");
    svg.appendChild(line);

    // Last point dot
    const last = run[run.length - 1];
    const dot = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    dot.setAttribute("cx", xFor(last.t));
    dot.setAttribute("cy", yFor(last[valueKey]));
    dot.setAttribute("r", "4");
    dot.setAttribute("fill", "var(--ink)");
    dot.setAttribute("stroke", "var(--white)");
    dot.setAttribute("stroke-width", "2");
    svg.appendChild(dot);
  }

  // Helper functions
  function xFor(t) {
    return padding.left + plotWidth * (t - tMin) / timeRange;
  }
  function yFor(v) {
    return padding.top + plotHeight * (1 - (v - niceMin) / (niceMax - niceMin));
  }

  // Add aria-label
  const latest = validPoints[validPoints.length - 1];
  const lowest = Math.min(...validPoints.map(p => p[valueKey]));
  const highest = Math.max(...validPoints.map(p => p[valueKey]));
  const labelKeyDisplay = labelKey === "lux" ? "Light" : "Distance";
  svg.setAttribute("aria-label", `${labelKeyDisplay}: latest ${latest[valueKey]}, lowest ${lowest}, highest ${highest} ${unit}`);

  wrapper.appendChild(svg);

  // Hover interaction
  setupLineChartHover(wrapper, svg, data, valueKey, unit, labelKey, padding, plotWidth, plotHeight, tMin, timeRange, niceMin, niceMax, width, height, runs);
}

// Draw timeline chart (messages received)
function drawTimeline(options) {
  const { wrapper, data, nodes, staleAfter } = options;
  wrapper.innerHTML = "";

  const width = wrapper.clientWidth || 600;
  const rowHeight = 30;
  const labelWidth = 70;
  const padding = { left: labelWidth, right: 12, top: 12, bottom: 24 };
  const plotWidth = width - padding.left - padding.right;
  const height = padding.top + nodes.length * rowHeight + padding.bottom;

  const now = Date.now() / 1000;
  const tMin = now - 15 * 60; // 15 minutes default, will be overridden by data
  const tMax = now;
  const timeRange = tMax - tMin;

  // Find actual time range from data
  let minT = now, maxT = now;
  for (const nodeData of data) {
    for (const p of nodeData) {
      if (p.t < minT) minT = p.t;
      if (p.t > maxT) maxT = p.t;
    }
  }
  // Ensure at least 5 min range
  if (maxT - minT < 300) {
    minT = maxT - 300;
  }

  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "auto");
  svg.setAttribute("role", "img");

  // Node labels and rows
  const nodeNames = { 0: "Nest", 1: "Doll 1", 2: "Doll 2" };
  let ariaParts = [];

  nodes.forEach((nid, rowIdx) => {
    const y = padding.top + rowIdx * rowHeight;
    const name = nodeNames[nid] || `Node ${nid}`;

    // Label
    const label = document.createElementNS("http://www.w3.org/2000/svg", "text");
    label.setAttribute("x", padding.left - 10);
    label.setAttribute("y", y + rowHeight / 2 + 4);
    label.setAttribute("font-size", "12");
    label.setAttribute("fill", "var(--ink)");
    label.setAttribute("text-anchor", "end");
    label.setAttribute("font-weight", "500");
    label.textContent = name;
    svg.appendChild(label);

    // Row background
    const rowBg = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    rowBg.setAttribute("x", padding.left);
    rowBg.setAttribute("y", y);
    rowBg.setAttribute("width", plotWidth);
    rowBg.setAttribute("height", rowHeight - 2);
    rowBg.setAttribute("fill", rowIdx % 2 === 0 ? "transparent" : "#f4f2ee");
    svg.appendChild(rowBg);

    // Messages as ticks
    const nodePoints = data[nid] || [];
    let gaps = 0;
    let prevT = null;

    for (const p of nodePoints) {
      if (p.t < minT || p.t > maxT) continue;
      const x = padding.left + plotWidth * (p.t - minT) / (maxT - minT);
      const tick = document.createElementNS("http://www.w3.org/2000/svg", "line");
      tick.setAttribute("x1", x);
      tick.setAttribute("x2", x);
      tick.setAttribute("y1", y + 4);
      tick.setAttribute("y2", y + rowHeight - 6);
      tick.setAttribute("stroke", "var(--ink)");
      tick.setAttribute("stroke-width", "2");
      svg.appendChild(tick);

      if (prevT !== null) {
        const gap = p.t - prevT;
        if (gap > staleAfter) {
          gaps++;
          const gapX1 = padding.left + plotWidth * (prevT - minT) / (maxT - minT);
          const gapX2 = x;
          const gapWidth = gapX2 - gapX1;
          const gapRect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
          gapRect.setAttribute("x", gapX1);
          gapRect.setAttribute("y", y + 2);
          gapRect.setAttribute("width", gapWidth);
          gapRect.setAttribute("height", rowHeight - 4);
          gapRect.setAttribute("fill", "var(--red)");
          gapRect.setAttribute("fill-opacity", "0.3");
          const gapSec = Math.round(gap);
          gapRect.setAttribute("title", `Gap: ${gapSec} s (${new Date(prevT * 1000).toLocaleTimeString()} – ${new Date(p.t * 1000).toLocaleTimeString()})`);
          svg.appendChild(gapRect);

          // Gap text if wide enough
          if (gapWidth > 60) {
            const gapText = document.createElementNS("http://www.w3.org/2000/svg", "text");
            gapText.setAttribute("x", gapX1 + gapWidth / 2);
            gapText.setAttribute("y", y + rowHeight / 2 + 4);
            gapText.setAttribute("font-size", "10");
            gapText.setAttribute("fill", "var(--red)");
            gapText.setAttribute("text-anchor", "middle");
            gapText.setAttribute("font-weight", "500");
            gapText.textContent = `gap ${gapSec}s`;
            svg.appendChild(gapText);
          }
        }
      }
      prevT = p.t;
    }

    // Gap from last message to now
    if (nodePoints.length > 0) {
      const lastT = nodePoints[nodePoints.length - 1].t;
      const gap = now - lastT;
      if (gap > staleAfter) {
        gaps++;
        const gapX1 = padding.left + plotWidth * (lastT - minT) / (maxT - minT);
        const gapX2 = width - padding.right;
        const gapWidth = gapX2 - gapX1;
        const gapRect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
        gapRect.setAttribute("x", gapX1);
        gapRect.setAttribute("y", y + 2);
        gapRect.setAttribute("width", gapWidth);
        gapRect.setAttribute("height", rowHeight - 4);
        gapRect.setAttribute("fill", "var(--red)");
        gapRect.setAttribute("fill-opacity", "0.3");
        const gapSec = Math.round(gap);
        gapRect.setAttribute("title", `Gap to now: ${gapSec} s (since ${new Date(lastT * 1000).toLocaleTimeString()})`);
        svg.appendChild(gapRect);
        if (gapWidth > 60) {
          const gapText = document.createElementNS("http://www.w3.org/2000/svg", "text");
          gapText.setAttribute("x", gapX1 + gapWidth / 2);
          gapText.setAttribute("y", y + rowHeight / 2 + 4);
          gapText.setAttribute("font-size", "10");
          gapText.setAttribute("fill", "var(--red)");
          gapText.setAttribute("text-anchor", "middle");
          gapText.setAttribute("font-weight", "500");
          gapText.textContent = `gap ${gapSec}s`;
          svg.appendChild(gapText);
        }
      }
    }

    // Counter text
    const received = nodePoints.length;
    const duplicate = 0; // Not tracked in history, would need counts from nodes API
    const counterText = document.createElementNS("http://www.w3.org/2000/svg", "text");
    counterText.setAttribute("x", width - padding.right);
    counterText.setAttribute("y", y + rowHeight / 2 + 4);
    counterText.setAttribute("font-size", "11");
    counterText.setAttribute("fill", "var(--grey)");
    counterText.setAttribute("text-anchor", "end");
    counterText.setAttribute("font-variant-numeric", "tabular-nums");
    counterText.textContent = `${name}: ${received} received, ${duplicate} duplicate`;
    svg.appendChild(counterText);

    ariaParts.push(`${name} ${received} messages${gaps ? `, ${gaps} gaps` : ''}`);
  });

  // X axis time labels
  const numXTicks = 5;
  for (let i = 0; i <= numXTicks; i++) {
    const x = padding.left + (i / numXTicks) * plotWidth;
    const t = minT + (i / numXTicks) * (maxT - minT);
    const date = new Date(t * 1000);
    const label = date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
    const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
    text.setAttribute("x", x);
    text.setAttribute("y", height - padding.bottom + 16);
    text.setAttribute("font-size", "11");
    text.setAttribute("fill", "var(--grey)");
    text.setAttribute("text-anchor", "middle");
    text.setAttribute("font-variant-numeric", "tabular-nums");
    text.textContent = label;
    svg.appendChild(text);
  }

  svg.setAttribute("aria-label", `Messages received. ${ariaParts.join('. ')}.`);
  wrapper.appendChild(svg);
}

// Draw activity strip
function drawActivityStrip(options) {
  const { wrapper, data, nodeId, nodeName } = options;
  wrapper.innerHTML = "";

  const width = wrapper.clientWidth || 600;
  const height = 44;
  const padding = { left: 0, right: 12, top: 0, bottom: 0 };
  const plotWidth = width - padding.left - padding.right;

  const now = Date.now() / 1000;
  const minT = now - 15 * 60;
  const maxT = now;
  const timeRange = maxT - minT;

  // Find actual time range
  let dataMinT = now, dataMaxT = now;
  for (const p of data) {
    if (p.t < dataMinT) dataMinT = p.t;
    if (p.t > dataMaxT) dataMaxT = p.t;
  }
  const useMinT = Math.min(minT, dataMinT);
  const useMaxT = Math.max(maxT, dataMaxT);
  const useRange = useMaxT - useMinT || 1;

  const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
  svg.setAttribute("width", "100%");
  svg.setAttribute("height", "auto");
  svg.setAttribute("role", "img");

  // Merge consecutive same-activity points
  const bands = [];
  let currentBand = null;
  for (const p of data) {
    const act = p.activity || "idle";
    if (currentBand && currentBand.activity === act) {
      currentBand.end = p.t;
    } else {
      if (currentBand) bands.push(currentBand);
      currentBand = { activity: act, start: p.t, end: p.t };
    }
  }
  if (currentBand) bands.push(currentBand);

  // Extend last band to now if it's recent
  if (bands.length > 0 && now - bands[bands.length - 1].end < 60) {
    bands[bands.length - 1].end = now;
  }

  const bandHeight = 26;
  const bandY = (height - bandHeight) / 2;
  const bandGap = 2;

  let ariaText = `Activity over time. `;
  let changes = 0;
  let currentActivity = null;

  for (let i = 0; i < bands.length; i++) {
    const band = bands[i];
    const x1 = padding.left + plotWidth * (band.start - useMinT) / useRange;
    const x2 = padding.left + plotWidth * (band.end - useMinT) / useRange;
    const bandWidth = x2 - x1;

    // Color
    let fill = "#ebe8e3";
    if (band.activity === "idle") fill = "#f4f2ee";
    else if (["recording", "waiting_ai", "feedback"].includes(band.activity)) fill = "#d9d6d1";
    else if (band.activity === "stopped") fill = "var(--red)";

    const rect = document.createElementNS("http://www.w3.org/2000/svg", "rect");
    rect.setAttribute("x", x1);
    rect.setAttribute("y", bandY);
    rect.setAttribute("width", Math.max(bandWidth, 2));
    rect.setAttribute("height", bandHeight);
    rect.setAttribute("rx", "3");
    rect.setAttribute("fill", fill);
    svg.appendChild(rect);

    // Title tooltip
    const title = document.createElementNS("http://www.w3.org/2000/svg", "title");
    const startStr = new Date(band.start * 1000).toLocaleTimeString();
    const endStr = new Date(band.end * 1000).toLocaleTimeString();
    title.textContent = `${band.activity}: ${startStr} to ${endStr}`;
    rect.appendChild(title);

    // Label inside band if it fits
    const label = formatActivity(band.activity);
    const charWidth = 7;
    const minLabelWidth = label.length * charWidth + 8;
    if (bandWidth >= minLabelWidth) {
      const text = document.createElementNS("http://www.w3.org/2000/svg", "text");
      text.setAttribute("x", x1 + bandWidth / 2);
      text.setAttribute("y", bandY + bandHeight / 2 + 4);
      text.setAttribute("font-size", "11");
      text.setAttribute("fill", band.activity === "stopped" ? "var(--white)" : "var(--ink)");
      text.setAttribute("text-anchor", "middle");
      text.setAttribute("font-weight", "500");
      text.textContent = label;
      svg.appendChild(text);
    }

    if (band.activity !== currentActivity) {
      changes++;
      currentActivity = band.activity;
    }
  }

  // Current activity
  const lastActivity = bands.length > 0 ? bands[bands.length - 1].activity : "idle";
  ariaText += `Now: ${formatActivity(lastActivity)}. ${changes} changes.`;
  svg.setAttribute("aria-label", ariaText);

  wrapper.appendChild(svg);
}

// Draw pressure line chart (similar to line chart but for pressure)
function drawPressureChart(options) {
  const { wrapper, data, valueKey, unit } = options;
  // Reuse drawLineChart logic but simplified
  drawLineChart({
    wrapper,
    data,
    labelKey: "Pressure",
    valueKey,
    threshold: undefined,
    unit,
    isDarkRoom: false,
  });
}

// Helper: format activity name
function formatActivity(act) {
  const map = {
    idle: "Idle",
    book_on: "Book on",
    recording: "Recording",
    waiting_ai: "Waiting AI",
    feedback: "Feedback",
    quiet_study: "Quiet study",
    stopped: "Stopped",
  };
  return map[act] || act;
}

// Helper: format number nicely
function formatNumber(n) {
  if (n >= 1000000) return (n / 1000000).toFixed(1) + "M";
  if (n >= 1000) return (n / 1000).toFixed(1) + "k";
  if (n >= 100) return Math.round(n).toString();
  if (n >= 10) return n.toFixed(1);
  return n.toFixed(2);
}

// Helper: get nice max for axis
function getNiceMax(maxVal, range) {
  if (maxVal <= 0) return 10;
  const magnitude = Math.pow(10, Math.floor(Math.log10(range || maxVal)));
  const steps = [1, 2, 2.5, 5, 10];
  for (const step of steps) {
    const candidate = step * magnitude;
    if (candidate >= maxVal * 1.1) return candidate;
  }
  return 10 * magnitude;
}

// Helper: get nice min for axis
function getNiceMin(minVal, range) {
  if (minVal <= 0) return 0;
  const magnitude = Math.pow(10, Math.floor(Math.log10(range || minVal)));
  const steps = [1, 2, 2.5, 5, 10];
  for (const step of steps) {
    const candidate = -step * magnitude;
    if (candidate <= minVal * 1.1) return 0;
  }
  return 0;
}

// Setup hover for line charts
function setupLineChartHover(wrapper, svg, data, valueKey, unit, labelKey, padding, plotWidth, plotHeight, tMin, timeRange, niceMin, niceMax, width, height, runs) {
  const tip = document.getElementById("chart-tip");
  if (!tip) return;

  let hoverTimeout = null;

  wrapper.addEventListener("pointermove", (e) => {
    const rect = wrapper.getBoundingClientRect();
    const x = e.clientX - rect.left;

    // Check if mouse is over plot area
    if (x < padding.left || x > width - padding.right) {
      tip.style.display = "none";
      return;
    }

    // Find nearest point by x
    let nearest = null;
    let nearestDist = Infinity;
    let nearestRun = null;

    for (const run of runs) {
      for (const p of run) {
        const px = padding.left + plotWidth * (p.t - tMin) / timeRange;
        const dist = Math.abs(px - x);
        if (dist < nearestDist) {
          nearestDist = dist;
          nearest = p;
          nearestRun = run;
        }
      }
    }

    if (!nearest) {
      tip.style.display = "none";
      return;
    }

    // Show crosshair and dot
    const cx = padding.left + plotWidth * (nearest.t - tMin) / timeRange;
    const cy = padding.top + plotHeight * (1 - (nearest[valueKey] - niceMin) / (niceMax - niceMin));

    // Remove old crosshair/dot
    const oldCrosshair = svg.querySelector(".crosshair");
    if (oldCrosshair) oldCrosshair.remove();
    const oldDot = svg.querySelector(".hover-dot");
    if (oldDot) oldDot.remove();

    // Crosshair line
    const crosshair = document.createElementNS("http://www.w3.org/2000/svg", "line");
    crosshair.setAttribute("class", "crosshair");
    crosshair.setAttribute("x1", cx);
    crosshair.setAttribute("x2", cx);
    crosshair.setAttribute("y1", padding.top);
    crosshair.setAttribute("y2", height - padding.bottom);
    crosshair.setAttribute("stroke", "var(--grey)");
    crosshair.setAttribute("stroke-width", "1");
    crosshair.setAttribute("stroke-dasharray", "4,4");
    svg.appendChild(crosshair);

    // Dot on point
    const dot = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    dot.setAttribute("class", "hover-dot");
    dot.setAttribute("cx", cx);
    dot.setAttribute("cy", cy);
    dot.setAttribute("r", "5");
    dot.setAttribute("fill", "var(--ink)");
    dot.setAttribute("stroke", "var(--white)");
    dot.setAttribute("stroke-width", "2");
    svg.appendChild(dot);

    // Tooltip
    const date = new Date(nearest.t * 1000);
    const timeStr = date.toLocaleTimeString();
    const label = labelKey === "lux" ? "Light" : "Distance";
    tip.textContent = `${timeStr} — ${label}: ${nearest[valueKey]} ${unit}`;
    tip.style.display = "block";

    // Position tooltip
    const tipRect = tip.getBoundingClientRect();
    let tipX = e.clientX + 12;
    let tipY = e.clientY - tipRect.height / 2;

    // Flip if would overflow right
    if (tipX + tipRect.width > window.innerWidth - 12) {
      tipX = e.clientX - tipRect.width - 12;
    }
    // Keep in bounds vertically
    if (tipY < 12) tipY = 12;
    if (tipY + tipRect.height > window.innerHeight - 12) tipY = window.innerHeight - tipRect.height - 12;

    tip.style.left = tipX + "px";
    tip.style.top = tipY + "px";
  });

  wrapper.addEventListener("pointerleave", () => {
    tip.style.display = "none";
    const crosshair = svg.querySelector(".crosshair");
    if (crosshair) crosshair.remove();
    const dot = svg.querySelector(".hover-dot");
    if (dot) dot.remove();
  });
}

// Main drawTrends function called from app.js
function drawTrends(historyData, nodesData) {
  // Skip redraw if mouse is over any trends chart
  const trendsSection = document.querySelector(".trends");
  if (trendsSection && trendsSection.matches(":hover")) {
    return;
  }

  const chosenMinutes = getChosenMinutes();
  const staleAfter = historyData.stale_after_s || 30;
  const darkLux = historyData.dark_lux || 100;

  // Node data for timeline
  const timelineData = {};
  for (const nid of [0, 1, 2]) {
    timelineData[nid] = historyData.nodes[nid] || [];
  }

  // Lux chart
  const luxWrapper = document.getElementById("chart-lux");
  if (luxWrapper) {
    const luxData = (historyData.nodes["0"] || []).map(p => ({
      t: p.t,
      lux: p.data?.lux ?? null,
    }));
    drawLineChart({
      wrapper: luxWrapper,
      data: luxData,
      labelKey: "lux",
      valueKey: "lux",
      threshold: darkLux,
      thresholdLabel: "dark below 100 lux",
      unit: "lux",
      darkLux,
      isDarkRoom: false,
    });
  }

  // Distance chart
  const distWrapper = document.getElementById("chart-distance");
  if (distWrapper) {
    const distData = (historyData.nodes["0"] || []).map(p => ({
      t: p.t,
      distance_cm: p.data?.distance_cm ?? null,
    }));
    drawLineChart({
      wrapper: distWrapper,
      data: distData,
      labelKey: "distance_cm",
      valueKey: "distance_cm",
      threshold: undefined,
      unit: "cm",
      darkLux,
      isDarkRoom: false,
    });
  }

  // Timeline
  const timelineWrapper = document.getElementById("chart-timeline");
  if (timelineWrapper) {
    drawTimeline({
      wrapper: timelineWrapper,
      data: timelineData,
      nodes: [0, 1, 2],
      staleAfter,
    });
  }

  // Doll 1 activity + pressure
  const act1Wrapper = document.getElementById("chart-activity-1");
  if (act1Wrapper) {
    const doll1Data = (historyData.nodes["1"] || []).map(p => ({
      t: p.t,
      activity: p.activity,
    }));
    drawActivityStrip({
      wrapper: act1Wrapper,
      data: doll1Data,
      nodeId: 1,
      nodeName: "Doll 1",
    });
  }

  const press1Wrapper = document.getElementById("chart-pressure-1");
  if (press1Wrapper) {
    const press1Data = (historyData.nodes["1"] || []).map(p => ({
      t: p.t,
      fsr: p.data?.fsr ?? null,
    }));
    drawPressureChart({
      wrapper: press1Wrapper,
      data: press1Data,
      valueKey: "fsr",
      unit: "",
    });
  }

  // Doll 2 activity + pressure
  const act2Wrapper = document.getElementById("chart-activity-2");
  if (act2Wrapper) {
    const doll2Data = (historyData.nodes["2"] || []).map(p => ({
      t: p.t,
      activity: p.activity,
    }));
    drawActivityStrip({
      wrapper: act2Wrapper,
      data: doll2Data,
      nodeId: 2,
      nodeName: "Doll 2",
    });
  }

  const press2Wrapper = document.getElementById("chart-pressure-2");
  if (press2Wrapper) {
    const press2Data = (historyData.nodes["2"] || []).map(p => ({
      t: p.t,
      fsr: p.data?.fsr ?? null,
    }));
    drawPressureChart({
      wrapper: press2Wrapper,
      data: press2Data,
      valueKey: "fsr",
      unit: "",
    });
  }
}

// Get chosen minutes from radio buttons
function getChosenMinutes() {
  const checked = document.querySelector('input[name="range"]:checked');
  return checked ? parseInt(checked.value, 10) : 15;
}

// Expose globally
window.drawTrends = drawTrends;
window.getChosenMinutes = getChosenMinutes;