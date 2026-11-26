// charts.js - SVG chart drawing functions

// Draw a line chart with area, threshold line, hover tooltip
function drawLineChart(wrapper, points, options) {
  const width = wrapper.clientWidth || 600;
  const height = 200;
  const padding = { left: 44, right: 12, top: 12, bottom: 24 };
  const innerWidth = width - padding.left - padding.right;
  const innerHeight = height - padding.top - padding.bottom;

  // Clear wrapper
  wrapper.innerHTML = '';

  // Filter valid points (not null)
  const validPoints = points.filter(p => p.value !== null && p.value !== undefined);
  if (validPoints.length === 0) {
    wrapper.setAttribute('aria-label', options.ariaLabel || 'No data');
    return;
  }

  // Find min/max for y scale
  let minVal = Math.min(...validPoints.map(p => p.value));
  let maxVal = Math.max(...validPoints.map(p => p.value));

  // Nice max
  const range = maxVal - minVal;
  if (range > 0) {
    const niceMax = getNiceMax(maxVal);
    maxVal = niceMax;
  } else {
    maxVal = minVal + 1;
  }
  minVal = Math.min(minVal, 0);

  // Include threshold if provided
  if (options.threshold !== undefined) {
    maxVal = Math.max(maxVal, options.threshold * 1.1);
  }
  maxVal = maxVal * 1.1;

  // X scale: time
  const now = options.now || Date.now() / 1000;
  const minutes = options.minutes || 15;
  const startTime = now - minutes * 60;

  function xScale(t) {
    return padding.left + ((t - startTime) / (minutes * 60)) * innerWidth;
  }

  function yScale(v) {
    return padding.top + innerHeight - ((v - minVal) / (maxVal - minVal)) * innerHeight;
  }

  // Create SVG
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  svg.setAttribute('width', '100%');
  svg.setAttribute('height', height);
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', options.ariaLabel || 'Chart');

  // Grid lines (y)
  const gridCount = 3;
  for (let i = 0; i <= gridCount; i++) {
    const v = minVal + (maxVal - minVal) * (i / gridCount);
    const y = yScale(v);
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', padding.left);
    line.setAttribute('x2', width - padding.right);
    line.setAttribute('y1', y);
    line.setAttribute('y2', y);
    line.setAttribute('stroke', '#ebe8e3');
    line.setAttribute('stroke-width', '1');
    svg.appendChild(line);

    // Y label
    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', padding.left - 6);
    text.setAttribute('y', y + 4);
    text.setAttribute('font-size', '11');
    text.setAttribute('fill', '#6b6b6b');
    text.setAttribute('text-anchor', 'end');
    text.setAttribute('font-variant-numeric', 'tabular-nums');
    text.textContent = formatNumber(v, options.unit);
    svg.appendChild(text);
  }

  // X grid lines and labels (approx 5 labels)
  const xTickCount = Math.min(5, minutes <= 5 ? 5 : minutes <= 15 ? 5 : 6);
  for (let i = 0; i <= xTickCount; i++) {
    const t = startTime + (minutes * 60) * (i / xTickCount);
    const x = xScale(t);
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', x);
    line.setAttribute('x2', x);
    line.setAttribute('y1', padding.top);
    line.setAttribute('y2', height - padding.bottom);
    line.setAttribute('stroke', '#ebe8e3');
    line.setAttribute('stroke-width', '1');
    svg.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', x);
    text.setAttribute('y', height - padding.bottom + 16);
    text.setAttribute('font-size', '11');
    text.setAttribute('fill', '#6b6b6b');
    text.setAttribute('text-anchor', 'middle');
    text.setAttribute('font-variant-numeric', 'tabular-nums');
    text.textContent = formatTime(t);
    svg.appendChild(text);
  }

  // Threshold line
  if (options.threshold !== undefined) {
    const y = yScale(options.threshold);
    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', padding.left);
    line.setAttribute('x2', width - padding.right);
    line.setAttribute('y1', y);
    line.setAttribute('y2', y);
    line.setAttribute('stroke', 'var(--red)');
    line.setAttribute('stroke-width', '1.5');
    svg.appendChild(line);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', width - padding.right - 4);
    text.setAttribute('y', y - 4);
    text.setAttribute('font-size', '11');
    text.setAttribute('fill', 'var(--red)');
    text.setAttribute('text-anchor', 'end');
    text.textContent = options.thresholdLabel || `threshold: ${options.threshold}`;
    svg.appendChild(text);
  }

  // Split points into runs (null breaks)
  const runs = [];
  let currentRun = [];
  points.forEach(p => {
    if (p.value !== null && p.value !== undefined) {
      currentRun.push(p);
    } else if (currentRun.length > 0) {
      runs.push(currentRun);
      currentRun = [];
    }
  });
  if (currentRun.length > 0) runs.push(currentRun);

  // Draw each run (area + line)
  runs.forEach(run => {
    if (run.length < 2) return;

    // Area path
    const areaPath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    let d = `M ${xScale(run[0].time)} ${height - padding.bottom}`;
    run.forEach(p => {
      d += ` L ${xScale(p.time)} ${yScale(p.value)}`;
    });
    d += ` L ${xScale(run[run.length - 1].time)} ${height - padding.bottom} Z`;
    areaPath.setAttribute('d', d);
    areaPath.setAttribute('fill', 'var(--ink)');
    areaPath.setAttribute('fill-opacity', '0.08');
    svg.appendChild(areaPath);

    // Line path
    const linePath = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    d = `M ${xScale(run[0].time)} ${yScale(run[0].value)}`;
    run.forEach(p => {
      d += ` L ${xScale(p.time)} ${yScale(p.value)}`;
    });
    linePath.setAttribute('d', d);
    linePath.setAttribute('stroke', 'var(--ink)');
    linePath.setAttribute('stroke-width', '2');
    linePath.setAttribute('fill', 'none');
    linePath.setAttribute('stroke-linejoin', 'round');
    linePath.setAttribute('stroke-linecap', 'round');
    svg.appendChild(linePath);

    // Points
    run.forEach(p => {
      const circle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
      circle.setAttribute('cx', xScale(p.time));
      circle.setAttribute('cy', yScale(p.value));
      circle.setAttribute('r', '4');
      circle.setAttribute('fill', 'var(--ink)');
      circle.setAttribute('stroke', 'var(--white)');
      circle.setAttribute('stroke-width', '2');
      circle.setAttribute('data-time', p.time);
      circle.setAttribute('data-value', p.value);
      circle.setAttribute('data-label', p.label || '');
      svg.appendChild(circle);
    });
  });

  // Last point highlight (most recent valid point)
  const lastValid = [...validPoints].pop();
  if (lastValid) {
    const circle = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    circle.setAttribute('cx', xScale(lastValid.time));
    circle.setAttribute('cy', yScale(lastValid.value));
    circle.setAttribute('r', '5');
    circle.setAttribute('fill', 'var(--ink)');
    circle.setAttribute('stroke', 'var(--white)');
    circle.setAttribute('stroke-width', '2');
    svg.appendChild(circle);
  }

  wrapper.appendChild(svg);

  // Hover tooltip
  const tip = document.createElement('div');
  tip.className = 'chart-tip hidden';
  tip.style.position = 'absolute';
  wrapper.style.position = 'relative';
  wrapper.appendChild(tip);

  let hoverActive = false;

  svg.addEventListener('pointermove', e => {
    hoverActive = true;
    const rect = svg.getBoundingClientRect();
    const x = e.clientX - rect.left;

    // Find nearest point
    let nearest = null;
    let minDist = Infinity;
    svg.querySelectorAll('circle[data-value]').forEach(circle => {
      const cx = parseFloat(circle.getAttribute('cx'));
      const dist = Math.abs(cx - x);
      if (dist < minDist) {
        minDist = dist;
        nearest = circle;
      }
    });

    if (nearest && minDist < 30) {
      const time = parseFloat(nearest.getAttribute('data-time'));
      const value = parseFloat(nearest.getAttribute('data-value'));
      const label = nearest.getAttribute('data-label') || options.label || 'Value';
      const unit = options.unit || '';

      tip.textContent = `${formatTime(time)}  ${label}: ${formatNumber(value, unit)}`;
      tip.classList.remove('hidden');

      // Position tooltip
      const cx = parseFloat(nearest.getAttribute('cx'));
      const cy = parseFloat(nearest.getAttribute('cy'));
      let tipX = cx + 12;
      if (tipX + 200 > width) tipX = cx - 12 - tip.offsetWidth;
      tip.style.left = `${tipX}px`;
      tip.style.top = `${cy - 20}px`;

      // Show crosshair and highlight point
      if (!svg.querySelector('.crosshair')) {
        const crosshair = document.createElementNS('http://www.w3.org/2000/svg', 'line');
        crosshair.setAttribute('class', 'crosshair');
        crosshair.setAttribute('stroke', '#6b6b6b');
        crosshair.setAttribute('stroke-width', '1');
        crosshair.setAttribute('stroke-dasharray', '4,4');
        crosshair.setAttribute('y1', padding.top);
        crosshair.setAttribute('y2', height - padding.bottom);
        svg.appendChild(crosshair);
      }
      svg.querySelector('.crosshair').setAttribute('x1', cx);
      svg.querySelector('.crosshair').setAttribute('x2', cx);

      const highlight = nearest.cloneNode();
      highlight.setAttribute('r', '6');
      highlight.setAttribute('fill', 'var(--red)');
      highlight.setAttribute('class', 'hover-highlight');
      svg.querySelectorAll('.hover-highlight').forEach(h => h.remove());
      svg.appendChild(highlight);
    } else {
      tip.classList.add('hidden');
      svg.querySelectorAll('.crosshair, .hover-highlight').forEach(h => h.remove());
    }
  });

  svg.addEventListener('pointerleave', () => {
    hoverActive = false;
    tip.classList.add('hidden');
    svg.querySelectorAll('.crosshair, .hover-highlight').forEach(h => h.remove());
  });

  // Return info for redraw check
  return { hoverActive: () => hoverActive };
}

