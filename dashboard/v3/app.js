// Ask the laptop server for the latest state every second and draw it.
// All text goes in with textContent, so data from the network cannot add HTML.
// Chart drawing lives in charts.js.

const POLL_MS = 1000;

const NODE_NAMES = {
  0: "Nest",
  1: "Doll 1",
  2: "Doll 2",
  3: "Doll 3",
};

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

// These values are shown as gauges on the cards, not in the value list.
const GAUGE_KEYS = ["lux", "distance_cm", "fsr"];

// What is drawn now, so lists are only rebuilt when something new arrives.
let drawnEventsKey = "";
let drawnErrorsKey = "";

function clockTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString();
}

function showValue(value) {
  if (value === null) {
    return "no reading";
  }
  return String(value);
}

// Known fields first, in the order of LABELS, then anything new.
function orderedEntries(values) {
  const known = Object.keys(LABELS).filter((key) => key in values);
  const others = Object.keys(values).filter((key) => !(key in LABELS));
  return [...known, ...others].map((key) => [key, values[key]]);
}

// Rebuilding a list closes any open <details>. Remember which were open.
function openKeys(container) {
  const keys = new Set();
  for (const item of container.querySelectorAll("details[open]")) {
    keys.add(item.dataset.key);
  }
  return keys;
}

function reopen(container, keys) {
  for (const item of container.querySelectorAll("details")) {
    if (keys.has(item.dataset.key)) {
      item.open = true;
    }
  }
}

function makeDetails(key, summaryText, blocks) {
  const details = document.createElement("details");
  details.dataset.key = key;
  const summary = document.createElement("summary");
  summary.textContent = summaryText;
  details.append(summary);

  for (const block of blocks) {
    const title = document.createElement("p");
    title.className = "detail-title";
    title.textContent = block.title;
    details.append(title);

    const body = document.createElement(block.pre ? "pre" : "p");
    body.textContent = block.text;
    details.append(body);
  }
  return details;
}

function drawNode(node) {
  const card = document.querySelector(`.card[data-node="${node.node_id}"]`);
  if (card === null) {
    return;
  }

  const name = NODE_NAMES[node.node_id];
  const badge = card.querySelector(".badge");
  const warning = card.querySelector(".warning");
  const values = card.querySelector(".values");
  const seen = card.querySelector(".seen");
  const activity = card.querySelector(".activity");
  const lowLight = card.querySelector(".low-light");

  badge.textContent = node.status.toUpperCase();
  badge.className = `badge ${node.status}`;
  card.classList.toggle("is-stale", node.status === "stale");
  card.classList.toggle("is-offline", node.status === "offline");

  if (node.status === "stale") {
    warning.textContent = `${name} — STALE. No update for ${Math.round(node.age_s)} seconds.`;
  } else if (node.status === "offline") {
    warning.textContent = `${name} — OFFLINE. Check power and Wi-Fi.`;
  } else {
    warning.textContent = "";
  }

  if (lowLight !== null) {
    lowLight.hidden = node.low_light !== true;
  }

  if (node.status === "unknown") {
    values.replaceChildren();
    seen.textContent = "No data yet";
    if (activity !== null) {
      activity.textContent = "";
    }
    return;
  }

  if (activity !== null) {
    activity.textContent = ACTIVITY_TEXT[node.activity] || "";
  }

  const rows = [];
  for (const [key, value] of orderedEntries(node.values)) {
    if (GAUGE_KEYS.includes(key)) {
      continue;
    }
    const row = document.createElement("div");
    const label = document.createElement("dt");
    const text = document.createElement("dd");
    label.textContent = LABELS[key] || key;
    text.textContent = showValue(value);
    row.append(label, text);
    rows.push(row);
  }
  values.replaceChildren(...rows);

  seen.textContent = `Last update ${Math.round(node.age_s)} s ago (${clockTime(node.last_seen)})`;
}

function eventSummary(event) {
  const parts = [];
  for (const [key, value] of orderedEntries(event.data)) {
    parts.push(`${LABELS[key] || key}: ${showValue(value)}`);
  }
  if (event.activity) {
    parts.unshift(ACTIVITY_TEXT[event.activity] || event.activity);
  }
  return parts.join(", ");
}

