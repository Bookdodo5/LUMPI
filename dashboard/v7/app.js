// Overview page (index.html).
// Live: nest, light, teacher distance, doll rows, mode in the last hour, recent activity.
// Scores, Today = this class or Past = every saved class: trend, class score, sounds, words, improved,
// class by class, and light / teacher distance against the score.

// Light gauge: lux break points and the zone between each two. The marker angle runs from 180°
// (far left) to 0° (far right), 36° per zone, matching the coloured arcs in index.html.
const LUX_STEPS = [0, 80, 120, 980, 1020, 2000];
const LUX_ZONES = ["Too dark", "A bit dark", "Normal", "A bit bright", "Too bright"];
const TEACHER_NEAR_CM = 200;  // the bar runs from 0 to 400 cm; near = the left half

// Dolls that need help come first, then dolls with a connection problem.
const ROW_ORDER = { help: 0, offline: 1, stale: 2, unknown: 3, online: 4 };

// ---- Live: room and dolls ----

function lightPlace(lux) {
  let i = 1;
  while (i < LUX_STEPS.length - 1 && lux > LUX_STEPS[i]) {
    i++;
  }
  const part = Math.min(1, (lux - LUX_STEPS[i - 1]) / (LUX_STEPS[i] - LUX_STEPS[i - 1]));
  return { angle: (180 - 36 * (i - 1 + part)) * Math.PI / 180, zone: LUX_ZONES[i - 1] };
}

// Light and teacher distance from the nest. Old values say "(old)", missing ones say so in
// words, so a failed sensor or a silent nest never looks like a live reading.
function drawRoom(nest) {
  const known = nest.status !== "unknown";
  // a reading the nest left out counts as missing, like null
  const lux = known && sent(nest.values.lux) ? nest.values.lux : null;
  const distance = known && sent(nest.values.distance_cm) ? nest.values.distance_cm : null;
  const old = nest.status === "online" ? "" : " (old)";
  const seen = `${nest.status === "online" ? "Updated" : "Old ·"} ${clockTime(nest.last_seen)}`;
  setText("light-seen", known ? seen : "No reading yet");

  const marker = byId("light-marker");
  marker.setAttribute("visibility", lux === null ? "hidden" : "visible");
  if (lux === null) {
    setText("light-zone", known ? "No reading" : "Waiting");
    setText("light-lux", "— lx");
  } else {
    const place = lightPlace(lux);
    // gauge centre (130, 140), radius 102
    marker.setAttribute("cx", (130 + 102 * Math.cos(place.angle)).toFixed(1));
    marker.setAttribute("cy", (140 - 102 * Math.sin(place.angle)).toFixed(1));
    setText("light-zone", place.zone);
    setText("light-lux", `${Math.round(lux)} lx`);
  }

  const bar = byId("teacher-marker");
  bar.style.visibility = distance === null ? "hidden" : "visible";
  if (!known) {
    setText("teacher-near", "Teacher near nest: —");
    setText("teacher-cm", "— cm");
  } else if (distance === null) {
    setText("teacher-near", "Teacher near nest: no reading");
    setText("teacher-cm", "No reading");
  } else {
    setText("teacher-near", `Teacher near nest: ${distance < TEACHER_NEAR_CM ? "Yes" : "No"}${old}`);
    setText("teacher-cm", `${Math.round(distance)} cm${old}`);
    bar.style.left = `${Math.min(distance, 400) / 400 * 100}%`;
  }
}

// One doll row: name, book / page / try and activity, mode chip, state chip.
function dollRow(doll) {
  const mode = el("span", { class: "chip" });
  const state = el("span", { class: "chip state" });
  setModeChip(mode, doll);
  setStateChip(state, doll);
  let what = "Waiting for the first message";
  if (doll.status !== "unknown") {
    const attempt = sent(doll.values.attempt) ? [`try ${doll.values.attempt}`] : [];
    const activity = ACTIVITY_TEXT[doll.activity] || "No activity yet";
    what = [...bookParts(doll.values), ...attempt, activity].join(" · ");
  }
  const row = el("a", { class: "row doll-row", href: `/doll.html?doll=${doll.node_id}` },
    el("div", {},
      el("div", { class: "doll-name" }, logo(), `Doll ${doll.node_id}`),
      el("div", { class: "doll-what" }, what)),
    mode, state, el("span", { class: "chevron" }, "›"));
  if (doll.needs_help || doll.status === "offline") {
    row.style.background = "var(--jp-red-tint)";  // red row, as in the design
  }
  return row;
}

