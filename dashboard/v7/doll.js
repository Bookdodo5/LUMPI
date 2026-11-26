// Doll page: /doll.html?doll=1 or ?doll=2. Fills the page from the cloud server.
// Live: mode, state, help popup, book and page, mode changes.
// Today: one timeline (mode, noise in Quiet Study, scores, help presses) and a summary.
// Scores (meter, words, sounds, practice) come from the AI.
// Today = this class, Past = this doll's saved classes. Shared helpers are in shared.js.

const POLL_MS = 2000;
const HISTORY_POLL_MS = 10000;
const SCORE_POLL_MS = 10000;
const MODE_CHANGES_SHOWN = 8;
const WORDS_SHOWN = 8;
const SOUNDS_SHOWN = 10;
const PRACTICE_SHOWN = 6;
// Noise lane, in dB. shortcut: placeholder bands until the doll microphone is calibrated (T09).
const NOISE_MIN_DB = 40;
const NOISE_MAX_DB = 90;
const NOISY_DB = 60;
const TOO_LOUD_DB = 70;

let view = "today";
// Last data from the server; the timeline needs both history and scores.
let lastHistory = null;
let lastScores = null;

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

// Points of this doll from today only (history holds up to 3 hours).
function todayPoints() {
  if (lastHistory === null) {
    return [];
  }
  const midnight = new Date().setHours(0, 0, 0, 0) / 1000;
  return sortedPoints(lastHistory, dollId).filter((point) => point.recorded_at >= midnight);
}

function todayScores() {
  if (lastScores === null) {
    return [];
  }
  return scoresOf(lastScores.scores, dollId);
}

function drawModeChanges(points) {
  const lines = [];
  let previous = null;
  for (const point of points) {
    if (point.event_type === "help" || point.event_type === "noise") {
      continue;
    }
    const mode = modeOf(point.activity);
    if (previous !== null && mode !== previous) {
      lines.push({ time: point.recorded_at, text: ` ${MODE_NAME[previous]} → ${MODE_NAME[mode]}` });
    }
    previous = mode;
  }
  lines.reverse();

  const items = lines.slice(0, MODE_CHANGES_SHOWN).map((line) => {
    const item = document.createElement("span");
    const clock = document.createElement("b");
    clock.textContent = clockTime(line.time);
    item.append(clock, line.text);
    return item;
  });
  if (items.length === 0) {
    items.push(emptyNote(previous === null ? "No messages from this doll today." : `${MODE_NAME[previous]} the whole time.`));
  }
  document.getElementById("mode-changes").replaceChildren(...items);
}