// Draw timeline chart (messages received)
function drawTimeline(wrapper, nodesData, options) {
  const width = wrapper.clientWidth || 600;
  const rowHeight = 30;
  const labelWidth = 70;
  const height = 3 * rowHeight + 40;
  const padding = { left: labelWidth, right: 12, top: 12, bottom: 24 };
  const innerWidth = width - padding.left - padding.right;

  wrapper.innerHTML = '';

  const now = options.now || Date.now() / 1000;
  const minutes = options.minutes || 15;
  const startTime = now - minutes * 60;
  const staleAfter = options.staleAfter || 30;

  function xScale(t) {
    return padding.left + ((t - startTime) / (minutes * 60)) * innerWidth;
  }

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  svg.setAttribute('width', '100%');
  svg.setAttribute('height', height);
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', 'Messages received timeline');

  const nodeOrder = [0, 1, 2];
  const nodeNames = { 0: 'Nest', 1: 'Doll 1', 2: 'Doll 2' };

  nodeOrder.forEach((nodeId, row) => {
    const y = padding.top + row * rowHeight;
    const nodeData = nodesData[nodeId] || [];
    const events = nodeData.events || [];
    const counts = nodeData.counts || { added: 0, duplicate: 0 };

    // Label
    const label = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    label.setAttribute('x', padding.left - 10);
    label.setAttribute('y', y + rowHeight / 2 + 5);
    label.setAttribute('font-size', '13');
    label.setAttribute('fill', 'var(--ink)');
    label.setAttribute('text-anchor', 'end');
    label.setAttribute('font-weight', '500');
    label.textContent = nodeNames[nodeId];
    svg.appendChild(label);

    // Sort events by time
    const sorted = [...events].sort((a, b) => a.recorded_at - b.recorded_at);

    // Draw ticks and gaps
    let prevTime = null;
    sorted.forEach((ev, i) => {
      const t = ev.recorded_at;
      if (t < startTime || t > now) return;

      const x = xScale(t);

      // Tick mark
      const tick = document.createElementNS('http://www.w3.org/2000/svg', 'line');
      tick.setAttribute('x1', x);
      tick.setAttribute('x2', x);
      tick.setAttribute('y1', y + 4);
      tick.setAttribute('y2', y + rowHeight - 4);
      tick.setAttribute('stroke', 'var(--ink)');
      tick.setAttribute('stroke-width', '2');
      svg.appendChild(tick);

      // Gap from previous
      if (prevTime !== null) {
        const gap = t - prevTime;
        if (gap > staleAfter) {
          const gapStart = xScale(prevTime);
          const gapWidth = x - gapStart;
          const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
          rect.setAttribute('x', gapStart);
          rect.setAttribute('y', y + 2);
          rect.setAttribute('width', gapWidth);
          rect.setAttribute('height', rowHeight - 4);
          rect.setAttribute('fill', 'var(--red)');
          rect.setAttribute('fill-opacity', '0.3');
          svg.appendChild(rect);

          // Gap label if wide enough
          if (gapWidth > 60) {
            const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            text.setAttribute('x', gapStart + gapWidth / 2);
            text.setAttribute('y', y + rowHeight / 2 + 4);
            text.setAttribute('font-size', '11');
            text.setAttribute('fill', 'var(--red)');
            text.setAttribute('text-anchor', 'middle');
            text.setAttribute('font-weight', '500');
            text.textContent = `gap ${Math.round(gap)}s`;
            svg.appendChild(text);
          }

          // Title for hover
          const title = document.createElementNS('http://www.w3.org/2000/svg', 'title');
          title.textContent = `Gap: ${formatTime(prevTime)} to ${formatTime(t)} (${Math.round(gap)}s)`;
          rect.appendChild(title);
        }
      }
      prevTime = t;
    });

    // Gap from last event to now
    if (sorted.length > 0) {
      const lastTime = sorted[sorted.length - 1].recorded_at;
      const gap = now - lastTime;
      if (gap > staleAfter) {
        const gapStart = xScale(lastTime);
        const gapWidth = xScale(now) - gapStart;
        const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
        rect.setAttribute('x', gapStart);
        rect.setAttribute('y', y + 2);
        rect.setAttribute('width', gapWidth);
        rect.setAttribute('height', rowHeight - 4);
        rect.setAttribute('fill', 'var(--red)');
        rect.setAttribute('fill-opacity', '0.3');
        svg.appendChild(rect);

        if (gapWidth > 60) {
          const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
          text.setAttribute('x', gapStart + gapWidth / 2);
          text.setAttribute('y', y + rowHeight / 2 + 4);
          text.setAttribute('font-size', '11');
          text.setAttribute('fill', 'var(--red)');
          text.setAttribute('text-anchor', 'middle');
          text.setAttribute('font-weight', '500');
          text.textContent = `gap ${Math.round(gap)}s`;
          svg.appendChild(text);
        }

        const title = document.createElementNS('http://www.w3.org/2000/svg', 'title');
        title.textContent = `Gap: ${formatTime(lastTime)} to now (${Math.round(gap)}s)`;
        rect.appendChild(title);
      }
    }

    // Counters
    const counterText = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    counterText.setAttribute('x', width - padding.right);
    counterText.setAttribute('y', y + rowHeight / 2 + 5);
    counterText.setAttribute('font-size', '12');
    counterText.setAttribute('fill', 'var(--grey)');
    counterText.setAttribute('text-anchor', 'end');
    counterText.setAttribute('font-variant-numeric', 'tabular-nums');
    counterText.textContent = `${counts.added} received, ${counts.duplicate} duplicate`;
    svg.appendChild(counterText);
  });

  // Caption / legend
  const caption = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  caption.setAttribute('x', padding.left);
  caption.setAttribute('y', height - padding.bottom + 18);
  caption.setAttribute('font-size', '12');
  caption.setAttribute('fill', 'var(--grey)');
  caption.textContent = 'Messages received — red = no message for over 30 s';
  svg.appendChild(caption);

  wrapper.appendChild(svg);
}

