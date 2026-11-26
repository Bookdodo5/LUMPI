// Doll page (doll.html?doll=1 or ?doll=2).
// Live: mode, state, book and page, mode changes. Today: one timeline (mode, noise in Quiet Study,
// scores, help presses) and a summary. Past: this doll's saved classes.
// Scores (meter, words, sounds, did practising help) come from the AI.

// Noise lane, in dB. shortcut: placeholder bands until the doll microphone is calibrated (T09).
const NOISE_MIN_DB = 40;
const NOISE_MAX_DB = 90;
const NOISY_DB = 60;
const TOO_LOUD_DB = 70;

let dollId = Number(new URLSearchParams(location.search).get("doll"));
if (!DOLL_IDS.includes(dollId)) {
  dollId = 1;  // a missing or wrong ?doll= shows Doll 1
}
document.title = `LUMPI · Doll ${dollId}`;
setText("doll-title", `Doll ${dollId}`);
byId("session-link").href = `/session.html?doll=${dollId}`;

// The last replies from the cloud: the timeline needs both.
let lastHistory = { nodes: { [dollId]: [] } };
let lastScores = [];  // this doll's score lines, of Today or Past

// ---- Live ----

function drawHeader(nodes) {
  const doll = nodes[dollId];
  setModeChip(byId("doll-mode"), doll);
  setStateChip(byId("doll-state"), doll);
  if (doll.status === "unknown") {
    setText("doll-info", "No message from this doll yet");
    return;
  }
  const parts = [];
  if (doll.needs_help) {
    parts.push(`Asked for help at ${clockTime(doll.help_at)}`);
  }
  parts.push(doll.values.uid ? `Book ${doll.values.uid}` : "No book");
  if (sent(doll.values.passage)) {
    parts.push(`page ${doll.values.passage}`);
  }
  parts.push(`last active ${clockTime(doll.last_seen)}`);
  setText("doll-info", parts.join(" · "));
}

// This doll's history points from today only (the history holds up to 3 hours).
function todayPoints() {
  const midnight = new Date().setHours(0, 0, 0, 0) / 1000;
  return sortedPoints(lastHistory, dollId).filter((point) => point.recorded_at >= midnight);
}

function drawModeChanges(points) {
  const lines = [];
  let previous = null;
  for (const point of points.filter((item) => item.event_type !== "help" && item.event_type !== "noise")) {
    const mode = modeOf(point.activity);
    if (previous !== null && mode !== previous) {
      const change = ` ${MODES[previous].name} → ${MODES[mode].name}`;
      lines.push(el("span", {}, el("b", {}, clockTime(point.recorded_at)), change));
    }
    previous = mode;
  }
  if (lines.length === 0) {
    lines.push(emptyNote(previous === null ? "No messages from this doll today."
      : `${MODES[previous].name} the whole time.`));
  }
  byId("mode-changes").replaceChildren(...lines.reverse().slice(0, 8));
}

// ---- Today: one timeline ----

