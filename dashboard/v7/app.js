// Overview page: fill every live part from the cloud server.
// Live: nest connection, light (lux), teacher distance, doll rows, help popup,
// mode in the last hour and recent activity.
// Scores: score trend, sounds (IPA), words and Improved, from the AI.
// Today = this class, Past = every saved class (button in the header).
// Shared helpers (chips, mode bars, score grouping, chart) are in shared.js.

const POLL_MS = 2000;
// Activity reads one hour of history, which is bigger, so it is fetched less often.
const ACTIVITY_POLL_MS = 10000;
const ACTIVITY_ROWS_SHOWN = 5;
const SCORE_POLL_MS = 10000;
const SOUNDS_SHOWN = 8;
const WORDS_SHOWN = 8;
// Teacher distance bar: 0 to 400 cm, near zone = the left half.
const TEACHER_BAR_CM = 400;

let view = "today";

// Light and teacher-distance limits drawn on the gauges.
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
    document.getElementById("teacher-cm").textContent = "— cm";
    document.getElementById("teacher-marker").style.visibility = "hidden";
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
  const distanceText = document.getElementById("teacher-cm");
  const marker = document.getElementById("teacher-marker");
  if (distance === null || distance === undefined) {
    teacher.textContent = "Teacher near nest: no reading";
    distanceText.textContent = "No reading";
    marker.style.visibility = "hidden";
    return;
  }
  if (distance < TEACHER_NEAR_CM) {
    teacher.textContent = `Teacher near nest: Yes${old}`;
  } else {
    teacher.textContent = `Teacher near nest: No${old}`;
  }
  distanceText.textContent = `${Math.round(distance)} cm${old}`;
  marker.style.left = `${Math.min(distance, TEACHER_BAR_CM) / TEACHER_BAR_CM * 100}%`;
  marker.style.visibility = "visible";
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
    row.querySelector(".doll-what").textContent = dollWhat(doll);
    setModeChip(row.querySelector(".doll-mode"), doll.activity);

    // Red row for "needs help" and for offline, as in the design.
    if (reason !== null || doll.status === "offline") {
      row.style.background = "var(--jp-red-tint)";
    }
    rows.push(row);
  }

  document.getElementById("doll-rows").replaceChildren(...rows);
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
// or the child pressing help. Heartbeats that repeat are skipped.
function drawActivity(history) {
  const changes = [];

  for (const dollId of DOLL_IDS) {
    let previous = null;

    for (const point of sortedPoints(history, dollId)) {
      if (point.event_type === "help") {
        changes.push({ time: point.recorded_at, dollId: dollId, text: " ⚑ asked for help", help: true });
        continue;
      }
      if (point.event_type === "noise") {
        continue;
      }
      if (point.event_type === "start") {
        let book = "a book";
        if (point.data.uid) {
          book = `book ${point.data.uid}`;
        }
        changes.push({ time: point.recorded_at, dollId: dollId, text: ` started reading ${book}` });
      } else if (point.activity !== previous && point.activity !== null) {
        changes.push({ time: point.recorded_at, dollId: dollId, text: ` → ${ACTIVITY_TEXT[point.activity] || point.activity}`, help: point.activity === "stopped" });
      }
      previous = point.activity;
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

// Score trend: Today = one point per reading session on the class clock,
// Past = one point per class day.
function drawTrend(scores) {
  const box = document.getElementById("trend-chart");
  const series = [];
  let xMin = Infinity;
  let xMax = -Infinity;
  let xLabels = [];

  if (view === "today") {
    for (const dollId of DOLL_IDS) {
      const points = sessionsOf(scores, dollId).map((session) => ({
        x: session.start,
        y: session.avg,
        title: `Doll ${dollId} · ${clockTime(session.start)}–${clockTime(session.end)} · ${Math.round(session.avg)}% · ${session.count} tries`,
      }));
      for (const point of points) {
        xMin = Math.min(xMin, point.x);
      }
      series.push({ name: `Doll ${dollId}`, color: DOLL_COLOR[dollId], points: points });
    }
    // The class clock runs from the first session to now.
    xMax = Date.now() / 1000;
    xLabels = [{ x: xMin, text: clockTime(xMin) }, { x: (xMin + xMax) / 2, text: clockTime((xMin + xMax) / 2) }, { x: xMax, text: "now" }];
    document.getElementById("trend-note").textContent = Number.isFinite(xMin) ? `Session average · class started ${clockTime(xMin)}` : "Session average";
  } else {
    const days = classesOf(scores, null).map((entry) => entry.day);
    for (const dollId of DOLL_IDS) {
      const points = classesOf(scores, dollId).map((entry) => ({
        x: days.indexOf(entry.day),
        y: entry.avg,
        title: `Doll ${dollId} · ${shortDay(entry.day)} · ${Math.round(entry.avg)}% · ${entry.sessions} sessions`,
      }));
      series.push({ name: `Doll ${dollId}`, color: DOLL_COLOR[dollId], points: points });
    }
    xMin = 0;
    xMax = days.length - 1;
    // At most 6 date labels, so they never overlap.
    const step = Math.max(1, Math.ceil(days.length / 6));
    xLabels = days.map((day, index) => ({ x: index, text: shortDay(day) })).filter((item, index) => index % step === 0 || index === days.length - 1);
    document.getElementById("trend-note").textContent = `Class average per day · ${days.length} classes`;
  }

  if (scores.length === 0 || !Number.isFinite(xMin)) {
    box.replaceChildren(emptyNote(view === "today" ? "No reading scores in this class yet." : "No saved classes yet."));
    return;
  }
  const label = view === "today" ? "Average score of each reading session today, one line per doll." : "Average score of each class day, one line per doll.";
  box.replaceChildren(scoreChart(series, xMin, xMax, xLabels, label));
}

// Improved: latest session (or class) minus the first, averaged over dolls with two or more.
function drawImproved(scores) {
  document.getElementById("improved-label").textContent = view === "today" ? "Improved today" : "Improved overall";
  const changes = [];
  for (const dollId of DOLL_IDS) {
    const steps = view === "today" ? sessionsOf(scores, dollId) : classesOf(scores, dollId);
    if (steps.length > 1) {
      changes.push(steps[steps.length - 1].avg - steps[0].avg);
    }
  }
  const number = document.getElementById("improved");
  const text = document.getElementById("improved-text");
  if (changes.length === 0) {
    number.textContent = "—";
    number.style.color = "var(--jp-grey-soft)";
    text.textContent = view === "today" ? "needs 2 sessions on a doll" : "needs 2 saved classes";
    return;
  }
  const change = changes.reduce((sum, value) => sum + value, 0) / changes.length;
  number.textContent = `${change >= 0 ? "+" : "−"}${Math.abs(Math.round(change))}`;
  number.style.color = change >= 0 ? "var(--jp-ok)" : "var(--jp-red)";
  text.textContent = view === "today" ? "pts overall today · first → latest session" : "pts all time · first → latest class";
}

// Past classes: light and teacher distance against each session's score.
function drawRoomPast(scores, room) {
  const light = [];
  const teacher = [];
  for (const dollId of DOLL_IDS) {
    for (const session of sessionsOf(scores, dollId)) {
      const when = `Doll ${dollId} · ${shortDay(dayOf(session.start))} ${clockTime(session.start)} · ${Math.round(session.avg)}%`;
      const lux = roomDuring(room, session, "lux");
      const distance = roomDuring(room, session, "distance_cm");
      if (lux !== null) {
        light.push({ x: lux, y: session.avg, title: `${when} · ${Math.round(lux)} lx` });
      }
      if (distance !== null) {
        teacher.push({ x: distance, y: session.avg, title: `${when} · ${Math.round(distance)} cm` });
      }
    }
  }
  document.getElementById("light-scatter").replaceChildren(scatterChart(light, 0, 1200, " lx", "Session score against room light."));
  document.getElementById("light-link").textContent = linkText(correlation(light), "more light", "less light");
  document.getElementById("teacher-scatter").replaceChildren(scatterChart(teacher, 0, 400, " cm", "Session score against teacher distance from the nest."));
  document.getElementById("teacher-link").textContent = linkText(correlation(teacher), "teacher further", "teacher nearer");
}

// Past: one row per class day, newest first: date, sessions, tries, average, change, weakest sound.
function drawClassHistory(scores) {
  const classes = classesOf(scores, null);
  const rows = [];
  classes.forEach((entry, index) => {
    const row = document.createElement("div");
    row.style.cssText = "display:grid;grid-template-columns:64px 1fr 48px 46px;gap:2px 8px;align-items:baseline;padding:7px 0;border-bottom:var(--jp-border)";
    const day = document.createElement("b");
    day.textContent = shortDay(entry.day);
    const counts = document.createElement("span");
    counts.style.color = "var(--jp-grey)";
    counts.textContent = `${entry.sessions} sessions · ${entry.count} tries`;
    const avg = document.createElement("b");
    avg.style.cssText = `text-align:right;color:${bandColor(entry.avg)}`;
    avg.textContent = `${Math.round(entry.avg)}%`;
    const change = document.createElement("span");
    change.style.cssText = "text-align:right;font-size:12px";
    if (index > 0) {
      const difference = entry.avg - classes[index - 1].avg;
      change.textContent = changeText(difference);
      change.style.color = difference >= 0 ? "var(--jp-ok)" : "var(--jp-red)";
    }
    const weakest = soundsOf(entry.lines, null)[0];
    const note = document.createElement("span");
    note.style.cssText = "grid-column:2 / 5;font-size:11.5px;color:var(--jp-grey)";
    note.textContent = weakest === undefined ? "" : `Weakest sound /${weakest.ipa}/ ${Math.round(weakest.avg)}%`;
    row.append(day, counts, avg, change, note);
    rows.push(row);
  });
  rows.reverse();
  document.getElementById("history-rows").replaceChildren(...(rows.length === 0 ? [emptyNote("No saved classes yet.")] : rows));
}

function drawScores(data) {
  const scores = data.scores;
  drawTrend(scores);
  drawImproved(scores);
  document.getElementById("mode-card").style.display = view === "today" ? "block" : "none";
  document.getElementById("activity-card").style.display = view === "today" ? "block" : "none";
  document.getElementById("history-card").style.display = view === "today" ? "none" : "flex";
  if (view === "past") {
    drawClassHistory(scores);
  }
  document.getElementById("room-live").style.display = view === "today" ? "block" : "none";
  document.getElementById("room-past").style.display = view === "today" ? "none" : "block";
  if (view === "past") {
    drawRoomPast(scores, data.room);
  }

  const classScore = average(scores);
  const big = document.getElementById("class-score");
  const small = document.getElementById("class-score-text");
  if (classScore === null) {
    big.textContent = "—";
    big.style.color = "var(--jp-grey-soft)";
    small.textContent = "No scores yet";
  } else {
    big.textContent = `${Math.round(classScore)}%`;
    big.style.color = bandColor(classScore);
    const perDoll = DOLL_IDS.map((dollId) => {
      const value = average(scoresOf(scores, dollId));
      return `Doll ${dollId} · ${value === null ? "—" : `${Math.round(value)}%`}`;
    });
    small.textContent = `Class score · ${bandName(classScore)} · ${perDoll.join(" | ")}`;
  }

  document.getElementById("sounds-note").textContent = view === "today" ? "Class average · weakest first" : "All classes · ▲▼ = latest vs first class";
  document.getElementById("sound-rows").replaceChildren(...soundRows(soundsOf(scores, null), SOUNDS_SHOWN));
  document.getElementById("word-rows").replaceChildren(...wordRows(wordsOf(scores, null), WORDS_SHOWN));
}

// Past is only read when the button is pressed; Today is read again every 10 s.
async function loadScores() {
  const asked = view;
  try {
    const data = await getJson(`/api/dashboard/scores?day=${asked}`);
    if (asked === view) {
      drawScores(data);
    }
  } catch (error) {
    console.warn("score poll failed:", error);
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
    drawNest(nodes.find((node) => node.node_id === 0));
    drawDolls(nodes);
    drawHelpPopup(nodes);
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

setupViewButtons((chosen) => {
  view = chosen;
  loadScores();
});

poll();
pollActivity();
pollScores();
