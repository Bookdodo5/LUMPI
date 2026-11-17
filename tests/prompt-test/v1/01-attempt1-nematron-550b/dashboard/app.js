// LUMPI Teacher Dashboard - Main application

const POLL_MS = 1000;
let pollTimer = null;
let drawnEventsKey = null;
let drawnErrorsKey = null;

// Mappings
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

// Start polling
function start() {
  poll();
}

// Fetch JSON with no-store cache
async function getJson(path) {
  const resp = await fetch(path, { cache: "no-store" });
  if (!resp.ok) {
    throw new Error(`HTTP ${resp.status}`);
  }
  return resp.json();
}

// Main poll loop
async function poll() {
  clearTimeout(pollTimer);
  try {
    const chosenMinutes = getChosenMinutes();
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

// Set connection status in header
function setConnected(ok) {
  const conn = document.getElementById("connection");
  const body = document.body;
  if (ok) {
    conn.textContent = "Server connected";
    conn.classList.remove("disconnected");
    body.classList.remove("disconnected");
  } else {
    conn.textContent = "Server disconnected — retrying";
    conn.classList.add("disconnected");
    body.classList.add("disconnected");
  }
}

// Draw a single node card
function drawNode(node) {
  const card = document.querySelector(`.card[data-node="${node.node_id}"]`);
  if (!card) return; // Node 3 has no card

  const badge = card.querySelector(".badge");
  const warning = card.querySelector(".warning");
  const lowLight = card.querySelector(".low-light");
  const activity = card.querySelector(".activity");
  const values = card.querySelector(".values");
  const seen = card.querySelector(".seen");

  // Status badge
  const status = node.status;
  badge.textContent = status.toUpperCase();
  badge.className = "badge " + status;

  // Card border classes
  card.classList.remove("is-stale", "is-offline");
  if (status === "stale") card.classList.add("is-stale");
  if (status === "offline") card.classList.add("is-offline");

  // Warning text
  const name = NODE_NAMES[node.node_id] || `Node ${node.node_id}`;
  if (status === "stale") {
    warning.textContent = `${name} — STALE. No update for ${Math.round(node.age_s)} seconds.`;
  } else if (status === "offline") {
    warning.textContent = `${name} — OFFLINE. Check power and Wi-Fi.`;
  } else {
    warning.textContent = "";
  }

  // Nest low light
  if (node.node_id === 0) {
    lowLight.hidden = node.low_light !== true;
  }

  // Unknown: clear values and stop
  if (status === "unknown") {
    values.innerHTML = "";
    activity.textContent = "";
    seen.textContent = "No data yet";
    return;
  }

  // Activity (dolls only)
  if (node.node_id > 0) {
    activity.textContent = ACTIVITY_TEXT[node.activity] || (node.activity || "");
  }

  // Values
  values.innerHTML = "";
  const knownKeys = Object.keys(LABELS);
  const seenKeys = new Set();

  // Known keys first, in LABELS order
  for (const key of knownKeys) {
    if (key in node.values) {
      seenKeys.add(key);
      addValueRow(values, LABELS[key], node.values[key]);
    }
  }
  // Unknown keys after
  for (const [key, val] of Object.entries(node.values)) {
    if (!seenKeys.has(key)) {
      addValueRow(values, key, val);
    }
  }

  // Last seen
  const age = Math.round(node.age_s);
  const clock = new Date(node.last_seen * 1000).toLocaleTimeString();
  seen.textContent = `Last update ${age} s ago (${clock})`;
}

// Add a value row to the values list
function addValueRow(container, label, value) {
  const div = document.createElement("div");
  const dt = document.createElement("dt");
  const dd = document.createElement("dd");
  dt.textContent = label;
  if (value === null || value === undefined) {
    dd.textContent = "no reading";
  } else if (typeof value === "number" && Number.isInteger(value)) {
    dd.textContent = value.toString();
  } else if (typeof value === "number") {
    dd.textContent = value.toFixed(1);
  } else {
    dd.textContent = value;
  }
  div.appendChild(dt);
  div.appendChild(dd);
  container.appendChild(div);
}

// Draw recent events table
function drawEvents(events) {
  const tbody = document.getElementById("events-body");
  if (!tbody) return;

  // Check if we need to rebuild (newest event_id changed)
  const newKey = events.length > 0 ? events[0].event?.event_id : null;
  if (newKey === drawnEventsKey) return;
  drawnEventsKey = newKey;

  // Collect open details keys before rebuild
  const openKeys = [];
  for (const detail of tbody.querySelectorAll("details[open]")) {
    openKeys.push(detail.dataset.key);
  }

  tbody.innerHTML = "";

  for (const entry of events) {
    const evt = entry.event;
    const receivedAt = entry.received_at;
    const recordedAt = evt.recorded_at;

    const tr = document.createElement("tr");

    // Time
    const tdTime = document.createElement("td");
    tdTime.textContent = new Date(recordedAt * 1000).toLocaleTimeString();
    tr.appendChild(tdTime);

    // Node
    const tdNode = document.createElement("td");
    tdNode.textContent = NODE_NAMES[evt.node_id] || `Node ${evt.node_id}`;
    tr.appendChild(tdNode);

    // Type
    const tdType = document.createElement("td");
    tdType.textContent = evt.event_type;
    tr.appendChild(tdType);

    // Value
    const tdValue = document.createElement("td");
    tdValue.className = "event-value";
    const activityDiv = document.createElement("div");
    activityDiv.className = "event-activity";
    activityDiv.textContent = ACTIVITY_TEXT[evt.activity] || (evt.activity || "");
    tdValue.appendChild(activityDiv);

    const detailDiv = document.createElement("div");
    detailDiv.className = "event-detail";
    const parts = [];
    for (const key of Object.keys(LABELS)) {
      if (key in evt.data) {
        const val = evt.data[key];
        const label = LABELS[key];
        if (val === null) {
          parts.push(`${label}: no reading`);
        } else if (typeof val === "number" && Number.isInteger(val)) {
          parts.push(`${label}: ${val}`);
        } else if (typeof val === "number") {
          parts.push(`${label}: ${val.toFixed(1)}`);
        } else {
          parts.push(`${label}: ${val}`);
        }
      }
    }
    detailDiv.textContent = parts.join(", ");
    tdValue.appendChild(detailDiv);
    tr.appendChild(tdValue);

    // Delay
    const tdDelay = document.createElement("td");
    const delay = receivedAt - recordedAt;
    tdDelay.textContent = delay.toFixed(1) + " s";
    tr.appendChild(tdDelay);

    // Details
    const tdDetails = document.createElement("td");
    const details = document.createElement("details");
    details.dataset.key = evt.event_id;
    if (openKeys.includes(evt.event_id)) {
      details.open = true;
    }
    const summary = document.createElement("summary");
    summary.textContent = "JSON";
    details.appendChild(summary);

    const receivedDiv = document.createElement("div");
    receivedDiv.className = "event-received";
    receivedDiv.textContent = `Received ${new Date(receivedAt * 1000).toLocaleTimeString()}`;
    details.appendChild(receivedDiv);

    const pre = document.createElement("pre");
    // Event without received_at
    const evtCopy = { ...evt };
    pre.textContent = JSON.stringify(evtCopy, null, 2);
    details.appendChild(pre);

    tdDetails.appendChild(details);
    tr.appendChild(tdDetails);

    tbody.appendChild(tr);
  }
}

// Draw developer panel errors
function drawErrors(errorsData) {
  const errorList = document.getElementById("error-list");
  const devEmpty = document.getElementById("dev-empty");
  const rejectedTotal = document.getElementById("rejected-total");
  if (!errorList) return;

  // Update total count
  rejectedTotal.textContent = errorsData.rejected_total || 0;

  // Check if we need to rebuild (newest error id changed)
  const newKey = errorsData.errors.length > 0 ? errorsData.errors[0].id : null;
  if (newKey === drawnErrorsKey) return;
  drawnErrorsKey = newKey;

  // Collect open details keys
  const openKeys = [];
  for (const detail of errorList.querySelectorAll("details[open]")) {
    openKeys.push(detail.dataset.key);
  }

  errorList.innerHTML = "";

  if (errorsData.errors.length === 0) {
    devEmpty.style.display = "block";
    return;
  }

  devEmpty.style.display = "none";

  for (const err of errorsData.errors) {
    const detail = document.createElement("details");
    detail.className = "error-card";
    detail.dataset.key = err.id;
    if (openKeys.includes(String(err.id))) {
      detail.open = true;
    }

    const summary = document.createElement("summary");
    const timeStr = new Date(err.time * 1000).toLocaleTimeString();
    summary.textContent = `${timeStr} · from ${err.source} · ${err.reason}`;
    detail.appendChild(summary);

    // Block 1: What is wrong
    addErrorBlock(detail, "What is wrong", err.reason);

    // Block 2: How to fix
    addErrorBlock(detail, "How to fix", err.hint);

    // Block 3: What the server received
    const rawTitle = document.createElement("div");
    rawTitle.className = "error-block-title";
    rawTitle.textContent = "What the server received";
    detail.appendChild(rawTitle);
    const rawBody = document.createElement("div");
    rawBody.className = "error-block-body";
    if (err.raw && err.raw !== "") {
      const pre = document.createElement("pre");
      pre.textContent = err.raw;
      rawBody.appendChild(pre);
    } else {
      rawBody.textContent = "(body not kept)";
    }
    detail.appendChild(rawBody);

    // Block 4: A correct packet looks like
    addErrorBlock(detail, "A correct packet looks like", JSON.stringify(err.example, null, 2), true);

    errorList.appendChild(detail);
  }
}

function addErrorBlock(parent, title, content, isPre = false) {
  const titleDiv = document.createElement("div");
  titleDiv.className = "error-block-title";
  titleDiv.textContent = title;
  parent.appendChild(titleDiv);

  const bodyDiv = document.createElement("div");
  bodyDiv.className = "error-block-body";
  if (isPre) {
    const pre = document.createElement("pre");
    pre.textContent = content;
    bodyDiv.appendChild(pre);
  } else {
    bodyDiv.textContent = content;
  }
  parent.appendChild(bodyDiv);
}

// Get chosen minutes from radio buttons
function getChosenMinutes() {
  const checked = document.querySelector('input[name="range"]:checked');
  return checked ? parseInt(checked.value, 10) : 15;
}

// Range change handler
document.addEventListener("change", (e) => {
  if (e.target.matches('input[name="range"]')) {
    poll(); // Immediate poll on range change
  }
});

// Start when DOM ready
document.addEventListener("DOMContentLoaded", start);