// Lanes from top to bottom: mode blocks, noise reports (Quiet Study only), each reading try's
// score, and red dashed lines where the child pressed help.
function drawTimeline() {
  const box = byId("timeline");
  const points = todayPoints();
  const end = Date.now() / 1000;
  const start = Math.min(end, ...points.slice(0, 1).map((point) => point.recorded_at),
    ...lastScores.map((line) => line.session));
  if (end - start < 60) {
    box.replaceChildren(emptyNote("No messages from this doll today."));
    return;
  }

  const [left, right] = [70, 625];
  const xOf = (time) => left + (time - start) / (end - start) * (right - left);
  const noiseY = (db) => {
    const clamped = Math.min(Math.max(db, NOISE_MIN_DB), NOISE_MAX_DB);
    return 146 - (clamped - NOISE_MIN_DB) / (NOISE_MAX_DB - NOISE_MIN_DB) * 80;
  };
  const scoreY = (score) => 226 - score / 100 * 60;
  // A line or band across the whole width.
  const across = (y, stroke, more = {}) => {
    return svgPart("line", { x1: left, y1: y, x2: right, y2: y, stroke: stroke, ...more });
  };
  const bandRect = (top, bottom, fill) => {
    return svgPart("rect", { x: left, y: top, width: right - left, height: bottom - top, fill: fill });
  };
  const description = "Today's timeline for this doll: mode, noise in Quiet Study, reading scores "
    + "and help presses.";
  const chart = svgPart("svg", { class: "chart", viewBox: "0 0 640 262", role: "img",
                                 "aria-label": description });
  const segments = modeSegments(points, start, end);

  // Quiet Study shading across every lane.
  for (const segment of segments.filter((item) => item.mode === "quiet")) {
    chart.append(svgPart("rect", { x: xOf(segment.from), y: 16, width: xOf(segment.to) - xOf(segment.from),
                                   height: 214, fill: "var(--lm-purple)", "fill-opacity": 0.08 }));
  }

  // Lane names, noise bands and the Practise line.
  const lane = { "font-size": 11, "text-anchor": "end" };
  const red = { "font-size": 9.5, fill: "var(--jp-red)" };
  chart.append(
    svgText(left - 8, 39, "Mode", lane),
    svgText(left - 8, 110, "Noise", lane),
    svgText(left - 8, 200, "Score", lane),
    bandRect(noiseY(NOISE_MAX_DB), noiseY(TOO_LOUD_DB), "var(--jp-red-tint)"),
    bandRect(noiseY(TOO_LOUD_DB), noiseY(NOISY_DB), "color-mix(in srgb,var(--jp-warn) 14%,var(--jp-white))"),
    svgText(left + 4, noiseY(NOISE_MAX_DB) + 11, `Too loud · ${TOO_LOUD_DB}+ dB`, red),
    svgText(left + 4, noiseY(TOO_LOUD_DB) + 11, "A bit noisy", { "font-size": 9.5, fill: "var(--jp-warn)" }),
    across(noiseY(NOISE_MIN_DB), "var(--jp-line)"),
    across(scoreY(SCORE_PRACTISE), "var(--jp-red)", { "stroke-dasharray": "4 3" }),
    svgText(left + 4, scoreY(SCORE_PRACTISE) + 11, `Practise · ${SCORE_PRACTISE}`, red),
    across(scoreY(0), "var(--jp-grey-soft)"));

  // Help presses: drawn first, so blocks and dots stay on top of the line.
  for (const point of points.filter((item) => item.event_type === "help")) {
    const x = xOf(point.recorded_at);
    chart.append(
      svgPart("line", { x1: x, y1: 14, x2: x, y2: 230, stroke: "var(--jp-red)", "stroke-width": 1.5,
                        "stroke-dasharray": "4 3", "stroke-opacity": 0.8 }),
      svgText(Math.min(x, right), 11, `⚑ Help ${clockTime(point.recorded_at)}`, { "font-size": 10.5,
        "font-weight": 700, fill: "var(--jp-red)", "text-anchor": x > right - 60 ? "end" : "middle" }));
  }

  // Mode blocks, named when wide enough.
  for (const segment of segments.filter((item) => item.mode !== null)) {
    const mode = MODES[segment.mode];
    const x = xOf(segment.from);
    const width = xOf(segment.to) - x;
    const tooltip = `${mode.name} ${clockTime(segment.from)}–${clockTime(segment.to)}`;
    const block = { x: x, y: 26, width: Math.max(width - 1, 0.5), height: 20, rx: 3, fill: mode.color };
    chart.append(svgPart("rect", block, svgPart("title", {}, tooltip)));
    if (width > 50) {
      const color = segment.mode === "idle" ? "var(--jp-grey)" : "var(--jp-white)";
      chart.append(svgText(x + width / 2, 40, mode.short,
                           { "font-size": 10.5, fill: color, "text-anchor": "middle" }));
    }
  }

  // Noise reports: one stem and dot per report.
  const noise = points.filter((point) => point.event_type === "noise");
  for (const point of noise) {
    const x = xOf(point.recorded_at);
    const y = noiseY(point.data.db);
    const color = point.data.db >= TOO_LOUD_DB ? "var(--jp-red)" : "var(--jp-warn)";
    const tooltip = `${clockTime(point.recorded_at)} · ${Math.round(point.data.db)} dB`;
    chart.append(
      svgPart("line", { x1: x, y1: noiseY(NOISE_MIN_DB), x2: x, y2: y, stroke: "var(--jp-grey-soft)" }),
      svgPart("circle", { cx: x, cy: y, r: 4.5, fill: color, stroke: "var(--jp-white)", "stroke-width": 1.5 },
        svgPart("title", {}, tooltip)));
  }
  chart.append(svgText(left, 61, `${noise.length} noise reports in Quiet Study`, { "font-size": 10.5,
    fill: noise.length > 0 ? "var(--jp-red)" : "var(--jp-grey)", "font-weight": 700 }));

  // Scores: one dot per reading try, joined inside each session, session average over the last one.
  for (const session of sessionsOf(lastScores)) {
    const coords = session.lines.map((line) => {
      return `${xOf(line.recorded_at).toFixed(1)},${scoreY(line.score).toFixed(1)}`;
    });
    chart.append(svgPart("polyline", { points: coords.join(" "), fill: "none", stroke: "var(--jp-grey-soft)",
                                       "stroke-width": 1.5 }));
    for (const line of session.lines) {
      const dot = { cx: xOf(line.recorded_at), cy: scoreY(line.score), r: 4, fill: band(line.score).color };
      const tooltip = `${clockTime(line.recorded_at)} · p.${line.passage} try ${line.attempt} · `
        + percent(line.score);
      chart.append(svgPart("circle", dot, svgPart("title", {}, tooltip)));
    }
    const last = session.lines[session.lines.length - 1];
    const lastX = xOf(last.recorded_at);
    const nearEdge = lastX > right - 20;  // near the right edge the label goes left of the dot
    chart.append(svgText(nearEdge ? lastX - 8 : lastX, scoreY(last.score) - 7, percent(session.avg),
      { "font-weight": 700, fill: band(session.avg).color, "text-anchor": nearEdge ? "end" : "middle" }));
  }

  // Time axis: 4 ticks, the last one "now".
  for (let i = 0; i <= 3; i++) {
    const time = start + (end - start) * i / 3;
    const x = xOf(time);
    chart.append(svgPart("line", { x1: x, y1: 230, x2: x, y2: 234, stroke: "var(--jp-grey-soft)" }),
      svgText(x, 248, i === 3 ? "now" : clockTime(time),
              { "text-anchor": ["start", "middle", "middle", "end"][i] }));
  }
  box.replaceChildren(chart);
}

