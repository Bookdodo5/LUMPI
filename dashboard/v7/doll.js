// Doll page: /doll.html?doll=1 or ?doll=2. Fills the page from the laptop server.
// Live: mode, state, needs help, book and page, mode changes and time per mode.
// Scores, words, sessions and noise have no data yet, so the page says so (see doll.html).
// Shared helpers are in shared.js.

const POLL_MS = 2000;
const HISTORY_POLL_MS = 10000;
const MODE_CHANGES_SHOWN = 6;

// A missing or wrong ?doll= falls back to Doll 1.
let dollId = Number(new URLSearchParams(location.search).get("doll"));
if (!DOLL_IDS.includes(dollId)) {
  dollId = 1;
}

document.title = `LUMPI · Doll ${dollId}`;
document.getElementById("doll-title").textContent = `Doll ${dollId}`;
document.getElementById("session-link").href = `/session.html?doll=${dollId}`;

function drawHeader(nodes) {
  const doll = nodes.find((node) => node.node_id === dollId);
  const mode = document.getElementById("doll-mode");
  const state = document.getElementById("doll-state");
  const info = document.getElementById("doll-info");
  drawNestStatus(nodes.find((node) => node.node_id === 0), document.getElementById("nest-dot"), document.getElementById("nest-text"));

  if (doll === undefined || doll.status === "unknown") {
    setChip(mode, "—", "var(--jp-grey-soft)", false, true);
    setChip(state, "Waiting", "var(--jp-grey-soft)", false, true);
    info.textContent = "No message from this doll yet";
    return;
  }

  setModeChip(mode, doll.activity);
  setStateChip(state, doll);

  const parts = [];
  const reason = helpReason(doll);
  if (reason !== null) {
    parts.push(reason);
  }
  if (doll.values.uid) {
    parts.push(`Book ${doll.values.uid}`);
  } else {
    parts.push("No book");
  }
  if (doll.values.passage !== null && doll.values.passage !== undefined) {
    parts.push(`page ${doll.values.passage}`);
  }
  parts.push(`last active ${clockTime(doll.last_seen)}`);
  info.textContent = parts.join(" · ");
}

function drawHistory(history) {
  const start = history.now - history.minutes * 60;
  const end = history.now;
  const points = sortedPoints(history, dollId);
  const segments = modeSegments(points, start, end);
  const totals = modeTotals(segments);

  document.getElementById("doll-mode-bar").replaceChildren(modeBar(segments, 26));
  document.getElementById("doll-axis").replaceChildren(timeAxis(start, end));
  document.getElementById("reading-time").textContent = minutesText(totals.reading);
  document.getElementById("quiet-time").textContent = minutesText(totals.quiet);

  // Mode changes, newest first, like "10:05 Quiet Study → Reading".
  const lines = [];
  let previous = null;
  for (const point of points) {
    const mode = modeOf(point.activity);
    if (previous !== null && mode !== previous) {
      lines.push({ time: point.recorded_at, text: ` ${MODE_NAME[previous]} → ${MODE_NAME[mode]}` });
    }
    previous = mode;
  }
  lines.reverse();

  const list = document.getElementById("mode-changes");
  const items = [];
  for (const line of lines.slice(0, MODE_CHANGES_SHOWN)) {
    const item = document.createElement("span");
    const clock = document.createElement("b");
    clock.textContent = clockTime(line.time);
    item.append(clock, line.text);
    items.push(item);
  }
  if (items.length === 0) {
    const empty = document.createElement("p");
    empty.className = "empty-note";
    empty.textContent = previous === null ? "No messages from this doll in the last hour." : `${MODE_NAME[previous]} the whole time.`;
    items.push(empty);
  }

  const total = document.createElement("span");
  total.style.cssText = "padding-top:6px;margin-top:2px;border-top:var(--jp-border);color:var(--jp-grey)";
  const reading = document.createElement("b");
  reading.style.color = "var(--jp-black)";
  reading.textContent = minutesText(totals.reading);
  const quiet = document.createElement("b");
  quiet.style.color = "var(--jp-black)";
  quiet.textContent = minutesText(totals.quiet);
  total.append(reading, " reading · ", quiet, " quiet study");
  items.push(total);

  list.replaceChildren(...items);
}

async function poll() {
  try {
    drawHeader(await getJson("/api/dashboard/nodes"));
  } catch (error) {
    console.warn("doll poll failed:", error);
    drawServerDown(document.getElementById("nest-dot"), document.getElementById("nest-text"));
    setChip(document.getElementById("doll-state"), "No server", "var(--jp-grey-soft)", false, true);
  }
  setTimeout(poll, POLL_MS);
}

async function pollHistory() {
  try {
    drawHistory(await getJson("/api/dashboard/history?minutes=60"));
  } catch (error) {
    console.warn("doll history poll failed:", error);
  }
  setTimeout(pollHistory, HISTORY_POLL_MS);
}

poll();
pollHistory();