function drawDolls(nodes) {
  const order = (doll) => (doll.needs_help ? ROW_ORDER.help : ROW_ORDER[doll.status]);
  const dolls = DOLL_IDS.map((id) => nodes[id]).sort((a, b) => order(a) - order(b) || a.node_id - b.node_id);
  byId("doll-rows").replaceChildren(...dolls.map(dollRow));
}

// ---- Live: mode bars and recent activity (last hour) ----

// One bar of blocks per doll, like the "Mode today" wireframe; wide blocks say "Quiet 15m".
function modeBar(segments) {
  const total = segments.reduce((sum, segment) => sum + (segment.to - segment.from), 0);
  return el("div", { class: "mode-bar" }, ...segments.map((segment) => {
    const seconds = segment.to - segment.from;
    const range = `${clockTime(segment.from)}–${clockTime(segment.to)}`;
    if (segment.mode === null) {
      return el("div", { class: "no-data", style: `flex:${seconds}`, title: `No data ${range}` });
    }
    const mode = MODES[segment.mode];
    const color = segment.mode === "idle" ? "var(--jp-grey)" : "var(--jp-white)";
    const block = el("div", { style: `flex:${seconds};background:${mode.color};color:${color}`,
                              title: `${mode.name} ${range}` });
    if (seconds / total > 0.28) {
      block.textContent = `${mode.short} ${Math.round(seconds / 60)}m`;
    }
    return block;
  }));
}

function drawModes(history) {
  const start = history.now - history.minutes * 60;
  const end = history.now;
  const grid = el("div", { class: "mode-grid" });
  for (const dollId of DOLL_IDS) {
    const segments = modeSegments(sortedPoints(history, dollId), start, end);
    grid.append(el("b", {}, `Doll ${dollId}`), modeBar(segments));
  }
  const axis = el("div", { class: "time-axis" },
    el("span", {}, clockTime(start)),
    el("span", {}, clockTime((start + end) / 2)),
    el("span", {}, "now"));
  grid.append(el("span"), axis);
  byId("mode-rows").replaceChildren(grid);
}

// Only changes are listed: a book put on the mat, a new activity, or a help press.
function drawActivity(history) {
  const changes = [];
  for (const dollId of DOLL_IDS) {
    let previous = null;
    for (const point of sortedPoints(history, dollId)) {
      const add = (text, alert) => {
        changes.push({ time: point.recorded_at, dollId: dollId, text: text, alert: alert });
      };
      if (point.event_type === "help") {
        add(" ⚑ asked for help", true);
        continue;
      }
      if (point.event_type === "noise") {
        continue;
      }
      if (point.event_type === "start") {
        add(` started reading ${point.data.uid ? `book ${point.data.uid}` : "a book"}`, false);
      } else if (point.activity !== previous && point.activity !== null) {
        add(` → ${ACTIVITY_TEXT[point.activity] || point.activity}`, point.activity === "stopped");
      }
      previous = point.activity;
    }
  }
  changes.sort((a, b) => b.time - a.time);
  const rows = changes.slice(0, 5).map((change) => el("a",
    { class: "row activity-row", href: `/doll.html?doll=${change.dollId}` },
    el("span", { class: "time" }, clockTime(change.time)),
    el("span", { class: change.alert ? "alert" : "" }, el("b", {}, `Doll ${change.dollId}`), change.text)));
  if (rows.length === 0) {
    rows.push(emptyNote("No doll activity in the last hour."));
  }
  byId("activity-rows").replaceChildren(...rows);
}

// ---- Scores ----