function drawEvents(events) {
  const key = events.length > 0 ? events[0].event_id : "";
  if (key === drawnEventsKey) {
    return;
  }
  drawnEventsKey = key;

  const body = document.getElementById("event-rows");
  const wasOpen = openKeys(body);
  const rows = [];

  for (const event of events) {
    const row = document.createElement("tr");
    // Delay = laptop receive time - recorded time. Raw data for T23 latency.
    const delay = event.received_at - event.recorded_at;
    const cells = [
      clockTime(event.recorded_at),
      `${NODE_NAMES[event.node_id] || `Node ${event.node_id}`} · ${event.event_type}`,
      eventSummary(event),
      `${delay.toFixed(1)} s`,
    ];
    for (const text of cells) {
      const cell = document.createElement("td");
      cell.textContent = text;
      row.append(cell);
    }

    const detailCell = document.createElement("td");
    const copy = { ...event };
    delete copy.received_at;
    detailCell.append(makeDetails(event.event_id, "JSON", [
      { title: `Received ${clockTime(event.received_at)}`, text: JSON.stringify(copy, null, 2), pre: true },
    ]));
    row.append(detailCell);
    rows.push(row);
  }

  body.replaceChildren(...rows);
  reopen(body, wasOpen);
}

function drawErrors(result) {
  document.getElementById("rejected-total").textContent = String(result.rejected_total);
  document.getElementById("dev-empty").hidden = result.errors.length > 0;

  const key = result.errors.length > 0 ? String(result.errors[0].id) : "";
  if (key === drawnErrorsKey) {
    return;
  }
  drawnErrorsKey = key;

  const list = document.getElementById("error-list");
  const wasOpen = openKeys(list);
  const items = [];

  for (const error of result.errors) {
    let raw = error.raw;
    if (raw === "") {
      raw = "(body not kept)";
    }
    items.push(makeDetails(String(error.id), `${clockTime(error.time)} · from ${error.source} · ${error.reason}`, [
      { title: "What is wrong", text: error.reason },
      { title: "How to fix", text: error.hint },
      { title: "What the server received", text: raw, pre: true },
      { title: "A correct packet looks like", text: JSON.stringify(error.example, null, 2), pre: true },
    ]));
  }

  list.replaceChildren(...items);
  reopen(list, wasOpen);
}

function chosenMinutes() {
  return document.querySelector('input[name="range"]:checked').value;
}

function valuePoints(points, key) {
  return points.map((point) => {
    let value = point.data[key];
    if (value === undefined) {
      value = null;
    }
    return { t: point.t, value: value };
  });
}

function drawTrends(history, nodes) {
  // Redrawing removes the tooltip, so wait while the mouse is on a chart.
  if (document.querySelector(".chart:hover") !== null) {
    return;
  }

  const start = history.now - history.minutes * 60;
  const end = history.now;
  const nest = history.nodes["0"] || [];

  drawLineChart(document.getElementById("chart-light"), valuePoints(nest, "lux"), {
    start: start,
    end: end,
    name: "Light",
    unit: "lux",
    threshold: history.dark_lux,
    thresholdError: history.dark_lux_error,
    thresholdLabel: `dark below ${history.dark_lux} ± ${history.dark_lux_error} lux`,
  });

  drawLineChart(document.getElementById("chart-distance"), valuePoints(nest, "distance_cm"), {
    start: start,
    end: end,
    name: "Distance",
    unit: "cm",
  });

  const rows = [];
  for (const nodeId of ["0", "1", "2"]) {
    const points = history.nodes[nodeId] || [];
    rows.push({ label: NODE_NAMES[nodeId], times: points.map((point) => point.t) });
  }
  drawTimeline(document.getElementById("chart-timeline"), rows, {
    start: start,
    end: end,
    staleAfter: history.stale_after_s,
  });

  const counts = [];
  for (const node of nodes) {
    const item = document.createElement("li");
    item.textContent = `${NODE_NAMES[node.node_id]}: ${node.counts.added} received, ${node.counts.duplicate} duplicate`;
    counts.push(item);
  }
  document.getElementById("counts").replaceChildren(...counts);

  for (const nodeId of ["1", "2"]) {
    const points = history.nodes[nodeId] || [];
    drawActivityStrip(document.getElementById(`chart-activity-${nodeId}`), points, {
      start: start,
      end: end,
      names: ACTIVITY_TEXT,
    });
    drawLineChart(document.getElementById(`chart-pressure-${nodeId}`), valuePoints(points, "fsr"), {
      start: start,
      end: end,
      name: "Pressure",
      unit: "",
    });
  }
}