// One timeline for today: mode blocks, noise reports (Quiet Study only), each
// reading try's score, and red lines where the child pressed help.
function drawTimeline() {
  const box = document.getElementById("timeline");
  const points = todayPoints();
  const scores = todayScores();
  const end = Date.now() / 1000;
  let start = end;
  if (points.length > 0) {
    start = Math.min(start, points[0].recorded_at);
  }
  for (const line of scores) {
    start = Math.min(start, line.session);
  }
  if (end - start < 60) {
    box.replaceChildren(emptyNote("No messages from this doll today."));
    return;
  }

  const left = 70;
  const right = 625;
  const xOf = (time) => left + (time - start) / (end - start) * (right - left);
  const noiseY = (db) => 146 - (Math.min(Math.max(db, NOISE_MIN_DB), NOISE_MAX_DB) - NOISE_MIN_DB) / (NOISE_MAX_DB - NOISE_MIN_DB) * 80;
  const scoreY = (score) => 226 - score / 100 * 60;
  const svg = svgPart("svg", { viewBox: "0 0 640 262", role: "img", "aria-label": "Today's timeline for this doll: mode, noise in Quiet Study, reading scores and help presses." });
  svg.style.cssText = "width:100%;height:100%;display:block";

  const segments = modeSegments(points, start, end);

  // Quiet Study shading across every lane.
  for (const segment of segments) {
    if (segment.mode === "quiet") {
      svg.append(svgPart("rect", { x: xOf(segment.from), y: 16, width: xOf(segment.to) - xOf(segment.from), height: 214, fill: "var(--lm-purple)", "fill-opacity": 0.08 }));
    }
  }

  // Lane labels and backgrounds.
  svg.append(svgPart("text", { x: left - 8, y: 39, "font-size": 11, fill: "var(--jp-grey)", "text-anchor": "end" }, "Mode"));
  svg.append(svgPart("text", { x: left - 8, y: 110, "font-size": 11, fill: "var(--jp-grey)", "text-anchor": "end" }, "Noise"));
  svg.append(svgPart("text", { x: left - 8, y: 200, "font-size": 11, fill: "var(--jp-grey)", "text-anchor": "end" }, "Score"));
  svg.append(svgPart("rect", { x: left, y: noiseY(NOISE_MAX_DB), width: right - left, height: noiseY(TOO_LOUD_DB) - noiseY(NOISE_MAX_DB), fill: "var(--jp-red-tint)" }));
  svg.append(svgPart("rect", { x: left, y: noiseY(TOO_LOUD_DB), width: right - left, height: noiseY(NOISY_DB) - noiseY(TOO_LOUD_DB), fill: "color-mix(in srgb,var(--jp-warn) 14%,var(--jp-white))" }));
  svg.append(svgPart("text", { x: left + 4, y: noiseY(NOISE_MAX_DB) + 11, "font-size": 9.5, fill: "var(--jp-red)" }, `Too loud · ${TOO_LOUD_DB}+ dB`));
  svg.append(svgPart("text", { x: left + 4, y: noiseY(TOO_LOUD_DB) + 11, "font-size": 9.5, fill: "var(--jp-warn)" }, "A bit noisy"));
  svg.append(svgPart("line", { x1: left, y1: noiseY(NOISE_MIN_DB), x2: right, y2: noiseY(NOISE_MIN_DB), stroke: "var(--jp-line)" }));
  svg.append(svgPart("line", { x1: left, y1: scoreY(SCORE_PRACTISE), x2: right, y2: scoreY(SCORE_PRACTISE), stroke: "var(--jp-red)", "stroke-dasharray": "4 3" }));
  svg.append(svgPart("text", { x: left + 4, y: scoreY(SCORE_PRACTISE) + 11, "font-size": 9.5, fill: "var(--jp-red)" }, `Practise · ${SCORE_PRACTISE}`));
  svg.append(svgPart("line", { x1: left, y1: scoreY(0), x2: right, y2: scoreY(0), stroke: "var(--jp-grey-soft)" }));

  // Help presses: drawn first, dashed, so blocks and dots stay on top of the line.
  for (const point of points.filter((item) => item.event_type === "help")) {
    const x = xOf(point.recorded_at);
    svg.append(svgPart("line", { x1: x, y1: 14, x2: x, y2: 230, stroke: "var(--jp-red)", "stroke-width": 1.5, "stroke-dasharray": "4 3", "stroke-opacity": 0.8 }));
    svg.append(svgPart("text", { x: Math.min(x, right), y: 11, "font-size": 10.5, "font-weight": 700, fill: "var(--jp-red)", "text-anchor": x > right - 60 ? "end" : "middle" }, `⚑ Help ${clockTime(point.recorded_at)}`));
  }

  // Mode blocks.
  for (const segment of segments) {
    if (segment.mode === null) {
      continue;
    }
    const width = xOf(segment.to) - xOf(segment.from);
    const block = svgPart("rect", { x: xOf(segment.from), y: 26, width: Math.max(width - 1, 0.5), height: 20, rx: 3, fill: MODE_COLOR[segment.mode] });
    block.append(svgPart("title", {}, `${MODE_NAME[segment.mode]} ${clockTime(segment.from)}–${clockTime(segment.to)}`));
    svg.append(block);
    if (width > 50) {
      svg.append(svgPart("text", { x: xOf(segment.from) + width / 2, y: 40, "font-size": 10.5, fill: segment.mode === "idle" ? "var(--jp-grey)" : "var(--jp-white)", "text-anchor": "middle" }, MODE_SHORT[segment.mode]));
    }
  }

  // Noise reports: one stem per report.
  const noise = points.filter((point) => point.event_type === "noise");
  for (const point of noise) {
    const x = xOf(point.recorded_at);
    const y = noiseY(point.data.db);
    svg.append(svgPart("line", { x1: x, y1: noiseY(NOISE_MIN_DB), x2: x, y2: y, stroke: "var(--jp-grey-soft)" }));
    const dot = svgPart("circle", { cx: x, cy: y, r: 4.5, fill: point.data.db >= TOO_LOUD_DB ? "var(--jp-red)" : "var(--jp-warn)", stroke: "var(--jp-white)", "stroke-width": 1.5 });
    dot.append(svgPart("title", {}, `${clockTime(point.recorded_at)} · ${Math.round(point.data.db)} dB`));
    svg.append(dot);
  }
  svg.append(svgPart("text", { x: left, y: 61, "font-size": 10.5, fill: noise.length > 0 ? "var(--jp-red)" : "var(--jp-grey)", "font-weight": 700 }, `${noise.length} noise reports in Quiet Study`));

  // Scores: one dot per reading try, joined inside each session.
  for (const session of sessionsOf(scores, dollId)) {
    const coords = session.lines.map((line) => `${xOf(line.recorded_at).toFixed(1)},${scoreY(line.score).toFixed(1)}`).join(" ");
    svg.append(svgPart("polyline", { points: coords, fill: "none", stroke: "var(--jp-grey-soft)", "stroke-width": 1.5 }));
    for (const line of session.lines) {
      const dot = svgPart("circle", { cx: xOf(line.recorded_at), cy: scoreY(line.score), r: 4, fill: bandColor(line.score) });
      dot.append(svgPart("title", {}, `${clockTime(line.recorded_at)} · p.${line.passage} try ${line.attempt} · ${Math.round(line.score)}%`));
      svg.append(dot);
    }
    const last = session.lines[session.lines.length - 1];
    // Near the right edge the label goes left of the dot, so it stays inside the chart.
    const lastX = xOf(last.recorded_at);
    const nearEdge = lastX > right - 20;
    svg.append(svgPart("text", { x: nearEdge ? lastX - 8 : lastX, y: scoreY(last.score) - 7, "font-size": 10, "font-weight": 700, fill: bandColor(session.avg), "text-anchor": nearEdge ? "end" : "middle" }, `${Math.round(session.avg)}%`));
  }

  // Time axis: 4 ticks, last one "now".
  for (let i = 0; i <= 3; i++) {
    const time = start + (end - start) * i / 3;
    svg.append(svgPart("line", { x1: xOf(time), y1: 230, x2: xOf(time), y2: 234, stroke: "var(--jp-grey-soft)" }));
    svg.append(svgPart("text", { x: xOf(time), y: 248, "font-size": 10, fill: "var(--jp-grey)", "text-anchor": i === 0 ? "start" : (i === 3 ? "end" : "middle") }, i === 3 ? "now" : clockTime(time)));
  }
  box.replaceChildren(svg);
}