// Score trend: Today = one point per reading session on the class clock, Past = one point per class day.
function drawTrend(scores) {
  const box = byId("trend-chart");
  if (view === "today") {
    const series = DOLL_IDS.map((dollId) => ({
      name: `Doll ${dollId}`,
      color: DOLL_COLOR[dollId],
      points: sessionsOf(ofDoll(scores, dollId)).map((session) => ({
        x: session.start,
        y: session.avg,
        title: `Doll ${dollId} · ${clockTime(session.start)}–${clockTime(session.end)} · `
          + `${percent(session.avg)} · ${session.count} tries`,
      })),
    }));
    // The class started with the first session (Infinity when there is none).
    const start = Math.min(...series.flatMap((line) => line.points.map((point) => point.x)));
    if (!Number.isFinite(start)) {
      setText("trend-note", "Session average");
      box.replaceChildren(emptyNote("No reading scores in this class yet."));
      return;
    }
    // The class clock runs from the first session to now.
    const end = Date.now() / 1000;
    const middle = (start + end) / 2;
    const labels = [{ x: start, text: clockTime(start) }, { x: middle, text: clockTime(middle) },
                    { x: end, text: "now" }];
    setText("trend-note", `Session average · class started ${clockTime(start)}`);
    const description = "Average score of each reading session today, one line per doll.";
    box.replaceChildren(scoreChart(series, start, end, labels, description));
  } else {
    const days = classesOf(scores).map((entry) => entry.day);
    const series = DOLL_IDS.map((dollId) => ({
      name: `Doll ${dollId}`,
      color: DOLL_COLOR[dollId],
      points: classesOf(ofDoll(scores, dollId)).map((entry) => ({
        x: days.indexOf(entry.day),
        y: entry.avg,
        title: `Doll ${dollId} · ${shortDay(entry.day)} · ${percent(entry.avg)} · ${entry.sessions} sessions`,
      })),
    }));
    setText("trend-note", `Class average per day · ${days.length} classes`);
    box.replaceChildren(scores.length === 0 ? emptyNote("No saved classes yet.")
      : dayChart(series, days, "Average score of each class day, one line per doll."));
  }
}

// Improved: latest session (or class) minus the first, averaged over the dolls that have two or more.
function drawImproved(scores) {
  const today = view === "today";
  setText("improved-label", today ? "Improved today" : "Improved overall");
  const changes = [];
  for (const dollId of DOLL_IDS) {
    const steps = today ? sessionsOf(ofDoll(scores, dollId)) : classesOf(ofDoll(scores, dollId));
    if (steps.length > 1) {
      changes.push(steps[steps.length - 1].avg - steps[0].avg);
    }
  }
  const number = byId("improved");
  if (changes.length === 0) {
    number.textContent = "—";
    number.style.color = "var(--jp-grey-soft)";
    setText("improved-text", today ? "needs 2 sessions on a doll" : "needs 2 saved classes");
    return;
  }
  const change = mean(changes, (value) => value);
  number.textContent = `${change >= 0 ? "+" : "−"}${Math.abs(Math.round(change))}`;
  number.style.color = change >= 0 ? "var(--jp-ok)" : "var(--jp-red)";
  setText("improved-text", today ? "pts overall today · first → latest session"
    : "pts all time · first → latest class");
}

function drawClassScore(scores) {
  const big = byId("class-score");
  const classScore = average(scores);
  if (classScore === null) {
    big.textContent = "—";
    big.style.color = "var(--jp-grey-soft)";
    setText("class-score-text", "No scores yet");
    return;
  }
  big.textContent = percent(classScore);
  big.style.color = band(classScore).color;
  const perDoll = DOLL_IDS.map((dollId) => {
    const value = average(ofDoll(scores, dollId));
    return `Doll ${dollId} · ${value === null ? "—" : percent(value)}`;
  });
  setText("class-score-text", `Class score · ${band(classScore).name} · ${perDoll.join(" | ")}`);
}

// Past: one row per class day, newest first: date, sessions, tries, average, change, weakest sound.
function drawClassHistory(scores) {
  const classes = classesOf(scores);
  const rows = classes.map((entry, index) => {
    const change = el("span", { class: "right change" });
    if (index > 0) {
      const difference = entry.avg - classes[index - 1].avg;
      change.textContent = changeText(difference);
      change.style.color = difference >= 0 ? "var(--jp-ok)" : "var(--jp-red)";
    }
    const weakest = soundsOf(entry.lines)[0];
    const weakText = weakest === undefined ? "" : `Weakest sound /${weakest.ipa}/ ${percent(weakest.avg)}`;
    return el("div", { class: "class-row" },
      el("b", {}, shortDay(entry.day)),
      el("span", { class: "muted" }, `${entry.sessions} sessions · ${entry.count} tries`),
      el("b", { class: "right", style: `color:${band(entry.avg).color}` }, percent(entry.avg)),
      change,
      el("span", { class: "under" }, weakText));
  });
  byId("history-rows").replaceChildren(...(rows.length > 0 ? rows.reverse()
    : [emptyNote("No saved classes yet.")]));
}