// ---- Summary and scores ----

function drawSummary() {
  const avg = average(lastScores);
  const avgCell = avg === null ? statCell("Avg score", "—")
    : statCell("Avg score", percent(avg), band(avg).color);
  if (view === "today") {
    const points = todayPoints();
    const end = Date.now() / 1000;
    const segments = modeSegments(points, points.length > 0 ? points[0].recorded_at : end, end);
    const minutes = (mode) => minutesText(segments.filter((segment) => segment.mode === mode)
      .reduce((sum, segment) => sum + segment.to - segment.from, 0));
    setText("summary-title", "Today at a glance");
    byId("summary").replaceChildren(
      statCell("Reading", minutes("reading")),
      statCell("Quiet Study", minutes("quiet")),
      statCell("Sessions", String(sessionsOf(lastScores).length)),
      statCell("Tries", String(lastScores.length)),
      avgCell,
      statCell("Help presses", String(points.filter((point) => point.event_type === "help").length)));
  } else {
    const classes = classesOf(lastScores);
    const sounds = soundsOf(lastScores);
    const change = classes.length > 1 ? changeText(classes[classes.length - 1].avg - classes[0].avg) : "—";
    setText("summary-title", "All classes at a glance");
    byId("summary").replaceChildren(
      statCell("Classes", String(classes.length)),
      statCell("Sessions", String(classes.reduce((sum, entry) => sum + entry.sessions, 0))),
      statCell("Tries", String(lastScores.length)),
      avgCell,
      statCell("First → latest", change),
      statCell("Weakest sound", sounds.length === 0 ? "—" : `/${sounds[0].ipa}/`));
  }
}

