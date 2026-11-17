/* LUMPI Teacher Dashboard - Main Application Logic */
/* Polling, drawing cards/events/errors, calling chart functions */

// ===================== CONSTANTS (top of file) =====================
const POLL_MS = 1000;
const STALE_AFTER_S = 30;
const OFFLINE_AFTER_S = 60;

// Debug: mark that app.js is loading
console.log('[app.js] Loading...');

const NODE_NAMES = { 0: "Nest", 1: "Doll 1", 2: "Doll 2", 3: "Doll 3" };

const LABELS = {
  lux: "Light (lux)",
  distance_cm: "Teacher distance (cm)",
  uid: "Book UID",
  passage: "Passage",
  attempt: "Attempt",
  fsr: "Pressure",
};

const ACTIVITY_TEXT = {
  idle: "Idle",
  book_on: "Book on the mat",
  recording: "Recording",
  waiting_ai: "Waiting for AI",
  feedback: "Giving feedback",
  quiet_study: "Quiet study",
  stopped: "Stopped (STOP button)",
};

// ===================== STATE =====================
let pollTimer = null;
let chosenMinutes = 15;
let drawnEventsKey = null;
let drawnErrorsKey = null;
let isHoveringTrends = false;

// ===================== UTILITIES =====================
function getJson(path) {
  return fetch(path, { cache: "no-store" }).then(r => {
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return r.json();
  });
}

function round1(x) {
  return Math.round(x * 10) / 10;
}