// Past: light and teacher distance against each session's score.
function drawRoomPast(scores, room) {
  const light = [];
  const teacher = [];
  for (const dollId of DOLL_IDS) {
    for (const session of sessionsOf(ofDoll(scores, dollId))) {
      const date = `${shortDay(dayOf(session.start))} ${clockTime(session.start)}`;
      const when = `Doll ${dollId} · ${date} · ${percent(session.avg)}`;
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
  byId("light-scatter").replaceChildren(scatterChart(light, 1200, " lx",
    "Session score against room light."));
  setText("light-link", linkText(correlation(light), "more light", "less light"));
  byId("teacher-scatter").replaceChildren(scatterChart(teacher, 400, " cm",
    "Session score against teacher distance from the nest."));
  setText("teacher-link", linkText(correlation(teacher), "teacher further", "teacher nearer"));
}

// Average of one room value during a session (null when the nest sent none then).
function roomDuring(room, session, key) {
  const values = room.filter((line) => line[key] !== null
    && line.recorded_at >= session.start && line.recorded_at <= session.end + 60);
  return values.length === 0 ? null : mean(values, (line) => line[key]);
}

// Pearson correlation of [{x, y}], or null with fewer than 3 points or no spread.
function correlation(points) {
  if (points.length < 3) {
    return null;
  }
  const mx = mean(points, (p) => p.x);
  const my = mean(points, (p) => p.y);
  let top = 0;
  let sx = 0;
  let sy = 0;
  for (const p of points) {
    top += (p.x - mx) * (p.y - my);
    sx += (p.x - mx) ** 2;
    sy += (p.y - my) ** 2;
  }
  return sx === 0 || sy === 0 ? null : top / Math.sqrt(sx * sy);
}

// upWords = what a positive r means, downWords = what a negative r means.
function linkText(r, upWords, downWords) {
  if (r === null) {
    return "Needs 3+ sessions to compare";
  }
  const size = Math.abs(r);
  if (size < 0.2) {
    return `r = ${r.toFixed(2)} · no clear link`;
  }
  const strength = size >= 0.5 ? "strong" : "weak";
  return `r = ${r.toFixed(2)} · ${strength} link: ${r > 0 ? upWords : downWords} → higher score`;
}

// Scatter of session score (y, 0-100) against a room value (x, 0 to xMax). points = [{x, y, title}].
function scatterChart(points, xMax, unit, description) {
  const [left, right, top, bottom] = [30, 300, 8, 110];
  const xOf = (x) => left + Math.min(Math.max(x, 0), xMax) / xMax * (right - left);
  const yOf = (y) => bottom - y / 100 * (bottom - top);
  const chart = svgPart("svg", { class: "scatter", viewBox: "0 0 310 130", role: "img",
                                 "aria-label": description });
  for (const value of [0, 50, 100]) {
    chart.append(
      svgPart("line", { x1: left, y1: yOf(value), x2: right, y2: yOf(value), stroke: "var(--jp-line)" }),
      svgText(left - 4, yOf(value) + 4, String(value), { "font-size": 9, "text-anchor": "end" }));
  }
  chart.append(svgPart("line", { x1: left, y1: yOf(SCORE_PRACTISE), x2: right, y2: yOf(SCORE_PRACTISE),
                                 stroke: "var(--jp-red)", "stroke-dasharray": "4 3" }));
  ["start", "middle", "end"].forEach((anchor, index) => {
    const value = xMax * index / 2;
    const text = `${Math.round(value)}${unit}`;
    chart.append(svgText(xOf(value), 124, text, { "font-size": 9, "text-anchor": anchor }));
  });
  for (const p of points) {
    const dot = { cx: xOf(p.x), cy: yOf(p.y), r: 4, fill: band(p.y).color, "fill-opacity": 0.8 };
    chart.append(svgPart("circle", dot, svgPart("title", {}, p.title)));
  }
  return chart;
}

function drawScores(data) {
  const scores = data.scores;
  drawTrend(scores);
  drawImproved(scores);
  drawClassScore(scores);
  if (view === "past") {
    drawClassHistory(scores);
    drawRoomPast(scores, data.room);
  }
  setText("sounds-note", view === "today" ? "Class average · weakest first"
    : "All classes · ▲▼ = latest vs first class");
  byId("sound-rows").replaceChildren(...soundRows(soundsOf(scores), 8));
  byId("word-rows").replaceChildren(...wordRows(wordsOf(scores), 8));
}

// ---- Start ----

watchNodes((nodes) => {
  drawRoom(nodes[0]);
  drawDolls(nodes);
}, () => {
  setText("light-seen", "Not current");
  // Without the cloud no doll's state is known, so never show OK.
  for (const chip of document.querySelectorAll("#doll-rows .state")) {
    setChip(chip, "No server", "var(--jp-grey-soft)", "dashed");
  }
});

repeat(10000, async () => {
  const history = await getJson("/api/dashboard/history?minutes=60");
  drawModes(history);
  drawActivity(history);
});

watchScores(drawScores);