// Draw activity strip
function drawActivityStrip(wrapper, events, options) {
  const width = wrapper.clientWidth || 600;
  const height = 70;
  const padding = { left: 0, right: 12, top: 12, bottom: 24 };
  const innerWidth = width - padding.left - padding.right;

  wrapper.innerHTML = '';

  const now = options.now || Date.now() / 1000;
  const minutes = options.minutes || 15;
  const startTime = now - minutes * 60;

  function xScale(t) {
    return padding.left + ((t - startTime) / (minutes * 60)) * innerWidth;
  }

  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', `0 0 ${width} ${height}`);
  svg.setAttribute('width', '100%');
  svg.setAttribute('height', height);
  svg.setAttribute('role', 'img');

  // Activity colors
  const activityColors = {
    idle: '#f4f2ee',
    recording: '#d9d6d1',
    waiting_ai: '#d9d6d1',
    feedback: '#d9d6d1',
    stopped: 'var(--red)',
    book_on: '#ebe8e3',
    quiet_study: '#ebe8e3',
    default: '#ebe8e3'
  };

  const activityLabels = {
    idle: 'Idle',
    book_on: 'Book on the mat',
    recording: 'Recording',
    waiting_ai: 'Waiting for AI',
    feedback: 'Giving feedback',
    quiet_study: 'Quiet study',
    stopped: 'Stopped (STOP button)'
  };

  // Filter and sort events with activity
  const activityEvents = events
    .filter(e => e.activity !== null && e.activity !== undefined)
    .sort((a, b) => a.recorded_at - b.recorded_at);

  if (activityEvents.length === 0) {
    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', width / 2);
    text.setAttribute('y', height / 2 + 4);
    text.setAttribute('font-size', '13');
    text.setAttribute('fill', 'var(--grey)');
    text.setAttribute('text-anchor', 'middle');
    text.textContent = 'No activity yet';
    svg.appendChild(text);
    wrapper.appendChild(svg);
    return;
  }

  // Merge consecutive same activity
  const bands = [];
  let currentBand = null;
  activityEvents.forEach(ev => {
    if (currentBand && currentBand.activity === ev.activity) {
      currentBand.end = ev.recorded_at;
    } else {
      if (currentBand) bands.push(currentBand);
      currentBand = {
        activity: ev.activity,
        start: ev.recorded_at,
        end: ev.recorded_at
      };
    }
  });
  if (currentBand) bands.push(currentBand);

  // Extend last band to now if it's recent
  if (bands.length > 0 && now - bands[bands.length - 1].end < 30) {
    bands[bands.length - 1].end = now;
  }

  const bandHeight = 26;
  const bandY = padding.top + 8;

  bands.forEach((band, i) => {
    const x = xScale(band.start);
    const xEnd = xScale(Math.min(band.end, now));
    const w = Math.max(2, xEnd - x);

    const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    rect.setAttribute('x', x);
    rect.setAttribute('y', bandY);
    rect.setAttribute('width', w);
    rect.setAttribute('height', bandHeight);
    rect.setAttribute('rx', '3');
    rect.setAttribute('ry', '3');
    rect.setAttribute('fill', activityColors[band.activity] || activityColors.default);
    svg.appendChild(rect);

    // Label if fits
    const label = activityLabels[band.activity] || band.activity;
    const charWidth = 7;
    const minWidth = label.length * charWidth + 8;
    if (w >= minWidth) {
      const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
      text.setAttribute('x', x + w / 2);
      text.setAttribute('y', bandY + bandHeight / 2 + 4);
      text.setAttribute('font-size', '12');
      text.setAttribute('fill', band.activity === 'stopped' ? 'var(--white)' : 'var(--ink)');
      text.setAttribute('text-anchor', 'middle');
      text.setAttribute('font-weight', '500');
      text.textContent = label;
      svg.appendChild(text);
    }

    // Title for hover
    const title = document.createElementNS('http://www.w3.org/2000/svg', 'title');
    title.textContent = `${label}: ${formatTime(band.start)} to ${formatTime(band.end)}`;
    rect.appendChild(title);
  });

  wrapper.appendChild(svg);
}