function summaryCell(label, value, color) {
  const cell = document.createElement("div");
  const small = document.createElement("div");
  small.style.cssText = "font-size:10.5px;text-transform:uppercase;letter-spacing:.12em;color:var(--jp-grey)";
  small.textContent = label;
  const big = document.createElement("div");
  big.style.cssText = `font-family:var(--jp-font-display);font-size:22px;font-weight:700;line-height:1.2;color:${color || "var(--jp-black)"}`;
  big.textContent = value;
  cell.append(small, big);
  return cell;
}

function drawSummary() {
  const cells = [];
  if (view === "today") {
    const points = todayPoints();
    const scores = todayScores();
    const end = Date.now() / 1000;
    const start = points.length > 0 ? points[0].recorded_at : end;
    const totals = modeTotals(modeSegments(points, start, end));
    const avg = average(scores);
    cells.push(summaryCell("Reading", minutesText(totals.reading)));
    cells.push(summaryCell("Quiet Study", minutesText(totals.quiet)));
    cells.push(summaryCell("Sessions", String(sessionsOf(scores, dollId).length)));
    cells.push(summaryCell("Tries", String(scores.length)));
    cells.push(summaryCell("Avg score", avg === null ? "—" : `${Math.round(avg)}%`, avg === null ? null : bandColor(avg)));
    cells.push(summaryCell("Help presses", String(points.filter((point) => point.event_type === "help").length)));
    document.getElementById("summary-title").textContent = "Today at a glance";
  } else {
    const scores = lastScores === null ? [] : scoresOf(lastScores.scores, dollId);
    const classes = classesOf(scores, dollId);
    const avg = average(scores);
    const sounds = soundsOf(scores, dollId);
    let change = "—";
    if (classes.length > 1) {
      change = changeText(classes[classes.length - 1].avg - classes[0].avg);
    }
    cells.push(summaryCell("Classes", String(classes.length)));
    cells.push(summaryCell("Sessions", String(classes.reduce((sum, entry) => sum + entry.sessions, 0))));
    cells.push(summaryCell("Tries", String(scores.length)));
    cells.push(summaryCell("Avg score", avg === null ? "—" : `${Math.round(avg)}%`, avg === null ? null : bandColor(avg)));
    cells.push(summaryCell("First → latest", change));
    cells.push(summaryCell("Weakest sound", sounds.length === 0 ? "—" : `/${sounds[0].ipa}/`));
    document.getElementById("summary-title").textContent = "All classes at a glance";
  }
  document.getElementById("summary").replaceChildren(...cells);
}