// The meter shows the latest session (Today) or class (Past); the timeline card shows Today's
// timeline or a chart of the past classes.
function drawLatest() {
  const today = view === "today";
  const steps = today ? sessionsOf(lastScores) : classesOf(lastScores);
  const latest = steps[steps.length - 1];
  setText("timeline-title", today ? "Today: mode, noise and score" : "Past classes: score per class");
  setText("timeline-note", today ? "Shaded = Quiet Study · noise is only measured there"
    : `${steps.length} classes`);
  if (today) {
    drawTimeline();
  } else if (steps.length === 0) {
    byId("timeline").replaceChildren(emptyNote("No saved classes yet."));
  } else {
    const points = steps.map((entry, index) => ({
      x: index,
      y: entry.avg,
      title: `${shortDay(entry.day)} · ${percent(entry.avg)} · ${entry.sessions} sessions`,
    }));
    const series = [{ name: `Doll ${dollId}`, color: DOLL_COLOR[dollId], points: points }];
    const days = steps.map((entry) => entry.day);
    byId("timeline").replaceChildren(dayChart(series, days, "Average score of each class day."));
  }

  byId("latest-gauge").replaceChildren(scoreGauge(latest === undefined ? null : latest.avg));
  if (latest === undefined) {
    setText("latest-label", today ? "Latest session" : "Latest class");
    setText("latest-text", today ? "No reading score yet today" : "No saved classes yet");
    return;
  }
  if (today) {
    const line = latest.lines[latest.lines.length - 1];
    const book = `book ${line.uid}, p.${line.passage}`;
    setText("latest-label", `Latest session · today ${clockTime(latest.start)} · ${book}`);
  } else {
    setText("latest-label", `Latest class · ${shortDay(latest.day)}`);
  }
  if (steps.length === 1) {
    setText("latest-text", `Reading score · ${latest.count} tries`);
  } else if (today) {
    const change = changeText(latest.avg - steps[steps.length - 2].avg);
    setText("latest-text", `Reading score · ${change} pts vs previous session`);
  } else {
    const change = changeText(latest.avg - steps[0].avg);
    setText("latest-text", `Reading score · ${change} pts since the first class`);
  }
}

// Did practising help: each word read 2+ times, first and latest read (score lines are oldest first).
// One row per word: "three   56% → 53%   ▼ 3 pts", then the sound to work on.
function drawPractice() {
  const words = groupBy(lastScores.flatMap((line) => line.words), (read) => read.word.toLowerCase())
    .filter((group) => group.items.length > 1)
    .map((group) => ({
      word: group.key,
      first: group.items[0].score,
      lastRead: group.items[group.items.length - 1],
      count: group.items.length,
    }))
    .sort((a, b) => a.lastRead.score - b.lastRead.score)
    .slice(0, 6);
  const rows = words.map((item) => {
    const last = item.lastRead.score;
    const change = last - item.first;
    const chipColor = change > 0 ? "var(--jp-ok)" : change < 0 ? "var(--jp-red)" : "var(--jp-grey-soft)";
    const stuck = last < SCORE_PRACTISE ? "Still stuck · " : "";
    const weak = weakSoundsText(soundsInWords([item.lastRead]));
    return el("div", { class: "practice-row" },
      el("span", { class: "word-name" }, item.word),
      el("span", { class: "scores" }, el("span", { class: "muted" }, percent(item.first)), " → ",
        el("b", { style: `color:${band(last).color}` }, percent(last))),
      el("span", { class: "change", style: `background:${chipColor}` },
        change === 0 ? "same" : `${changeText(change)} pts`),
      el("span", { class: "under" }, `${stuck}${weak} · ${item.count} reads`));
  });
  setText("practice-note", view === "today" ? "1st try today → latest" : "1st try ever → latest");
  byId("practice").replaceChildren(...(rows.length > 0 ? rows : [emptyNote("No word read twice yet.")]));
}

function drawScores(data) {
  lastScores = ofDoll(data.scores, dollId);
  drawLatest();
  drawSummary();
  setText("sounds-note", view === "today" ? "This doll today · weakest first"
    : "All classes · ▲▼ = latest vs first class");
  byId("sound-rows").replaceChildren(...soundRows(soundsOf(lastScores), 10));
  byId("word-rows").replaceChildren(...wordRows(wordsOf(lastScores), 8));
  drawPractice();
}

// ---- Start ----

watchNodes((nodes) => drawHeader(nodes), () => {
  setChip(byId("doll-state"), "No server", "var(--jp-grey-soft)", "dashed");
});

repeat(10000, async () => {
  lastHistory = await getJson("/api/dashboard/history?minutes=180");
  drawModeChanges(todayPoints());
  // both: the Today button is pressed AND today's scores have arrived (never past scores under "Today")
  if (view === "today" && document.body.dataset.view === "today") {
    drawTimeline();
    drawSummary();
  }
});

watchScores(drawScores);
