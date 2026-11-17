// app.js - Dashboard polling and rendering

const POLL_MS = 1000;
let pollTimer = null;
let drawnEventsKey = null;
let drawnErrorsKey = null;
let chosenMinutesValue = 15;

// Mappings
const NODE_NAMES = { 0: "Nest", 1: "Doll 1", 2: "Doll 2", 3: "Doll 3" };
const LABELS = {
  lux: "Light (lux)",
  distance_cm: "Teacher distance (cm)",
  uid: "Book UID",
  passage: "Passage",
  attempt: "Attempt",
  fsr: "Pressure"
};
const ACTIVITY_TEXT = {
  idle: "Idle",
  book_on: "Book on the mat",
  recording: "Recording",
  waiting_ai: "Waiting for AI",
  feedback: "Giving feedback",
  quiet_study: "Quiet study",
  stopped: "Stopped (STOP button)"
};

async function getJson(path) {
  const resp = await fetch(path, { cache: "no-store" });
  if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
  return resp.json();
}

function chosenMinutes() {
  const input = document.querySelector('input[name="range"]:checked');
  return input ? parseInt(input.value) : 15;
}

function setConnected(ok) {
  const el = document.getElementById('connection');
  if (ok) {
    el.textContent = 'Server connected';
    el.classList.remove('disconnected');
    document.body.classList.remove('disconnected');
  } else {
    el.textContent = 'Server disconnected — retrying';
    el.classList.add('disconnected');
    document.body.classList.add('disconnected');
  }
}