function formatTime(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

function formatTimeShort(ts) {
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
}

// ===================== CONNECTION STATUS =====================
function setConnected(ok) {
  const el = document.getElementById("connection");
  if (ok) {
    el.textContent = "Server connected";
    el.classList.remove("disconnected");
    document.body.classList.remove("disconnected");
  } else {
    el.textContent = "Server disconnected — retrying";
    el.classList.add("disconnected");
    document.body.classList.add("disconnected");
  }
}

// ===================== NODE CARD DRAWING =====================
function drawNode(node) {
  const card = document.querySelector(`.card[data-node="${node.node_id}"]`);
  if (!card) return; // node 3 has no card

  const status = node.status || "unknown";
  const age = node.age_s;

  // Badge
  const badge = card.querySelector(".badge");
  badge.textContent = status.toUpperCase();
  badge.className = "badge " + status;

  // Card border states
  card.classList.remove("is-stale", "is-offline");
  if (status === "stale") card.classList.add("is-stale");
  if (status === "offline") card.classList.add("is-offline");

  // Warning text
  const warning = card.querySelector(".warning");
  const name = NODE_NAMES[node.node_id] || `Node ${node.node_id}`;
  if (status === "stale" && age !== undefined) {
    warning.textContent = `${name} — STALE. No update for ${Math.round(age)} seconds.`;
  } else if (status === "offline") {
    warning.textContent = `${name} — OFFLINE. Check power and Wi-Fi.`;
  } else {
    warning.textContent = "";
  }

  // Nest low light
  if (node.node_id === 0) {
    const lowLight = card.querySelector(".low-light");
    lowLight.hidden = node.low_light !== true;
  }

  // Unknown: clear values, show "No data yet"
  if (status === "unknown") {
    card.querySelector(".values").replaceChildren();
    card.querySelector(".activity").textContent = "";
    card.querySelector(".seen").textContent = "No data yet";
    return;
  }

  // Activity (dolls only)
  if (node.node_id >= 1) {
    const actEl = card.querySelector(".activity");
    actEl.textContent = ACTIVITY_TEXT[node.activity] || (node.activity ? String(node.activity) : "");
  }

  // Values
  const valuesEl = card.querySelector(".values");
  valuesEl.innerHTML = "";
  const knownKeys = Object.keys(LABELS);
  const seenKeys = new Set();

  // Known keys first in LABELS order
  knownKeys.forEach(key => {
    if (key in node.values) {
      seenKeys.add(key);
      const div = document.createElement("div");
      const dt = document.createElement("dt");
      dt.textContent = LABELS[key];
      const dd = document.createElement("dd");
      const val = node.values[key];
      if (val === null || val === undefined) {
        dd.textContent = "no reading";
        dd.classList.add("no-reading");
      } else {
        dd.textContent = String(val);
      }
      div.appendChild(dt);
      div.appendChild(dd);
      valuesEl.appendChild(div);
    }
  });

  // Unknown keys after
  Object.keys(node.values).forEach(key => {
    if (!seenKeys.has(key)) {
      const div = document.createElement("div");
      const dt = document.createElement("dt");
      dt.textContent = key;
      const dd = document.createElement("dd");
      const val = node.values[key];
      if (val === null || val === undefined) {
        dd.textContent = "no reading";
        dd.classList.add("no-reading");
      } else {
        dd.textContent = String(val);
      }
      div.appendChild(dt);
      div.appendChild(dd);
      valuesEl.appendChild(div);
    }
  });

  // Last update
  if (age !== undefined) {
    card.querySelector(".seen").textContent = `Last update ${round1(age)} s ago (${formatTimeShort(node.last_seen)})`;
  }
}

// ===================== EVENTS TABLE =====================
function drawEvents(events) {
  const tbody = document.getElementById("events-body");
  if (!tbody) return;

  // Check if we need to rebuild (newest event_id changed)
  const newKey = events.length > 0 ? events[0].event_id : null;
  if (newKey === drawnEventsKey) return;
  drawnEventsKey = newKey;

  // Collect open details keys
  const openKeys = new Set();
  tbody.querySelectorAll("details[open]").forEach(d => {
    openKeys.add(d.dataset.key);
  });

  tbody.innerHTML = "";
  events.forEach(ev => {
    const tr = document.createElement("tr");
    tr.dataset.key = ev.event_id;

    // Time
    const tdTime = document.createElement("td");
    tdTime.textContent = formatTime(ev.recorded_at);
    tr.appendChild(tdTime);

    // Node
    const tdNode = document.createElement("td");
    tdNode.textContent = NODE_NAMES[ev.node_id] || `Node ${ev.node_id}`;
    tr.appendChild(tdNode);

    // Type
    const tdType = document.createElement("td");
    tdType.textContent = ev.event_type;
    tr.appendChild(tdType);

    // Value
    const tdValue = document.createElement("td");
    const parts = [];
    if (ev.activity) parts.push(ACTIVITY_TEXT[ev.activity] || ev.activity);
    Object.keys(LABELS).forEach(key => {
      if (key in ev.data && ev.data[key] !== null && ev.data[key] !== undefined) {
        parts.push(`${LABELS[key]}: ${ev.data[key]}`);
      }
    });
    tdValue.textContent = parts.join(", ");
    tr.appendChild(tdValue);

    // Delay
    const tdDelay = document.createElement("td");
    const delay = (ev.received_at - ev.recorded_at).toFixed(1) + " s";
    tdDelay.textContent = delay;
    tr.appendChild(tdDelay);

    // Details
    const tdDetails = document.createElement("td");
    const details = document.createElement("details");
    details.dataset.key = ev.event_id;
    if (openKeys.has(ev.event_id)) details.open = true;

    const summary = document.createElement("summary");
    summary.textContent = "JSON";
    details.appendChild(summary);

    const pre = document.createElement("pre");
    // Copy event without received_at
    const { received_at, ...eventCopy } = ev;
    pre.textContent = JSON.stringify(eventCopy, null, 2);
    details.appendChild(pre);

    tdDetails.appendChild(details);
    tr.appendChild(tdDetails);

    tbody.appendChild(tr);
  });
}

// ===================== ERRORS PANEL =====================
function drawErrors(errorsData) {
  const list = document.getElementById("error-list");
  const empty = document.getElementById("dev-empty");
  const totalEl = document.getElementById("rejected-total");
  if (!list) return;

  const errors = errorsData.errors || [];
  const total = errorsData.rejected_total || 0;
  totalEl.textContent = total;

  // Check if we need to rebuild
  const newKey = errors.length > 0 ? errors[0].id : null;
  if (newKey === drawnErrorsKey) return;
  drawnErrorsKey = newKey;

  // Collect open details
  const openKeys = new Set();
  list.querySelectorAll("details[open]").forEach(d => {
    openKeys.add(d.dataset.key);
  });

  if (errors.length === 0) {
    list.innerHTML = "";
    empty.style.display = "block";
    return;
  }

  empty.style.display = "none";
  list.innerHTML = "";

  errors.forEach(err => {
    const details = document.createElement("details");
    details.className = "error-card";
    details.dataset.key = err.id;
    if (openKeys.has(String(err.id))) details.open = true;

    const summary = document.createElement("summary");
    const timeStr = formatTimeShort(err.time);
    summary.textContent = `${timeStr} · from ${err.source} · ${err.reason}`;
    details.appendChild(summary);

    // What is wrong
    addErrorSection(details, "What is wrong", err.reason);
    // How to fix
    addErrorSection(details, "How to fix", err.hint);
    // What the server received
    addErrorSection(details, "What the server received", err.raw || "(body not kept)", true);
    // A correct packet looks like
    addErrorSection(details, "A correct packet looks like", JSON.stringify(err.example, null, 2), true);

    list.appendChild(details);
  });
}

function addErrorSection(parent, title, body, isPre = false) {
  const section = document.createElement("div");
  section.className = "error-section";

  const titleEl = document.createElement("div");
  titleEl.className = "error-section-title";
  titleEl.textContent = title;
  section.appendChild(titleEl);

  const bodyEl = document.createElement("div");
  bodyEl.className = "error-section-body";
  if (isPre) {
    const pre = document.createElement("pre");
    pre.textContent = body;
    bodyEl.appendChild(pre);
  } else {
    bodyEl.textContent = body;
  }
  section.appendChild(bodyEl);

  parent.appendChild(section);
}

// ===================== TRENDS CHARTS =====================
function drawTrends(history, nodes) {
  if (isHoveringTrends) return;

  // Line charts
  window.drawLineChartFull("chart-lux", history["0"] || [], chosenMinutes, history.now, {
    valueKey: "lux",
    threshold: history.dark_lux,
    seriesName: "Light",
    unit: "lux",
    ariaLabel: "Light chart"
  });

  window.drawLineChartFull("chart-distance", history["0"] || [], chosenMinutes, history.now, {
    valueKey: "distance_cm",
    seriesName: "Teacher distance",
    unit: "cm",
    ariaLabel: "Distance chart"
  });

  // Timeline
  window.drawTimeline("chart-timeline", history, chosenMinutes, history.now, history.stale_after_s, NODE_NAMES);

  // Activity strips
  window.drawActivityStrip("chart-activity-1", history, chosenMinutes, history.now, 1, "Doll 1");
  window.drawActivityStrip("chart-activity-2", history, chosenMinutes, history.now, 2, "Doll 2");
}

// ===================== POLLING LOOP =====================
async function poll() {
  clearTimeout(pollTimer);
  try {
    const [nodes, events, errors, history] = await Promise.all([
      getJson("/api/dashboard/nodes"),
      getJson("/api/dashboard/events"),
      getJson("/api/dashboard/errors"),
      getJson(`/api/dashboard/history?minutes=${chosenMinutes}`),
    ]);
    nodes.forEach(drawNode);
    drawEvents(events);
    drawErrors(errors);
    drawTrends(history, nodes);
    setConnected(true);
  } catch (error) {
    console.warn("dashboard poll failed:", error);
    setConnected(false);
  }
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, POLL_MS);
}

// ===================== RANGE SELECTOR =====================
function setupRangeSelector() {
  const rangeEl = document.querySelector(".range");
  if (!rangeEl) return;

  rangeEl.addEventListener("change", e => {
    if (e.target.type === "radio") {
      chosenMinutes = parseInt(e.target.value, 10);
      poll(); // immediate poll on range change
    }
  });

  // Track hover on trends section to pause redraw
  const trendsEl = document.querySelector(".trends");
  if (trendsEl) {
    trendsEl.addEventListener("mouseenter", () => { isHoveringTrends = true; });
    trendsEl.addEventListener("mouseleave", () => { isHoveringTrends = false; });
  }
}

// ===================== INIT =====================
document.addEventListener("DOMContentLoaded", () => {
  console.log('[app.js] DOMContentLoaded fired, initializing...');
  setupRangeSelector();
  poll();
});