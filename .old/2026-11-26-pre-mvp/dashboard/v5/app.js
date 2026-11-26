// Ask the laptop server for the latest state every second and draw it.
// All text goes in with textContent, so data from the network cannot add HTML.
// Chart drawing lives in charts.js.
// Design rule: nothing is coloured while all is well. Red only means a device
// or the server has stopped reporting.

const POLL_MS = 1000;

const NODE_NAMES = {
  0: "Nest",
  1: "Doll 1",
  2: "Doll 2",
  3: "Doll 3",
};

// Card and table labels, in the order they are shown.
const LABELS = {
  lux: "Light",
  distance_cm: "Teacher distance",
  uid: "Book",
  passage: "Passage",
  attempt: "Attempt",
  fsr: "Pressure",
};

const UNITS = {
  lux: "lux",
  distance_cm: "cm",
  fsr: "/ 4095",
};

// "No book" is not a failed sensor, so it gets its own words.
const NULL_TEXT = {
  uid: "No book",
  passage: "—",
  attempt: "—",
  lux: "No reading",
  distance_cm: "No reading",
  fsr: "No reading",
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

// Rows of the activity chart, top to bottom.
const ACTIVITY_ROWS = ["feedback", "waiting_ai", "recording", "book_on", "quiet_study", "stopped", "idle"];

const ACTIVITY_SHORT = {
  feedback: "Feedback",
  waiting_ai: "Waiting for AI",
  recording: "Recording",
  book_on: "Book on",
  quiet_study: "Quiet study",
  stopped: "Stopped",
  idle: "Idle",
};

const EVENT_TEXT = {
  start: "Start",
  status: "Heartbeat",
  room: "Room",
};

// Dolls talk to the nest, so a doll hint is about range, not Wi-Fi.
const OFFLINE_HINT = {
  nest: "Check the nest has power and is on the classroom Wi\u2011Fi.",
  doll: "Check the doll is switched on and near the nest.",
};

const TEXT = {
  serverUp: "Server connected",
  serverDown: "Server not reachable · retrying",
  waiting: "Waiting for the first message",
  nothingYet: "Nothing received since the server started.",
  lastKnownValues: "Last known values",
  allWorking: "All 3 devices are working.",
  nestOffline: "Nest offline. The dolls report through the nest, so check the nest first.",
  noServer: "Can't reach the laptop server. Retrying every second.",
};

// Delay this long or more is shown in bold: the nest probably sent it again later.
const LATE_DELAY_S = 5;

// What is drawn now, so lists are only rebuilt when something new arrives.
let drawnEventsKey = "";
let drawnErrorsKey = "";
// Last good answer from the server, used while it cannot be reached.
let lastNodes = [];
let lastGoodPollTime = null;
// The live region speaks every time it is written, so only write on change.
let shownSummary = "";

const nameList = new Intl.ListFormat("en", { type: "conjunction" });

function clockTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour12: false });
}

function formatAge(seconds) {
  const whole = Math.round(seconds);
  if (whole < 60) {
    return `${whole} s`;
  }
  if (whole < 3600) {
    return `${Math.floor(whole / 60)} min`;
  }
  return `${Math.floor(whole / 3600)} h ${Math.floor((whole % 3600) / 60)} min`;
}

// Distance arrives with decimals that the sensor cannot really measure.
function formatNumber(key, value) {
  if (key === "distance_cm" && typeof value === "number") {
    return String(Math.round(value));
  }
  return String(value);
}