// Draw pressure line chart (sub-chart under activity)
function drawPressureChart(wrapper, points, options) {
  return drawLineChart(wrapper, points, {
    ...options,
    threshold: undefined,
    thresholdLabel: undefined,
    ariaLabel: options.ariaLabel || 'Pressure chart'
  });
}

// Helper functions
function getNiceMax(max) {
  const magnitude = Math.pow(10, Math.floor(Math.log10(max)));
  const normalized = max / magnitude;
  let nice;
  if (normalized <= 1) nice = 1;
  else if (normalized <= 2) nice = 2;
  else if (normalized <= 2.5) nice = 2.5;
  else if (normalized <= 5) nice = 5;
  else nice = 10;
  return nice * magnitude;
}

function formatNumber(val, unit) {
  if (unit === 'lux') {
    return val.toFixed(val >= 100 ? 0 : 1);
  }
  if (unit === 'cm') {
    return val.toFixed(1);
  }
  if (unit === 'fsr' || unit === 'pressure') {
    return Math.round(val).toString();
  }
  return val.toFixed(1);
}

function formatTime(t) {
  const date = new Date(t * 1000);
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

// Expose globally
window.drawLineChart = drawLineChart;
window.drawTimeline = drawTimeline;
window.drawActivityStrip = drawActivityStrip;
window.drawPressureChart = drawPressureChart;