function formatTime(t) {
  const date = new Date(t * 1000);
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function formatNumber(val, key) {
  if (val === null || val === undefined) return 'no reading';
  if (key === 'lux') return val >= 100 ? Math.round(val).toString() : val.toFixed(1);
  if (key === 'distance_cm') return val.toFixed(1);
  if (key === 'fsr') return Math.round(val).toString();
  return val.toString();
}

function drawNode(node) {
  const card = document.querySelector(`.card[data-node="${node.node_id}"]`);
  if (!card) return;

  const badge = card.querySelector('.badge');
  const warning = card.querySelector('.warning');
  const lowLight = card.querySelector('.low-light');
  const activity = card.querySelector('.activity');
  const values = card.querySelector('.values');
  const seen = card.querySelector('.seen');

  const status = node.status || 'unknown';
  const age = node.age_s;

  // Badge
  badge.textContent = status.toUpperCase();
  badge.className = 'badge ' + status;
  if (status === 'unknown') badge.classList.add('unknown');
  else if (status === 'online') badge.classList.add('online');
  else if (status === 'stale') badge.classList.add('stale');
  else if (status === 'offline') badge.classList.add('offline');
  else if (status === 'no-server') badge.classList.add('no-server');

  // Card border state
  card.classList.remove('is-stale', 'is-offline');
  if (status === 'stale') card.classList.add('is-stale');
  if (status === 'offline') card.classList.add('is-offline');

  // Warning
  if (status === 'stale' && age !== undefined) {
    warning.textContent = `${NODE_NAMES[node.node_id]} — STALE. No update for ${Math.round(age)} seconds.`;
  } else if (status === 'offline') {
    warning.textContent = `${NODE_NAMES[node.node_id]} — OFFLINE. Check power and Wi-Fi.`;
  } else {
    warning.textContent = '';
  }

  // Nest low light
  if (lowLight) {
    lowLight.hidden = node.low_light !== true;
  }

  // Unknown - no data
  if (status === 'unknown') {
    values.innerHTML = '';
    if (activity) activity.textContent = '';
    seen.textContent = 'No data yet';
    return;
  }

  // Activity (dolls only)
  if (activity && node.activity !== null && node.activity !== undefined) {
    activity.textContent = ACTIVITY_TEXT[node.activity] || node.activity;
  } else if (activity) {
    activity.textContent = '';
  }

  // Values
  values.innerHTML = '';
  const knownKeys = ['lux', 'distance_cm', 'uid', 'passage', 'attempt', 'fsr'];
  knownKeys.forEach(key => {
    if (node.values && node.values[key] !== undefined) {
      const div = document.createElement('div');
      const dt = document.createElement('dt');
      dt.textContent = LABELS[key] || key;
      const dd = document.createElement('dd');
      dd.textContent = formatNumber(node.values[key], key);
      div.appendChild(dt);
      div.appendChild(dd);
      values.appendChild(div);
    }
  });
  // Unknown keys
  if (node.values) {
    Object.keys(node.values).forEach(key => {
      if (!knownKeys.includes(key)) {
        const div = document.createElement('div');
        const dt = document.createElement('dt');
        dt.textContent = key;
        const dd = document.createElement('dd');
        dd.textContent = formatNumber(node.values[key], key);
        div.appendChild(dt);
        div.appendChild(dd);
        values.appendChild(div);
      }
    });
  }

  // Last seen
  if (age !== undefined && node.last_seen !== undefined) {
    seen.textContent = `Last update ${Math.round(age)} s ago (${formatTime(node.last_seen)})`;
  } else {
    seen.textContent = 'No data yet';
  }
}

function drawEvents(events) {
  const tbody = document.getElementById('events-body');
  const newKey = events.length > 0 ? events[0].event_id : null;

  if (drawnEventsKey === newKey) return;

  // Collect open details keys
  const openKeys = [];
  tbody.querySelectorAll('details[open]').forEach(d => {
    openKeys.push(d.dataset.key);
  });

  tbody.innerHTML = '';
  events.forEach(ev => {
    const tr = document.createElement('tr');

    // Time
    const tdTime = document.createElement('td');
    tdTime.textContent = formatTime(ev.recorded_at);
    tr.appendChild(tdTime);

    // Node
    const tdNode = document.createElement('td');
    tdNode.textContent = NODE_NAMES[ev.node_id] || `Node ${ev.node_id}`;
    tr.appendChild(tdNode);

    // Type
    const tdType = document.createElement('td');
    tdType.textContent = ev.event_type;
    tr.appendChild(tdType);

    // Value
    const tdValue = document.createElement('td');
    let valueText = '';
    if (ev.activity !== null && ev.activity !== undefined) {
      valueText = ACTIVITY_TEXT[ev.activity] || ev.activity;
    }
    if (ev.values) {
      const pairs = [];
      Object.keys(LABELS).forEach(key => {
        if (ev.values[key] !== undefined) {
          pairs.push(`${LABELS[key]}: ${formatNumber(ev.values[key], key)}`);
        }
      });
      Object.keys(ev.values).forEach(key => {
        if (!LABELS[key] && ev.values[key] !== undefined) {
          pairs.push(`${key}: ${formatNumber(ev.values[key], key)}`);
        }
      });
      if (pairs.length > 0) {
        if (valueText) valueText += ', ';
        valueText += pairs.join(', ');
      }
    }
    tdValue.textContent = valueText || '—';
    tr.appendChild(tdValue);

    // Delay
    const tdDelay = document.createElement('td');
    if (ev.received_at !== undefined && ev.recorded_at !== undefined) {
      tdDelay.textContent = (ev.received_at - ev.recorded_at).toFixed(1) + ' s';
    } else {
      tdDelay.textContent = '—';
    }
    tr.appendChild(tdDelay);

    // Details
    const tdDetails = document.createElement('td');
    const details = document.createElement('details');
    details.dataset.key = ev.event_id;
    if (openKeys.includes(ev.event_id)) details.open = true;
    const summary = document.createElement('summary');
    summary.textContent = 'JSON';
    details.appendChild(summary);
    const pre = document.createElement('pre');
    const copy = { ...ev };
    delete copy.received_at;
    pre.textContent = JSON.stringify(copy, null, 2);
    details.appendChild(pre);
    tdDetails.appendChild(details);
    tr.appendChild(tdDetails);

    tbody.appendChild(tr);
  });

  drawnEventsKey = newKey;
}

function drawErrors(data) {
  const list = document.getElementById('error-list');
  const empty = document.getElementById('dev-empty');
  const totalEl = document.getElementById('rejected-total');

  const errors = data.errors || [];
  const total = data.rejected_total || 0;

  totalEl.textContent = total;

  if (errors.length === 0) {
    empty.style.display = 'block';
    list.innerHTML = '';
    drawnErrorsKey = null;
    return;
  }

  empty.style.display = 'none';

  const newKey = errors.length > 0 ? errors[0].id : null;
  if (drawnErrorsKey === newKey) return;

  // Collect open details keys
  const openKeys = [];
  list.querySelectorAll('details[open]').forEach(d => {
    openKeys.push(d.dataset.key);
  });

  list.innerHTML = '';
  errors.forEach(err => {
    const details = document.createElement('details');
    details.dataset.key = err.id;
    if (openKeys.includes(err.id)) details.open = true;

    const summary = document.createElement('summary');
    summary.textContent = `${formatTime(err.time)} · from ${err.source} · ${err.reason}`;
    details.appendChild(summary);

    // What is wrong
    addErrorBlock(details, 'What is wrong', err.reason);
    // How to fix
    addErrorBlock(details, 'How to fix', err.hint || 'No hint available');
    // What the server received
    addErrorBlock(details, 'What the server received', err.raw || '(body not kept)');
    // A correct packet looks like
    addErrorBlock(details, 'A correct packet looks like', JSON.stringify(err.example, null, 2));

    list.appendChild(details);
  });

  drawnErrorsKey = newKey;
}

function addErrorBlock(parent, title, body) {
  const block = document.createElement('div');
  block.className = 'error-block';
  const titleEl = document.createElement('div');
  titleEl.className = 'error-title';
  titleEl.textContent = title;
  block.appendChild(titleEl);
  const bodyEl = document.createElement('div');
  bodyEl.className = 'error-body';
  if (body.startsWith('{') || body.startsWith('[')) {
    const pre = document.createElement('pre');
    pre.textContent = body;
    bodyEl.appendChild(pre);
  } else {
    bodyEl.textContent = body;
  }
  block.appendChild(bodyEl);
  parent.appendChild(block);
}

function drawTrends(history, nodes) {
  const minutes = chosenMinutes();

  // Check if any trends chart is being hovered
  const trendsCharts = document.querySelectorAll('.trends .chart');
  let hoverActive = false;
  trendsCharts.forEach(chart => {
    if (chart.matches(':hover')) hoverActive = true;
  });
  if (hoverActive) return;

  const now = history.now || Date.now() / 1000;
  const darkLux = history.dark_lux || 100;
  const staleAfter = history.stale_after_s || 30;

  // Node 0: Nest - light and distance
  const node0Data = history.nodes && history.nodes['0'] ? history.nodes['0'] : [];
  const lightPoints = node0Data
    .filter(d => d.data && d.data.lux !== undefined)
    .map(d => ({ time: d.recorded_at, value: d.data.lux, label: 'Light' }));
  const distancePoints = node0Data
    .filter(d => d.data && d.data.distance_cm !== undefined)
    .map(d => ({ time: d.recorded_at, value: d.data.distance_cm, label: 'Distance' }));

  const lightWrapper = document.getElementById('chart-light');
  if (lightWrapper) {
    window.drawLineChart(lightWrapper, lightPoints, {
      now,
      minutes,
      threshold: darkLux,
      thresholdLabel: 'dark below 100 lux',
      unit: 'lux',
      label: 'Light',
      ariaLabel: `Light: latest ${lightPoints.length > 0 ? lightPoints[lightPoints.length - 1].value.toFixed(1) : 'N/A'} lux`
    });
  }

  const distanceWrapper = document.getElementById('chart-distance');
  if (distanceWrapper) {
    window.drawLineChart(distanceWrapper, distancePoints, {
      now,
      minutes,
      unit: 'cm',
      label: 'Distance',
      ariaLabel: `Distance: latest ${distancePoints.length > 0 ? distancePoints[distancePoints.length - 1].value.toFixed(1) : 'N/A'} cm`
    });
  }

  // Timeline - messages received
  const timelineWrapper = document.getElementById('chart-timeline');
  if (timelineWrapper) {
    const nodeData = {};
    [0, 1, 2].forEach(nodeId => {
      const nodeHistory = history.nodes && history.nodes[String(nodeId)] ? history.nodes[String(nodeId)] : [];
      const events = nodeHistory
        .filter(d => d.event_type)
        .map(d => ({ recorded_at: d.recorded_at }));
      const node = nodes.find(n => n.node_id === nodeId);
      const counts = node ? node.counts : { added: 0, duplicate: 0 };
      nodeData[nodeId] = { events, counts };
    });
    window.drawTimeline(timelineWrapper, nodeData, { now, minutes, staleAfter });
  }

  // Activity strips and pressure charts for dolls
  [1, 2].forEach(nodeId => {
    const nodeHistory = history.nodes && history.nodes[String(nodeId)] ? history.nodes[String(nodeId)] : [];

    // Activity strip
    const activityEvents = nodeHistory
      .filter(d => d.activity !== null && d.activity !== undefined)
      .map(d => ({ recorded_at: d.recorded_at, activity: d.activity }));

    const activityWrapper = document.getElementById(`chart-activity-${nodeId}`);
    if (activityWrapper) {
      window.drawActivityStrip(activityWrapper, activityEvents, { now, minutes });
    }

    // Pressure chart
    const pressurePoints = nodeHistory
      .filter(d => d.data && d.data.fsr !== undefined)
      .map(d => ({ time: d.recorded_at, value: d.data.fsr, label: 'Pressure' }));

    const pressureWrapper = document.getElementById(`chart-pressure-${nodeId}`);
    if (pressureWrapper) {
      window.drawPressureChart(pressureWrapper, pressurePoints, {
        now,
        minutes,
        unit: 'fsr',
        label: 'Pressure',
        ariaLabel: `Pressure: latest ${pressurePoints.length > 0 ? pressurePoints[pressurePoints.length - 1].value.toFixed(0) : 'N/A'}`
      });
    }
  });
}

async function poll() {
  clearTimeout(pollTimer);
  try {
    const [nodes, events, errors, history] = await Promise.all([
      getJson('/api/dashboard/nodes'),
      getJson('/api/dashboard/events'),
      getJson('/api/dashboard/errors'),
      getJson(`/api/dashboard/history?minutes=${chosenMinutes()}`)
    ]);
    nodes.forEach(drawNode);
    drawEvents(events);
    drawErrors(errors);
    drawTrends(history, nodes);
    setConnected(true);
  } catch (error) {
    console.warn('dashboard poll failed:', error);
    setConnected(false);
  }
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, POLL_MS);
}

// Range change handler
document.addEventListener('change', e => {
  if (e.target.name === 'range') {
    poll();
  }
});

// Start polling
document.addEventListener('DOMContentLoaded', () => {
  poll();
});