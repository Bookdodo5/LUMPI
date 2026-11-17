// Polls the laptop server every second and draws the page. Chart drawing lives in charts.js.

const POLL_MS = 1000;
const SHOWN_NODES = [0, 1, 2];

// Human-readable text lives in these tables, so translating or renaming means editing one table.
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

let pollTimer = null;
let drawnEventsKey = null;
let drawnErrorsKey = null;

function byId(id) {
  return document.getElementById(id);
}

function labelOf(table, key) {
  return Object.hasOwn(table, key) ? table[key] : String(key);
}

function clockTime(seconds) {
  return new Date(seconds * 1000).toLocaleTimeString();
}

function valueText(value) {
  return value === null || value === undefined ? "no reading" : String(value);
}

async function getJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${path} returned ${response.status}`);
  }
  return response.json();
}

function chosenMinutes() {
  return Number(document.querySelector('input[name="range"]:checked').value);
}

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
    setConnected(true);
  } catch (error) {
    console.warn("dashboard poll failed:", error);
    setConnected(false);
  }
  // Clear again: a range change may have started a second poll meanwhile.
  clearTimeout(pollTimer);
  pollTimer = setTimeout(poll, POLL_MS);
}

function setConnected(ok) {
  const connection = byId("connection");
  document.body.classList.toggle("disconnected", !ok);
  if (ok) {
    connection.textContent = "Server connected";
    connection.className = "connection";
    return;
  }
  connection.textContent = "Server disconnected — retrying";
  connection.className = "connection bad";
  // Without the server we do not know any status, so never keep showing ONLINE.
  document.querySelectorAll(".badge").forEach((badge) => {
    badge.textContent = "NO SERVER";
    badge.className = "badge";
  });
}

function warningText(name, node) {
  if (node.status === "stale") {
    return `${name} — STALE. No update for ${Math.round(node.age_s)} seconds.`;
  }
  if (node.status === "offline") {
    return `${name} — OFFLINE. Check power and Wi-Fi.`;
  }
  return "";
}

function drawNode(node) {
  const card = document.querySelector(`.card[data-node="${node.node_id}"]`);
  if (!card) return;  // node 3 has no card

  const badge = card.querySelector(".badge");
  badge.textContent = node.status.toUpperCase();
  badge.className = `badge ${node.status}`;
  card.classList.toggle("is-stale", node.status === "stale");
  card.classList.toggle("is-offline", node.status === "offline");
  card.querySelector(".warning").textContent = warningText(NODE_NAMES[node.node_id], node);

  if (card.classList.contains("nest")) {
    card.querySelector(".low-light").hidden = node.low_light !== true;
  }

  const activity = card.querySelector(".activity");
  const values = card.querySelector(".values");
  const seen = card.querySelector(".seen");

  if (node.status === "unknown") {
    values.replaceChildren();
    seen.textContent = "No data yet";
    if (activity) activity.textContent = "";
    return;
  }

  if (activity) {
    activity.textContent = node.activity === null ? "" : labelOf(ACTIVITY_TEXT, node.activity);
  }
  drawValues(values, node.values);
  seen.textContent = `Last update ${Math.round(node.age_s)} s ago (${clockTime(node.last_seen)})`;
}

function drawValues(container, values) {
  const keys = Object.keys(LABELS).filter((key) => Object.hasOwn(values, key));
  const unknown = Object.keys(values).filter((key) => !Object.hasOwn(LABELS, key));
  const rows = [...keys, ...unknown].map((key) => {
    const item = document.createElement("div");
    const term = document.createElement("dt");
    term.textContent = labelOf(LABELS, key);
    const definition = document.createElement("dd");
    definition.textContent = valueText(values[key]);
    item.append(term, definition);
    return item;
  });
  container.replaceChildren(...rows);
}

function seriesFor(points, key) {
  return points.map((p) => ({
    t: p.t,
    value: p.data && typeof p.data[key] === "number" ? p.data[key] : null,
  }));
}

function drawTrends(history, nodes) {
  // Redrawing would remove a tooltip under the mouse, so wait until the mouse leaves.
  if (document.querySelector(".trends .chart:hover")) return;

  const minutes = history.minutes;
  const timeWindow = { start: history.now - minutes * 60, end: history.now, minutes: minutes };
  const nest = history.nodes["0"] || [];

  drawLineChart(byId("light-body"), seriesFor(nest, "lux"), {
    ...timeWindow,
    label: "Light",
    unit: "lux",
    threshold: history.dark_lux,
    thresholdLabel: `dark below ${history.dark_lux} lux`,
  });
  drawLineChart(byId("distance-body"), seriesFor(nest, "distance_cm"), {
    ...timeWindow,
    label: "Teacher distance",
    unit: "cm",
  });

  const rows = SHOWN_NODES.map((nodeId) => ({
    name: NODE_NAMES[nodeId],
    times: (history.nodes[String(nodeId)] || []).map((p) => p.t),
  }));
  drawTimeline(byId("timeline-body"), rows, {
    ...timeWindow,
    now: history.now,
    staleAfter: history.stale_after_s,
  });
  byId("counters").textContent = nodes
    .filter((node) => SHOWN_NODES.includes(node.node_id))
    .map((node) => `${NODE_NAMES[node.node_id]}: ${node.counts.added} received, ${node.counts.duplicate} duplicate`)
    .join("   ");

  for (const dollId of [1, 2]) {
    const points = history.nodes[String(dollId)] || [];
    drawActivityStrip(byId(`doll${dollId}-activity`), points.map((p) => ({ t: p.t, activity: p.activity })), {
      ...timeWindow,
      labelFor: (activity) => labelOf(ACTIVITY_TEXT, activity),
    });
    drawLineChart(byId(`doll${dollId}-pressure`), seriesFor(points, "fsr"), {
      ...timeWindow,
      label: `Doll ${dollId} pressure`,
      unit: "",
    });
  }
}

function eventValueText(event) {
  const parts = [];
  if (event.activity) {
    parts.push(labelOf(ACTIVITY_TEXT, event.activity));
  }
  for (const key of Object.keys(LABELS)) {
    if (Object.hasOwn(event.data, key)) {
      parts.push(`${LABELS[key]}: ${valueText(event.data[key])}`);
    }
  }
  return parts.join(", ");
}

function cell(text) {
  const td = document.createElement("td");
  td.textContent = text;
  return td;
}

function detailsCell(event) {
  const td = document.createElement("td");
  td.className = "details";
  const details = document.createElement("details");
  details.dataset.key = event.event_id;
  const summary = document.createElement("summary");
  summary.textContent = "JSON";
  const received = document.createElement("p");
  received.className = "small";
  received.textContent = `Received ${clockTime(event.received_at)}`;
  const shown = { ...event };
  delete shown.received_at;
  const pre = document.createElement("pre");
  pre.textContent = JSON.stringify(shown, null, 2);
  details.append(summary, received, pre);
  td.append(details);
  return td;
}

function eventRow(event) {
  const row = document.createElement("tr");
  row.append(
    cell(clockTime(event.recorded_at)),
    cell(NODE_NAMES[event.node_id] || `Node ${event.node_id}`),
    cell(event.event_type),
    cell(eventValueText(event)),
    cell(`${(event.received_at - event.recorded_at).toFixed(1)} s`),
    detailsCell(event),
  );
  return row;
}

// Rebuilding a list closes open <details>, so remember which are open and reopen them.
function openKeysIn(container) {
  return [...container.querySelectorAll("details[data-key]")]
    .filter((details) => details.open)
    .map((details) => details.dataset.key);
}

function reopenIn(container, keys) {
  container.querySelectorAll("details[data-key]").forEach((details) => {
    if (keys.includes(details.dataset.key)) details.open = true;
  });
}

function drawEvents(events) {
  const key = events.length ? events[0].event_id : "";
  if (key === drawnEventsKey) return;
  const body = byId("events-body");
  const keys = openKeysIn(body);
  body.replaceChildren(...events.map(eventRow));
  reopenIn(body, keys);
  drawnEventsKey = key;
}

function errorBlock(title, text, tag) {
  const wrap = document.createElement("div");
  const heading = document.createElement("h3");
  heading.textContent = title;
  const body = document.createElement(tag);
  body.textContent = text;
  wrap.append(heading, body);
  return wrap;
}

function errorCard(error) {
  const card = document.createElement("details");
  card.className = "error";
  card.dataset.key = String(error.id);
  const summary = document.createElement("summary");
  summary.textContent = `${clockTime(error.time)} · from ${error.source} · ${error.reason}`;
  card.append(
    summary,
    errorBlock("What is wrong", error.reason, "p"),
    errorBlock("How to fix", error.hint, "p"),
    errorBlock("What the server received", error.raw || "(body not kept)", "pre"),
    errorBlock("A correct packet looks like", JSON.stringify(error.example, null, 2), "pre"),
  );
  return card;
}

function drawErrors(data) {
  byId("rejected-total").textContent = String(data.rejected_total);
  byId("dev-empty").hidden = data.errors.length > 0;
  const key = data.errors.length ? String(data.errors[0].id) : "";
  if (key === drawnErrorsKey) return;
  const list = byId("error-list");
  const keys = openKeysIn(list);
  list.replaceChildren(...data.errors.map(errorCard));
  reopenIn(list, keys);
  drawnErrorsKey = key;
}

document.querySelectorAll('input[name="range"]').forEach((radio) => {
  radio.addEventListener("change", () => poll());
});

poll();