// Gauge zones come from the server config, so they match DARK_LUX and friends.
function gaugeZones(key, history) {
  if (key === "lux") {
    const error = history.dark_lux_error;
    return [
      { upTo: history.dark_lux - error, level: "bad", text: "Too dark" },
      { upTo: history.dark_lux + error, level: "warn", text: "Almost dark" },
      { upTo: history.bright_lux - error, level: "good", text: "Good light" },
      { upTo: history.bright_lux + error, level: "warn", text: "Almost too bright" },
      { upTo: history.bright_lux * 1.5, level: "bad", text: "Too bright" },
    ];
  }
  if (key === "distance_cm") {
    const margin = history.teacher_margin_cm;
    return [
      { upTo: history.teacher_near_cm - margin, level: "good", text: "Teacher near" },
      { upTo: history.teacher_near_cm + margin, level: "warn", text: "Teacher at the edge" },
      // 400 cm is the longest the HC-SR04 can measure.
      { upTo: 400, level: "bad", text: "Teacher far" },
    ];
  }
  const margin = history.fsr_margin;
  return [
    { upTo: history.fsr_active - margin, level: "off", text: "No book" },
    { upTo: history.fsr_active + margin, level: "warn", text: "Light press" },
    // 4095 is the top of the ESP32 12-bit ADC.
    { upTo: 4095, level: "good", text: "Book on the mat" },
  ];
}

const GAUGE_NAMES = {
  lux: { name: "Light", label: "Light (lux)", unit: "lux" },
  distance_cm: { name: "Teacher distance", label: "Teacher distance (cm)", unit: "cm" },
  fsr: { name: "Pressure", label: "Pressure (book on the mat)", unit: "" },
};

function drawGauges(nodes, history) {
  for (const node of nodes) {
    for (const wrapper of document.querySelectorAll(`.card[data-node="${node.node_id}"] .gauge`)) {
      const key = wrapper.dataset.key;
      let value = null;
      if (node.status !== "unknown" && node.values[key] !== undefined) {
        value = node.values[key];
      }
      drawGauge(wrapper, value, gaugeZones(key, history), GAUGE_NAMES[key]);
    }
  }
}

function setConnected(connected) {
  const connection = document.getElementById("connection");
  document.body.classList.toggle("disconnected", !connected);
  connection.classList.toggle("down", !connected);

  if (connected) {
    connection.textContent = "Server connected";
    return;
  }

  connection.textContent = "Server disconnected — retrying";
  // Without the server we do not know any node's status, so never show ONLINE.
  for (const badge of document.querySelectorAll(".badge")) {
    badge.textContent = "NO SERVER";
    badge.className = "badge";
  }
}

async function getJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${path} returned ${response.status}`);
  }
  return response.json();
}

let pollTimer = null;

async function poll() {
  clearTimeout(pollTimer);
  try {
    const [nodes, events, errors, history] = await Promise.all([
      getJson("/api/dashboard/nodes"),
      getJson("/api/dashboard/events"),
      getJson("/api/dashboard/errors"),
      getJson(`/api/dashboard/history?minutes=${chosenMinutes()}`),
    ]);
    nodes.forEach(drawNode);
    drawEvents(events);
    drawErrors(errors);
    drawTrends(history, nodes);
    drawGauges(nodes, history);
    setConnected(true);
  } catch (error) {
    console.warn("dashboard poll failed:", error);
    setConnected(false);
  }
  // Clear again: a range change may have started a second poll meanwhile.
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, POLL_MS);
}

// Redraw at once when the time range changes.
for (const input of document.querySelectorAll('input[name="range"]')) {
  input.addEventListener("change", poll);
}

poll();
