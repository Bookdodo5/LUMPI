// Overview page: fill every live part from the laptop server.
// Live: nest connection, light (lux), teacher near nest, doll rows, needs help,
// mode in the last hour and recent activity. Scores, words and noise have no
// data yet, so the page shows 0 or "no data" there (see index.html).
// Shared helpers (chips, mode bars, needs help) are in shared.js.

const POLL_MS = 2000;
// Activity reads one hour of history, which is bigger, so it is fetched less often.
const ACTIVITY_POLL_MS = 10000;
const ACTIVITY_ROWS_SHOWN = 5;

// Same values as DARK_LUX, DARK_LUX_ERROR, BRIGHT_LUX and TEACHER_NEAR_CM in laptop/dashboard_server.py.
const DARK_LUX = 100;
const LUX_MARGIN = 20;
const BRIGHT_LUX = 1000;
const TEACHER_NEAR_CM = 200;

// Light gauge: lux break points and the gauge angle for each, in degrees.
// 180 = far left (too dark), 0 = far right (too bright). Matches the coloured arcs.
const LUX_STEPS = [0, DARK_LUX - LUX_MARGIN, DARK_LUX + LUX_MARGIN, BRIGHT_LUX - LUX_MARGIN, BRIGHT_LUX + LUX_MARGIN, 2000];
const ANGLE_STEPS = [180, 144, 108, 72, 36, 0];

// Gauge circle in the SVG: centre (130, 140), radius 102.
const GAUGE_X = 130;
const GAUGE_Y = 140;
const GAUGE_R = 102;

// Dolls that need help come first, then dolls with a connection problem.
const ROW_ORDER = { help: 0, offline: 1, stale: 2, unknown: 3, online: 4 };

function luxToAngle(lux) {
  if (lux >= LUX_STEPS[LUX_STEPS.length - 1]) {
    return 0;
  }
  for (let i = 1; i < LUX_STEPS.length; i++) {
    if (lux <= LUX_STEPS[i]) {
      const part = (lux - LUX_STEPS[i - 1]) / (LUX_STEPS[i] - LUX_STEPS[i - 1]);
      return ANGLE_STEPS[i - 1] + part * (ANGLE_STEPS[i] - ANGLE_STEPS[i - 1]);
    }
  }
  return 180;
}

function luxZone(lux) {
  if (lux < DARK_LUX - LUX_MARGIN) {
    return "Too dark";
  }
  if (lux < DARK_LUX + LUX_MARGIN) {
    return "A bit dark";
  }
  if (lux <= BRIGHT_LUX - LUX_MARGIN) {
    return "Normal";
  }
  if (lux <= BRIGHT_LUX + LUX_MARGIN) {
    return "A bit bright";
  }
  return "Too bright";
}

function drawLight(lux) {
  const marker = document.getElementById("light-marker");
  const zone = document.getElementById("light-zone");
  const text = document.getElementById("light-lux");

  if (lux === null || lux === undefined) {
    // Words, not a number, so a failed sensor never looks like a reading.
    marker.setAttribute("visibility", "hidden");
    zone.textContent = "No reading";
    text.textContent = "— lx";
    return;
  }

  const angle = luxToAngle(lux) * Math.PI / 180;
  marker.setAttribute("visibility", "visible");
  marker.setAttribute("cx", (GAUGE_X + GAUGE_R * Math.cos(angle)).toFixed(1));
  marker.setAttribute("cy", (GAUGE_Y - GAUGE_R * Math.sin(angle)).toFixed(1));
  zone.textContent = luxZone(lux);
  text.textContent = `${Math.round(lux)} lx`;
}

function drawNest(nest) {
  const seen = document.getElementById("light-seen");
  const teacher = document.getElementById("teacher-near");
  drawNestStatus(nest, document.getElementById("nest-dot"), document.getElementById("nest-text"));

  // No message from the nest yet: show nothing that looks like a reading.
  if (nest === undefined || nest.status === "unknown") {
    seen.textContent = "No reading yet";
    drawLight(null);
    document.getElementById("light-zone").textContent = "Waiting";
    teacher.textContent = "Teacher near nest: —";
    return;
  }

  const clock = clockTime(nest.last_seen);
  if (nest.status === "online") {
    seen.textContent = `Updated ${clock}`;
  } else {
    seen.textContent = `Old · ${clock}`;
  }
  // Mark values that are not live, so an old reading never looks current.
  const old = nest.status === "online" ? "" : " (old)";

  drawLight(nest.values.lux);

  const distance = nest.values.distance_cm;
  if (distance === null || distance === undefined) {
    teacher.textContent = "Teacher near nest: no reading";
  } else if (distance < TEACHER_NEAR_CM) {
    teacher.textContent = `Teacher near nest: Yes${old}`;
  } else {
    teacher.textContent = `Teacher near nest: No${old}`;
  }
}

// Second line of a row: book, page and try, then what the doll is doing.
function dollWhat(doll) {
  const parts = [];
  const values = doll.values;
  if (values.uid) {
    parts.push(`Book ${values.uid}`);
  }
  if (values.passage !== null && values.passage !== undefined) {
    parts.push(`p.${values.passage}`);
  }
  if (values.attempt !== null && values.attempt !== undefined) {
    parts.push(`try ${values.attempt}`);
  }
  parts.push(ACTIVITY_TEXT[doll.activity] || "No activity yet");
  return parts.join(" · ");
}

function rowOrder(doll) {
  if (helpReason(doll) !== null) {
    return ROW_ORDER.help;
  }
  return ROW_ORDER[doll.status];
}