function showValue(key, value) {
  if (value === null) {
    return NULL_TEXT[key] || "No reading";
  }
  if (UNITS[key] !== undefined) {
    return `${formatNumber(key, value)} ${UNITS[key]}`;
  }
  return formatNumber(key, value);
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

// blocks: [{title, text, pre}]. A block without a title is a plain line.
function makeDetails(key, summaryText, summaryLabel, blocks) {
  const details = document.createElement("details");
  details.dataset.key = key;
  const summary = document.createElement("summary");
  summary.textContent = summaryText;
  if (summaryLabel !== "") {
    summary.setAttribute("aria-label", summaryLabel);
  }
  details.append(summary);

  for (const block of blocks) {
    if (block.title !== undefined) {
      const title = document.createElement("p");
      title.className = "detail-title";
      title.textContent = block.title;
      details.append(title);
    }
    const body = document.createElement(block.pre ? "pre" : "p");
    body.textContent = block.text;
    details.append(body);
  }
  return details;
}

// Badge = word + shape + fill, so it reads without colour.
function setBadge(badge, status, text) {
  const shape = document.createElement("span");
  shape.className = `shape shape-${status}`;
  shape.setAttribute("aria-hidden", "true");
  badge.className = `badge ${status}`;
  badge.replaceChildren(shape, text);
}

function makeValueRow(key, value) {
  const row = document.createElement("div");
  const label = document.createElement("dt");
  const text = document.createElement("dd");
  label.textContent = LABELS[key] || key;

  if (value === null) {
    // Words, not a number, so a failed sensor never looks like a reading.
    text.textContent = NULL_TEXT[key] || "No reading";
    text.className = "empty";
  } else {
    text.textContent = formatNumber(key, value);
    if (UNITS[key] !== undefined) {
      const unit = document.createElement("span");
      unit.className = "unit";
      unit.textContent = ` ${UNITS[key]}`;
      text.append(unit);
    }
  }
  row.append(label, text);
  return row;
}

// Every branch sets every element, so no old text stays behind.
function drawNode(node, darkLux) {
  const card = document.querySelector(`.card[data-node="${node.node_id}"]`);
  if (card === null) {
    return;
  }

  const isNest = node.node_id === 0;
  const alert = card.querySelector(".alert");
  const hint = card.querySelector(".hint");
  const headline = card.querySelector(".headline");
  const known = card.querySelector(".known");
  const values = card.querySelector(".values");
  const note = card.querySelector(".note");
  const seen = card.querySelector(".seen");

  setBadge(card.querySelector(".badge"), node.status, node.status.toUpperCase());
  card.classList.toggle("is-stale", node.status === "stale");
  card.classList.toggle("is-offline", node.status === "offline");

  alert.textContent = "";
  hint.textContent = "";
  known.textContent = "";
  note.textContent = "";
  headline.textContent = "";
  headline.className = "headline";

  if (node.status === "unknown") {
    headline.textContent = TEXT.waiting;
    headline.classList.add("waiting");
    values.replaceChildren();
    seen.textContent = TEXT.nothingYet;
    return;
  }

  const activity = ACTIVITY_TEXT[node.activity] || "";
  const clock = clockTime(node.last_seen);

  if (node.status === "online") {
    headline.textContent = activity;
    seen.textContent = `Updated ${formatAge(node.age_s)} ago · ${clock}`;
    // A dark room is worth a note, but it is not a fault, so it is not red.
    if (isNest && node.low_light === true) {
      note.textContent = `Room is dark: ${node.values.lux} lux, under ${darkLux}.`;
    }
  } else {
    if (activity !== "") {
      headline.textContent = `Last known: ${activity}`;
    }
    headline.classList.add("muted");
    seen.textContent = `Last message ${clock}`;
  }

  if (node.status === "stale") {
    alert.textContent = `No message for ${formatAge(node.age_s)}.`;
  }
  if (node.status === "offline") {
    alert.textContent = `Offline. No message since ${clock} (${formatAge(node.age_s)}).`;
    hint.textContent = isNest ? OFFLINE_HINT.nest : OFFLINE_HINT.doll;
    known.textContent = TEXT.lastKnownValues;
  }

  const rows = [];
  for (const [key, value] of orderedEntries(node.values)) {
    rows.push(makeValueRow(key, value));
  }
  values.replaceChildren(...rows);
}

function showSummary(text, shape, alarm) {
  const summary = document.getElementById("summary");
  summary.classList.toggle("alarm", alarm);
  summary.querySelector(".shape").className = `shape shape-${shape}`;
  if (text !== shownSummary) {
    document.getElementById("summary-text").textContent = text;
    shownSummary = text;
  }
}

// One sentence that names the problem to act on first. First match wins.
function drawSummary(nodes, staleAfter) {
  const shown = nodes.filter((node) => node.node_id <= 2);
  const nest = shown.find((node) => node.node_id === 0);
  const problems = shown.filter((node) => node.status === "stale" || node.status === "offline");
  const waiting = shown.filter((node) => node.status === "unknown");

  if (nest !== undefined && nest.status === "offline") {
    showSummary(TEXT.nestOffline, "offline", true);
    return;
  }

  if (problems.length > 0) {
    const phrases = problems.map((node) => {
      const name = NODE_NAMES[node.node_id];
      if (node.status === "offline") {
        return `${name} offline.`;
      }
      return `${name} has not sent a message for over ${staleAfter} s.`;
    });
    const anyOffline = problems.some((node) => node.status === "offline");
    showSummary(phrases.join(" "), anyOffline ? "offline" : "stale", true);
    return;
  }

  if (waiting.length > 0) {
    const names = waiting.map((node) => NODE_NAMES[node.node_id]);
    showSummary(`Waiting for the first message from ${nameList.format(names)}.`, "unknown", false);
    return;
  }

  showSummary(TEXT.allWorking, "online", false);
}

function drawCounts(nodes) {
  const items = [];
  for (const node of nodes) {
    const item = document.createElement("li");
    const word = node.counts.duplicate === 1 ? "duplicate" : "duplicates";
    item.textContent = `${NODE_NAMES[node.node_id]}: ${node.counts.added} received, ${node.counts.duplicate} ${word}`;
    items.push(item);
  }
  document.getElementById("counts").replaceChildren(...items);
}

function eventValues(event) {
  const parts = [];
  if (event.activity) {
    parts.push(ACTIVITY_TEXT[event.activity] || event.activity);
  }
  for (const [key, value] of orderedEntries(event.data)) {
    parts.push(`${LABELS[key] || key} ${showValue(key, value)}`);
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
    const device = NODE_NAMES[event.node_id] || `Device ${event.node_id}`;
    const eventName = EVENT_TEXT[event.event_type] || event.event_type;
    const time = clockTime(event.recorded_at);
    // Delay = laptop receive time - recorded time. Raw data for T23 latency.
    const delay = event.received_at - event.recorded_at;

    const cells = [time, device, eventName, eventValues(event), `${delay.toFixed(1)} s`];
    cells.forEach((text, i) => {
      const cell = document.createElement("td");
      cell.textContent = text;
      if (i === 3) {
        cell.className = "values-cell";
      }
      if (i === 4 && delay >= LATE_DELAY_S) {
        cell.className = "late";
      }
      row.append(cell);
    });

    // Each summary needs its own name for screen readers; the event number keeps
    // it unique when one device sends two messages in the same second.
    const number = event.event_id.split("/").pop();
    const label = `JSON for ${device} ${eventName.toLowerCase()} at ${time}, event ${number}`;
    const detailCell = document.createElement("td");
    const copy = { ...event };
    delete copy.received_at;
    detailCell.append(makeDetails(event.event_id, "JSON", label, [
      { title: `Received ${clockTime(event.received_at)}`, text: JSON.stringify(copy, null, 2), pre: true },
    ]));
    row.append(detailCell);
    rows.push(row);
  }

  if (rows.length === 0) {
    return;
  }
  body.replaceChildren(...rows);
  reopen(body, wasOpen);
}

function drawErrors(result) {
  const total = document.getElementById("rejected-total");
  total.textContent = String(result.rejected_total);
  total.classList.toggle("has-count", result.rejected_total > 0);
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
    items.push(makeDetails(String(error.id), `${clockTime(error.time)} · ${error.reason}`, "", [
      { text: `From ${error.source}` },
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

// Value and activity charts use recorded_at, the time the device measured it.
// A message the nest kept and sent 80 s late then lands where it belongs,
// not at the right edge as if it were new. Sorted, because late ones arrive last.
// Points measured before the chart start are left out; a device clock a little
// ahead of the laptop is held at "now".
function recordedPoints(points, start, end, valueOf) {
  const result = [];
  for (const point of points) {
    if (point.recorded_at < start) {
      continue;
    }
    result.push({ t: Math.min(point.recorded_at, end), value: valueOf(point) });
  }
  result.sort((a, b) => a.t - b.t);
  return result;
}

function valuePoints(points, key, start, end) {
  return recordedPoints(points, start, end, (point) => {
    let value = point.data[key];
    if (value === undefined) {
      value = null;
    }
    return value;
  });
}

function activityPoints(points, start, end) {
  return recordedPoints(points, start, end, (point) => {
    if (!ACTIVITY_ROWS.includes(point.activity)) {
      return null;
    }
    return point.activity;
  });
}

function drawTrends(history, nodes) {
  // chartTipOpen and keyboardTipTime are set by the tooltips in charts.js.
  // Wait only while the mouse rests on a chart with its tooltip open.
  // Keyboard focus does not pause: the tooltip is opened again after the redraw.
  if (document.querySelector(".chart svg:hover") === null) {
    chartTipOpen = false;
  }
  if (chartTipOpen) {
    return;
  }
  // The new chart's focus handler needs this; the old chart may clear it on blur.
  const tipTime = keyboardTipTime;
  // Redrawing also replaces the focused chart. Remember it to give focus back.
  const focused = document.activeElement;
  let focusedChart = null;
  if (focused !== null) {
    focusedChart = focused.closest(".chart");
  }

  const start = history.now - history.minutes * 60;
  const end = history.now;
  const staleAfter = history.stale_after_s;
  const rangeText = history.minutes === 60 ? "1 h" : `${history.minutes} min`;
  const isLive = (nodeId) => nodes.some((node) => node.node_id === nodeId && node.status === "online");

  const nest = history.nodes["0"] || [];
  const lux = valuePoints(nest, "lux", start, end);
  const luxValues = lux.filter((point) => point.value !== null).map((point) => point.value);
  const luxTop = Math.max(200, niceMax(Math.max(0, ...luxValues) * 1.1));
  const darkLux = history.dark_lux;

  drawLineChart(document.getElementById("chart-light"), lux, {
    start: start,
    end: end,
    staleAfter: staleAfter,
    height: 110,
    padLeft: PAD_LEFT,
    name: "Light",
    unit: "lux",
    yMax: luxTop,
    yTicks: [
      { value: 0, label: "0" },
      { value: darkLux, label: String(darkLux) },
      { value: luxTop, label: String(luxTop) },
    ],
    zoneTop: darkLux,
    zoneLabel: "dark",
    live: isLive(0),
    rangeText: rangeText,
  });

  // 400 cm is the longest the HC-SR04 can measure.
  drawLineChart(document.getElementById("chart-distance"), valuePoints(nest, "distance_cm", start, end), {
    start: start,
    end: end,
    staleAfter: staleAfter,
    height: 110,
    padLeft: PAD_LEFT,
    name: "Teacher distance",
    unit: "cm",
    yMax: 400,
    yTicks: [
      { value: 0, label: "0" },
      { value: 200, label: "200" },
      { value: 400, label: "400" },
    ],
    live: isLive(0),
    rangeText: rangeText,
  });

  for (const nodeId of [1, 2]) {
    const points = history.nodes[String(nodeId)] || [];
    const name = NODE_NAMES[nodeId];

    drawActivityChart(document.getElementById(`chart-activity-${nodeId}`), activityPoints(points, start, end), {
      start: start,
      end: end,
      staleAfter: staleAfter,
      name: `${name} activity`,
      rows: ACTIVITY_ROWS,
      rowNames: ACTIVITY_SHORT,
      names: ACTIVITY_TEXT,
      live: isLive(nodeId),
      rangeText: rangeText,
    });

    // 4095 is the top of the ESP32 12-bit ADC. Pressure starts at the same x as
    // the activity chart above it, so one moment sits at one x in the column.
    drawLineChart(document.getElementById(`chart-pressure-${nodeId}`), valuePoints(points, "fsr", start, end), {
      start: start,
      end: end,
      staleAfter: staleAfter,
      height: 94,
      padLeft: PAD_LEFT_WIDE,
      name: `${name} pressure`,
      unit: "of 4095",
      yMax: 4095,
      yTicks: [
        { value: 0, label: "0" },
        { value: history.fsr_active, label: `${history.fsr_active} book` },
        { value: 4095, label: "4095" },
      ],
      refValue: history.fsr_active,
      live: isLive(nodeId),
      rangeText: rangeText,
      step: true,
    });
  }

  // The timeline shows arrivals, so it keeps the receive time t.
  const rows = [];
  for (const nodeId of ["0", "1", "2"]) {
    const points = history.nodes[nodeId] || [];
    rows.push({ label: NODE_NAMES[nodeId], times: points.map((point) => point.t) });
  }
  drawTimeline(document.getElementById("chart-timeline"), rows, {
    start: start,
    end: end,
    staleAfter: staleAfter,
    rangeText: rangeText,
  });

  if (focusedChart !== null) {
    keyboardTipTime = tipTime;
    restoringFocus = true;
    focusedChart.querySelector("svg").focus({ preventScroll: true });
    restoringFocus = false;
  }
}

function setConnected(connected) {
  const connection = document.getElementById("connection");
  document.body.classList.toggle("disconnected", !connected);
  connection.classList.toggle("down", !connected);

  if (connected) {
    connection.textContent = `${TEXT.serverUp} · ${clockTime(lastGoodPollTime)}`;
    return;
  }

  connection.textContent = TEXT.serverDown;

  // A tooltip would show a frozen value as if it were live.
  // chartTipOpen and keyboardTipTime belong to charts.js.
  for (const tip of document.querySelectorAll(".chart-tip")) {
    tip.hidden = true;
  }
  for (const mark of document.querySelectorAll(".crosshair, .hover-dot")) {
    mark.setAttribute("visibility", "hidden");
  }
  chartTipOpen = false;
  keyboardTipTime = null;

  // Without the server we do not know any device's status, so never show ONLINE or OFFLINE.
  for (const card of document.querySelectorAll(".card")) {
    setBadge(card.querySelector(".badge"), "no-server", "NO SERVER");
    card.classList.remove("is-stale", "is-offline");
    card.querySelector(".alert").textContent = "";
    card.querySelector(".hint").textContent = "";
    card.querySelector(".known").textContent = "";
    const node = lastNodes.find((item) => String(item.node_id) === card.dataset.node);
    if (node !== undefined && node.last_seen !== undefined) {
      card.querySelector(".seen").textContent = `Last data ${clockTime(node.last_seen)}. Not current.`;
    }
  }

  const times = lastNodes.filter((node) => node.last_seen !== undefined).map((node) => node.last_seen);
  if (times.length === 0) {
    showSummary(TEXT.noServer, "no-server", true);
    return;
  }
  const newest = clockTime(Math.max(...times));
  showSummary(`Can't reach the laptop server. Showing data from ${newest}. Retrying every second.`, "no-server", true);
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
    lastNodes = nodes;
    lastGoodPollTime = Date.now() / 1000;
    setConnected(true);
    for (const node of nodes) {
      drawNode(node, history.dark_lux);
    }
    drawSummary(nodes, history.stale_after_s);
    drawCounts(nodes);
    drawEvents(events);
    drawErrors(errors);
    drawTrends(history, nodes);
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