function drawScores() {
  const scores = lastScores === null ? [] : scoresOf(lastScores.scores, dollId);
  const label = document.getElementById("latest-label");
  const text = document.getElementById("latest-text");
  const gauge = document.getElementById("latest-gauge");

  if (view === "today") {
    const sessions = sessionsOf(scores, dollId);
    document.getElementById("timeline-title").textContent = "Today: mode, noise and score";
    document.getElementById("timeline-note").textContent = "Shaded = Quiet Study · noise is only measured there";
    drawTimeline();
    if (sessions.length === 0) {
      label.textContent = "Latest session";
      gauge.replaceChildren(scoreGauge(null));
      text.textContent = "No reading score yet today";
    } else {
      const latest = sessions[sessions.length - 1];
      const line = latest.lines[latest.lines.length - 1];
      label.textContent = `Latest session · today ${clockTime(latest.start)} · book ${line.uid}, p.${line.passage}`;
      gauge.replaceChildren(scoreGauge(latest.avg));
      text.textContent = sessions.length > 1 ? `Reading score · ${changeText(latest.avg - sessions[sessions.length - 2].avg)} pts vs previous session` : `Reading score · ${latest.count} tries`;
    }
  } else {
    const classes = classesOf(scores, dollId);
    document.getElementById("timeline-title").textContent = "Past classes: score per class";
    document.getElementById("timeline-note").textContent = `${classes.length} classes`;
    const box = document.getElementById("timeline");
    if (classes.length === 0) {
      box.replaceChildren(emptyNote("No saved classes yet."));
      label.textContent = "Latest class";
      gauge.replaceChildren(scoreGauge(null));
      text.textContent = "No saved classes yet";
    } else {
      const points = classes.map((entry, index) => ({ x: index, y: entry.avg, title: `${shortDay(entry.day)} · ${Math.round(entry.avg)}% · ${entry.sessions} sessions` }));
      const step = Math.max(1, Math.ceil(classes.length / 6));
      const labels = classes.map((entry, index) => ({ x: index, text: shortDay(entry.day) })).filter((item, index) => index % step === 0 || index === classes.length - 1);
      box.replaceChildren(scoreChart([{ name: `Doll ${dollId}`, color: DOLL_COLOR[dollId], points: points }], 0, classes.length - 1, labels, "Average score of each class day."));
      const latest = classes[classes.length - 1];
      label.textContent = `Latest class · ${shortDay(latest.day)}`;
      gauge.replaceChildren(scoreGauge(latest.avg));
      text.textContent = classes.length > 1 ? `Reading score · ${changeText(latest.avg - classes[0].avg)} pts since the first class` : `Reading score · ${latest.count} tries`;
    }
  }

  drawSummary();
  document.getElementById("sounds-note").textContent = view === "today" ? "This doll today · weakest first" : "All classes · ▲▼ = latest vs first class";
  document.getElementById("sound-rows").replaceChildren(...soundRows(soundsOf(scores, dollId), SOUNDS_SHOWN));
  document.getElementById("word-rows").replaceChildren(...wordRows(wordsOf(scores, dollId), WORDS_SHOWN));

  const practice = practiceOf(scores).slice(0, PRACTICE_SHOWN);
  document.getElementById("practice-note").textContent = view === "today" ? "1st try today → latest" : "1st try ever → latest";
  document.getElementById("practice").replaceChildren(...(practice.length === 0 ? [emptyNote("No word read twice yet.")] : practiceRows(practice)));
}

// Past is only read when the button is pressed; Today is read again every 10 s.
async function loadScores() {
  const asked = view;
  try {
    const data = await getJson(`/api/dashboard/scores?day=${asked}`);
    if (asked === view) {
      lastScores = data;
      drawScores();
    }
  } catch (error) {
    console.warn("doll score poll failed:", error);
  }
}

async function pollScores() {
  if (view === "today") {
    await loadScores();
  }
  setTimeout(pollScores, SCORE_POLL_MS);
}

async function poll() {
  try {
    const nodes = await getJson("/api/dashboard/nodes");
    drawHeader(nodes);
    drawHelpPopup(nodes);
  } catch (error) {
    console.warn("doll poll failed:", error);
    drawServerDown(document.getElementById("nest-dot"), document.getElementById("nest-text"));
    setChip(document.getElementById("doll-state"), "No server", "var(--jp-grey-soft)", false, true);
  }
  setTimeout(poll, POLL_MS);
}

async function pollHistory() {
  try {
    lastHistory = await getJson("/api/dashboard/history?minutes=180");
    drawModeChanges(todayPoints());
    if (view === "today") {
      drawTimeline();
      drawSummary();
    }
  } catch (error) {
    console.warn("doll history poll failed:", error);
  }
  setTimeout(pollHistory, HISTORY_POLL_MS);
}

setupViewButtons((chosen) => {
  view = chosen;
  loadScores();
});

poll();
pollHistory();
pollScores();