function drawDolls(nodes) {
  const dolls = nodes.filter((node) => DOLL_IDS.includes(node.node_id));
  dolls.sort((a, b) => rowOrder(a) - rowOrder(b) || a.node_id - b.node_id);

  const template = document.getElementById("doll-row-template");
  const rows = [];
  let helpCount = 0;

  for (const doll of dolls) {
    const row = template.content.firstElementChild.cloneNode(true);
    row.href = `/doll.html?doll=${doll.node_id}`;
    row.querySelector(".doll-name").textContent = `Doll ${doll.node_id}`;
    setStateChip(row.querySelector(".doll-state"), doll);

    if (doll.status === "unknown") {
      row.querySelector(".doll-what").textContent = "Waiting for the first message";
      setChip(row.querySelector(".doll-mode"), "—", "var(--jp-grey-soft)", false, true);
      rows.push(row);
      continue;
    }

    const reason = helpReason(doll);
    if (reason !== null) {
      helpCount += 1;
    }
    // The reason is already visible: "try 3" or "Stopped (STOP button)" is in this line.
    row.querySelector(".doll-what").textContent = dollWhat(doll);
    setModeChip(row.querySelector(".doll-mode"), doll.activity);

    // Red row for "needs help" and for offline, as in the design.
    if (reason !== null || doll.status === "offline") {
      row.style.background = "var(--jp-red-tint)";
    }
    rows.push(row);
  }

  document.getElementById("doll-rows").replaceChildren(...rows);
  document.getElementById("needs-help-count").textContent = String(helpCount);
  document.getElementById("needs-help-total").textContent = `of ${DOLL_IDS.length} dolls`;
}

function drawModes(history) {
  const start = history.now - history.minutes * 60;
  const end = history.now;
  const grid = document.createElement("div");
  grid.style.cssText = "display:grid;grid-template-columns:48px minmax(0,1fr);gap:8px 10px;align-items:center";

  for (const dollId of DOLL_IDS) {
    const label = document.createElement("span");
    label.style.cssText = "font-weight:700;font-size:13px";
    label.textContent = `Doll ${dollId}`;
    grid.append(label, modeBar(modeSegments(sortedPoints(history, dollId), start, end), 26));
  }
  grid.append(document.createElement("span"), timeAxis(start, end));
  document.getElementById("mode-rows").replaceChildren(grid);
}

// One activity line: time, bold doll name, then the text.
function activityRow(change) {
  const row = document.createElement("a");
  row.className = "row";
  row.href = `/doll.html?doll=${change.dollId}`;
  row.style.cssText = "display:grid;grid-template-columns:44px 1fr;gap:8px;padding:8px 6px;border-bottom:var(--jp-border);font-size:13px;align-items:center";
  const clock = document.createElement("span");
  clock.style.cssText = "color:var(--jp-grey);font-size:12px";
  clock.textContent = clockTime(change.time);
  const body = document.createElement("span");
  const bold = document.createElement("b");
  bold.textContent = `Doll ${change.dollId}`;
  body.append(bold, change.text);
  if (change.help) {
    body.style.color = "var(--jp-red)";
  }
  row.append(clock, body);
  return row;
}

// Only changes are listed: a new activity, a book put on the mat (start event),
// or a doll reaching the "needs help" try. Heartbeats that repeat are skipped.
function drawActivity(history) {
  const changes = [];

  for (const dollId of DOLL_IDS) {
    let previous = null;
    let previousTry = null;

    for (const point of sortedPoints(history, dollId)) {
      const tries = point.data.attempt;
      if (point.event_type === "start") {
        let book = "a book";
        if (point.data.uid) {
          book = `book ${point.data.uid}`;
        }
        changes.push({ time: point.recorded_at, dollId: dollId, text: ` started reading ${book}` });
      } else if (point.activity !== previous && point.activity !== null) {
        changes.push({ time: point.recorded_at, dollId: dollId, text: ` → ${ACTIVITY_TEXT[point.activity] || point.activity}`, help: point.activity === "stopped" });
      }
      if (typeof tries === "number" && tries >= NEEDS_HELP_TRIES && tries !== previousTry) {
        changes.push({ time: point.recorded_at, dollId: dollId, text: ` ⚑ needs help · try ${tries} on p.${point.data.passage}`, help: true });
      }
      previous = point.activity;
      previousTry = tries;
    }
  }

  changes.sort((a, b) => b.time - a.time);
  const rows = changes.slice(0, ACTIVITY_ROWS_SHOWN).map(activityRow);

  if (rows.length === 0) {
    const empty = document.createElement("p");
    empty.className = "empty-note";
    empty.style.margin = "12px 0";
    empty.textContent = "No doll activity in the last hour.";
    rows.push(empty);
  }
  document.getElementById("activity-rows").replaceChildren(...rows);
}

async function poll() {
  try {
    const nodes = await getJson("/api/dashboard/nodes");
    drawNest(nodes.find((node) => node.node_id === 0));
    drawDolls(nodes);
  } catch (error) {
    console.warn("dashboard poll failed:", error);
    drawServerDown(document.getElementById("nest-dot"), document.getElementById("nest-text"));
    document.getElementById("light-seen").textContent = "Not current";
    // Without the server we do not know any doll's state, so never show OK.
    for (const chip of document.querySelectorAll("#doll-rows .doll-state")) {
      setChip(chip, "No server", "var(--jp-grey-soft)", false, true);
    }
  }
  setTimeout(poll, POLL_MS);
}

async function pollActivity() {
  try {
    const history = await getJson("/api/dashboard/history?minutes=60");
    drawModes(history);
    drawActivity(history);
  } catch (error) {
    // poll() already shows that the server is down; keep the last lists.
    console.warn("activity poll failed:", error);
  }
  setTimeout(pollActivity, ACTIVITY_POLL_MS);
}

poll();
pollActivity();
