// Shared by the Overview page (app.js) and the Doll page (doll.js).
// All text goes in with textContent, so data from the network cannot add HTML.

const DOLL_IDS = [1, 2];

const ACTIVITY_TEXT = {
  idle: "Idle",
  book_on: "Book on the mat",
  recording: "Recording",
  waiting_ai: "Waiting for AI",
  feedback: "Giving feedback",
  quiet_study: "Quiet study",
  stopped: "Stopped (STOP button)",
};

// Mode = what the child chose on the doll. Reading = ink, Quiet Study = purple, Idle = line colour.
const READING_ACTIVITIES = ["book_on", "recording", "waiting_ai", "feedback"];
const MODE_NAME = { reading: "Reading", quiet: "Quiet Study", idle: "Idle" };
const MODE_SHORT = { reading: "Reading", quiet: "Quiet", idle: "Idle" };
const MODE_COLOR ={ reading: "var(--jp-black)", quiet: "var(--lm-purple)", idle: "var(--jp-line)" };

// Same as the offline time in nodes() in api/lumpi.py. A longer gap between
// two messages is drawn as "no data", not as the mode before it.
const NO_DATA_AFTER_S = 60;

// A doll "needs help" for this long after the child presses help on the doll.
const HELP_SHOWN_S = 300;

// Score bands from the wireframes: Practise under 60, Close 60-84, Good 85+.
const SCORE_GOOD = 85;
const SCORE_PRACTISE = 60;
// Line colours per doll, as in the v6 Score trend.
const DOLL_COLOR = { 1: "var(--jp-chart-2)", 2: "var(--jp-chart-1)", 3: "var(--jp-chart-3)" };

function clockTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: false });
}

function dayOf(unixSeconds) {
  // en-CA writes dates as YYYY-MM-DD, the same as the server's file names.
  return new Date(unixSeconds * 1000).toLocaleDateString("en-CA");
}

function shortDay(day) {
  return new Date(`${day}T12:00:00`).toLocaleDateString([], { day: "numeric", month: "short" });
}

function minutesText(seconds) {
  return `${Math.round(seconds / 60)} min`;
}

function modeOf(activity) {
  if (activity === "quiet_study") {
    return "quiet";
  }
  if (READING_ACTIVITIES.includes(activity)) {
    return "reading";
  }
  return "idle";
}

// Why this doll needs the teacher, or null. Offline dolls show "Last seen" instead.
function helpReason(doll) {
  if (doll.status !== "online" && doll.status !== "stale") {
    return null;
  }
  if (typeof doll.help_age_s === "number" && doll.help_age_s < HELP_SHOWN_S) {
    return `Asked for help at ${clockTime(doll.help_at)}`;
  }
  return null;
}

// Help popup: appears when a child presses help on a doll. OK only hides it
// on this screen; the dashboard never sends anything back to the doll.
const helpSeen = {};

function drawHelpPopup(nodes) {
  let popup = document.getElementById("help-popup");
  if (popup === null) {
    popup = document.createElement("div");
    popup.id = "help-popup";
    popup.setAttribute("role", "alert");
    popup.style.cssText = "font-family:var(--jp-font-body);color:var(--jp-black);position:fixed;right:20px;bottom:50px;z-index:10;min-width:280px;max-width:360px;background:var(--jp-white);border:2px solid var(--jp-red);border-radius:var(--jp-radius);box-shadow:0 6px 24px rgba(35,26,18,.25);padding:14px 16px;display:none";
    document.body.append(popup);
  }

  const pending = nodes.filter((node) => DOLL_IDS.includes(node.node_id) && helpReason(node) !== null && node.help_at !== helpSeen[node.node_id]);
  if (pending.length === 0) {
    popup.style.display = "none";
    return;
  }

  const title = document.createElement("div");
  title.style.cssText = "font-family:var(--jp-font-display);font-size:18px;font-weight:700;color:var(--jp-red);margin-bottom:6px";
  title.textContent = "⚑ A child needs help";
  const items = [title];

  for (const doll of pending) {
    const line = document.createElement("a");
    line.className = "row";
    line.href = `/doll.html?doll=${doll.node_id}`;
    line.style.cssText = "display:block;padding:6px 4px;border-bottom:var(--jp-border);font-size:13px";
    const name = document.createElement("b");
    name.textContent = `Doll ${doll.node_id}`;
    const parts = [` · asked at ${clockTime(doll.help_at)}`];
    if (doll.values && doll.values.uid) {
      parts.push(`Book ${doll.values.uid}`);
    }
    if (doll.values && doll.values.passage !== null && doll.values.passage !== undefined) {
      parts.push(`p.${doll.values.passage}`);
    }
    line.append(name, parts.join(" · "));
    items.push(line);
  }

  const ok = document.createElement("button");
  ok.type = "button";
  ok.textContent = "OK, I am going";
  ok.style.cssText = "margin-top:10px;font:inherit;font-weight:500;letter-spacing:.06em;min-height:40px;width:100%;border:0;border-radius:var(--jp-radius);background:var(--jp-red);color:var(--jp-white);cursor:pointer";
  ok.addEventListener("click", () => {
    for (const doll of pending) {
      helpSeen[doll.node_id] = doll.help_at;
    }
    popup.style.display = "none";
  });
  items.push(ok);

  popup.replaceChildren(...items);
  popup.style.display = "block";
}

function setChip(chip, text, color, filled, dashed) {
  chip.textContent = text;
  chip.style.background = filled ? color : "transparent";
  chip.style.color = filled ? "var(--jp-white)" : color;
  chip.style.border = `1px ${dashed ? "dashed" : "solid"} ${color}`;
}

function setModeChip(chip, activity) {
  const mode = modeOf(activity);
  setChip(chip, MODE_NAME[mode], MODE_COLOR[mode], true, false);
  if (mode === "idle") {
    chip.style.color = "var(--jp-black)";
  }
}

// State chip: needs help, OK, no message for a while, or offline.
function setStateChip(chip, doll) {
  if (doll.status === "unknown") {
    setChip(chip, "Waiting", "var(--jp-grey-soft)", false, true);
  } else if (helpReason(doll) !== null) {
    setChip(chip, "⚑ Needs help", "var(--jp-red)", true, false);
  } else if (doll.status === "online") {
    setChip(chip, "✓ OK", "var(--jp-ok)", false, false);
  } else if (doll.status === "stale") {
    // Not "quiet": that word belongs to Quiet Study mode.
    setChip(chip, `! No msg ${Math.round(doll.age_s)} s`, "var(--jp-warn)", false, false);
  } else {
    setChip(chip, `Last seen ${clockTime(doll.last_seen)}`, "var(--jp-red)", false, true);
  }
}

// Header dot and text for the nest. Returns false when there is no live nest reading.
function drawNestStatus(nest, dot, text) {
  let color = "var(--jp-ok)";
  let outline = false;
  let words = "Nest and cloud connected";

  if (nest === undefined || nest.status === "unknown") {
    color = "var(--jp-grey-soft)";
    outline = true;
    words = "Laptop connected · waiting for the nest";
  } else if (nest.status === "stale") {
    color = "var(--jp-warn)";
    outline = true;
    words = `No message from the nest for ${Math.round(nest.age_s)} s`;
  } else if (nest.status === "offline") {
    color = "var(--jp-red)";
    words = `Nest offline since ${clockTime(nest.last_seen)}`;
  }

  dot.style.background = outline ? "transparent" : color;
  dot.style.border = outline ? `2px solid ${color}` : "0";
  text.textContent = words;
}

function drawServerDown(dot, text) {
  dot.style.background = "var(--jp-red)";
  dot.style.border = "0";
  text.textContent = "Laptop server not reachable · retrying";
}

async function getJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${path} returned ${response.status}`);
  }
  return response.json();
}

// History points of one doll, sorted by the time the doll measured them
// (late messages arrive last).
function sortedPoints(history, dollId) {
  const points = (history.nodes[String(dollId)] || []).slice();
  points.sort((a, b) => a.recorded_at - b.recorded_at);
  return points;
}

// Cut [start, end] into parts: [{mode, from, to}]. mode is null where no message came.
function modeSegments(allPoints, start, end) {
  // Help and noise messages carry no mode, so they must not draw a block.
  const points = allPoints.filter((point) => point.event_type !== "help" && point.event_type !== "noise");
  const segments = [];
  let cursor = start;

  function add(mode, from, to) {
    const last = segments[segments.length - 1];
    if (last !== undefined && last.mode === mode && last.to === from) {
      last.to = to;
    } else {
      segments.push({ mode: mode, from: from, to: to });
    }
  }

  for (let i = 0; i < points.length; i++) {
    const at = Math.max(start, Math.min(points[i].recorded_at, end));
    let next = end;
    if (i + 1 < points.length) {
      next = Math.min(points[i + 1].recorded_at, end);
    }
    const stop = Math.max(at, Math.min(next, at + NO_DATA_AFTER_S));

    if (at > cursor) {
      add(null, cursor, at);
    }
    if (stop > at) {
      add(modeOf(points[i].activity), at, stop);
    }
    cursor = Math.max(cursor, stop);
  }

  if (cursor < end) {
    add(null, cursor, end);
  }
  return segments;
}

// Seconds spent in each mode.
function modeTotals(segments) {
  const totals = { reading: 0, quiet: 0, idle: 0 };
  for (const segment of segments) {
    if (segment.mode !== null) {
      totals[segment.mode] += segment.to - segment.from;
    }
  }
  return totals;
}

// One bar of blocks, like the "Mode today" wireframe.
function modeBar(segments, height) {
  const bar = document.createElement("div");
  bar.style.cssText = `display:flex;gap:2px;height:${height}px`;
  const total = segments.reduce((sum, segment) => sum + (segment.to - segment.from), 0);

  for (const segment of segments) {
    const block = document.createElement("div");
    const seconds = segment.to - segment.from;
    block.style.cssText = `flex:${seconds};min-width:0;overflow:hidden;border-radius:var(--jp-radius);font-size:11px;display:flex;align-items:center;justify-content:center;white-space:nowrap`;
    const range = `${clockTime(segment.from)}–${clockTime(segment.to)}`;

    if (segment.mode === null) {
      block.style.border = "1px dashed var(--jp-grey-soft)";
      block.title = `No data ${range}`;
    } else {
      block.style.background = MODE_COLOR[segment.mode];
      block.style.color = segment.mode === "idle" ? "var(--jp-grey)" : "var(--jp-white)";
      block.title = `${MODE_NAME[segment.mode]} ${range}`;
      // Only label blocks wide enough to hold a short text like "Quiet 15m".
      if (seconds / total > 0.28) {
        block.textContent = `${MODE_SHORT[segment.mode]} ${Math.round(seconds / 60)}m`;
      }
    }
    bar.append(block);
  }
  return bar;
}

// Time labels under a bar: start, middle and "now".
function timeAxis(start, end) {
  const axis = document.createElement("div");
  axis.style.cssText = "position:relative;height:14px;font-size:10.5px;color:var(--jp-grey)";
  const labels = [
    { text: clockTime(start), css: "left:0" },
    { text: clockTime((start + end) / 2), css: "left:50%;transform:translateX(-50%)" },
    { text: "now", css: "right:0" },
  ];
  for (const label of labels) {
    const span = document.createElement("span");
    span.style.cssText = `position:absolute;${label.css}`;
    span.textContent = label.text;
    axis.append(span);
  }
  return axis;
}

// ---- Reading scores from the AI ----
// One score line = one reading attempt:
// {node_id, recorded_at, session, uid, passage, attempt, score, words: [{word, score, phonemes: [{ipa, score}]}]}
// session = when the child switched the doll into reading (set by the server).

function bandColor(score) {
  if (score >= SCORE_GOOD) {
    return "var(--jp-ok)";
  }
  if (score >= SCORE_PRACTISE) {
    return "var(--jp-warn)";
  }
  return "var(--jp-red)";
}

function bandName(score) {
  if (score >= SCORE_GOOD) {
    return "Good";
  }
  if (score >= SCORE_PRACTISE) {
    return "Close";
  }
  return "Practise";
}

function scoresOf(scores, dollId) {
  if (dollId === null) {
    return scores;
  }
  return scores.filter((line) => line.node_id === dollId);
}

function average(lines) {
  if (lines.length === 0) {
    return null;
  }
  return lines.reduce((sum, line) => sum + line.score, 0) / lines.length;
}

// Reading sessions of one doll: [{start, end, count, avg}], oldest first.
function sessionsOf(scores, dollId) {
  const byStart = {};
  for (const line of scoresOf(scores, dollId)) {
    if (byStart[line.session] === undefined) {
      byStart[line.session] = { start: line.session, end: line.recorded_at, lines: [] };
    }
    const session = byStart[line.session];
    session.end = Math.max(session.end, line.recorded_at);
    session.lines.push(line);
  }
  const sessions = Object.values(byStart);
  for (const session of sessions) {
    session.count = session.lines.length;
    session.avg = average(session.lines);
  }
  sessions.sort((a, b) => a.start - b.start);
  return sessions;
}

// One entry per class day: [{day, count, sessions, avg}], oldest first. dollId null = whole class.
function classesOf(scores, dollId) {
  const byDay = {};
  for (const line of scoresOf(scores, dollId)) {
    const day = dayOf(line.recorded_at);
    if (byDay[day] === undefined) {
      byDay[day] = { day: day, lines: [], starts: new Set() };
    }
    byDay[day].lines.push(line);
    byDay[day].starts.add(`${line.node_id}/${line.session}`);
  }
  const days = Object.values(byDay);
  for (const entry of days) {
    entry.count = entry.lines.length;
    entry.sessions = entry.starts.size;
    entry.avg = average(entry.lines);
  }
  days.sort((a, b) => (a.day < b.day ? -1 : 1));
  return days;
}

// Adds score to a {total, count} counter kept under key.
function addTo(table, key, score) {
  if (table[key] === undefined) {
    table[key] = { key: key, total: 0, count: 0 };
  }
  table[key].total += score;
  table[key].count += 1;
  return table[key];
}

// Average of each IPA sound in a list of word scores: [{ipa, avg}], weakest first.
function soundsInWords(wordScores) {
  const table = {};
  for (const word of wordScores) {
    for (const sound of word.phonemes) {
      addTo(table, sound.ipa, sound.score);
    }
  }
  const sounds = Object.values(table).map((entry) => ({ ipa: entry.key, avg: entry.total / entry.count }));
  sounds.sort((a, b) => a.avg - b.avg);
  return sounds;
}

// Why a word is wrong: its weakest sounds under Good, like "/θ/ 35% · /r/ 50%".
function weakSoundsText(sounds) {
  const weak = sounds.filter((sound) => sound.avg < SCORE_GOOD).slice(0, 2);
  if (weak.length === 0) {
    return "All sounds Good";
  }
  return `Weak sound: ${weak.map((sound) => `/${sound.ipa}/ ${Math.round(sound.avg)}%`).join(" · ")}`;
}

// Average per word: [{word, avg, count, sounds}], worst first. sounds = that word's IPA sounds, weakest first.
function wordsOf(scores, dollId) {
  const table = {};
  for (const line of scoresOf(scores, dollId)) {
    for (const word of line.words) {
      const entry = addTo(table, word.word.toLowerCase(), word.score);
      if (entry.reads === undefined) {
        entry.reads = [];
      }
      entry.reads.push(word);
    }
  }
  const words = Object.values(table).map((entry) => ({ word: entry.key, avg: entry.total / entry.count, count: entry.count, sounds: soundsInWords(entry.reads) }));
  words.sort((a, b) => a.avg - b.avg || b.count - a.count);
  return words;
}

// Average per IPA sound, with the words that use it:
// [{ipa, avg, count, change, words: [{word, avg}]}], worst first.
// change = last class day minus first class day (null with one day).
function soundsOf(scores, dollId) {
  const table = {};
  for (const line of scoresOf(scores, dollId)) {
    const day = dayOf(line.recorded_at);
    for (const word of line.words) {
      for (const sound of word.phonemes) {
        const entry = addTo(table, sound.ipa, sound.score);
        if (entry.words === undefined) {
          entry.words = {};
          entry.days = {};
        }
        addTo(entry.words, word.word.toLowerCase(), word.score);
        addTo(entry.days, day, sound.score);
      }
    }
  }

  const sounds = [];
  for (const entry of Object.values(table)) {
    const words = Object.values(entry.words).map((item) => ({ word: item.key, avg: item.total / item.count }));
    words.sort((a, b) => a.avg - b.avg);
    const days = Object.keys(entry.days).sort();
    let change = null;
    if (days.length > 1) {
      const first = entry.days[days[0]];
      const last = entry.days[days[days.length - 1]];
      change = last.total / last.count - first.total / first.count;
    }
    sounds.push({ ipa: entry.key, avg: entry.total / entry.count, count: entry.count, change: change, words: words });
  }
  sounds.sort((a, b) => a.avg - b.avg);
  return sounds;
}

function changeText(change) {
  if (change === null) {
    return "";
  }
  if (change >= 0) {
    return `▲ ${Math.round(change)}`;
  }
  return `▼ ${Math.round(-change)}`;
}

function emptyNote(text) {
  const note = document.createElement("p");
  note.className = "empty-note";
  note.textContent = text;
  return note;
}

// One row: label, bar (width = score), "72%", and an optional small line under it.
function scoreRow(label, score, right, under) {
  const row = document.createElement("div");
  row.style.cssText = "display:grid;grid-template-columns:72px minmax(0,1fr) 92px;gap:4px 10px;align-items:center;padding:6px 0;border-bottom:var(--jp-border)";
  const name = document.createElement("span");
  name.style.cssText = "font-family:var(--jp-font-display);font-weight:700;font-size:16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap";
  name.textContent = label;
  const track = document.createElement("div");
  track.style.cssText = "height:12px;border-radius:var(--jp-radius);background:var(--jp-washi);overflow:hidden";
  const bar = document.createElement("div");
  bar.style.cssText = `height:100%;width:${Math.round(score)}%;background:${bandColor(score)}`;
  track.append(bar);
  const value = document.createElement("span");
  value.style.cssText = "font-size:12.5px;text-align:right;white-space:nowrap";
  const percent = document.createElement("b");
  percent.style.color = bandColor(score);
  percent.textContent = `${Math.round(score)}%`;
  value.append(percent, right);
  row.append(name, track, value);
  if (under) {
    const small = document.createElement("span");
    small.style.cssText = "grid-column:2 / 4;font-size:11.5px;color:var(--jp-grey);margin-top:-2px";
    small.textContent = under;
    row.append(small);
  }
  return row;
}

// Sounds list, worst first, each with its weakest words.
function soundRows(sounds, shown) {
  if (sounds.length === 0) {
    return [emptyNote("No sounds scored yet. They appear once the AI sends scores.")];
  }
  return sounds.slice(0, shown).map((sound) => {
    const words = sound.words.slice(0, 3).map((item) => `${item.word} ${Math.round(item.avg)}%`).join(" · ");
    const right = sound.change === null ? ` ×${sound.count}` : ` ${changeText(sound.change)}`;
    return scoreRow(`/${sound.ipa}/`, sound.avg, right, words);
  });
}

// Words list, worst first: word, average accuracy, times read.
function wordRows(words, shown) {
  if (words.length === 0) {
    return [emptyNote("No words scored yet.")];
  }
  return words.slice(0, shown).map((item) => scoreRow(item.word, item.avg, ` ×${item.count}`, weakSoundsText(item.sounds)));
}

const SVG_NS = "http://www.w3.org/2000/svg";

function svgPart(tag, attributes, text) {
  const part = document.createElementNS(SVG_NS, tag);
  for (const name of Object.keys(attributes)) {
    part.setAttribute(name, attributes[name]);
  }
  if (text !== undefined) {
    part.textContent = text;
  }
  return part;
}

// Score line chart, 0-100 with the Good and Practise lines.
// series = [{name, color, points: [{x, y, title}]}]; x is any number (time or day index).
// xLabels = [{x, text}]. Each line ends with its name, last score and change since its first point.
function scoreChart(series, xMin, xMax, xLabels, label) {
  const left = 36;
  const right = 296;
  const top = 12;
  const bottom = 196;
  const svg = svgPart("svg", { viewBox: "0 0 380 220", role: "img", "aria-label": label });
  svg.style.cssText = "width:100%;height:100%;display:block";

  function xOf(value) {
    if (xMax === xMin) {
      return (left + right) / 2;
    }
    return left + 10 + (value - xMin) / (xMax - xMin) * (right - left - 20);
  }
  function yOf(value) {
    return bottom - value / 100 * (bottom - top);
  }

  for (const value of [0, 20, 40, 60, 80, 100]) {
    svg.append(svgPart("line", { x1: left, y1: yOf(value), x2: 376, y2: yOf(value), stroke: value === 0 ? "var(--jp-grey-soft)" : "var(--jp-line)" }));
    svg.append(svgPart("text", { x: left - 6, y: yOf(value) + 4, "font-size": 10, fill: "var(--jp-grey)", "text-anchor": "end" }, String(value)));
  }
  svg.append(svgPart("line", { x1: left, y1: yOf(SCORE_GOOD), x2: 376, y2: yOf(SCORE_GOOD), stroke: "var(--jp-grey-soft)", "stroke-dasharray": "4 3" }));
  svg.append(svgPart("text", { x: right - 4, y: yOf(SCORE_GOOD) - 4, "font-size": 10, fill: "var(--jp-grey)", "text-anchor": "end" }, `Good · ${SCORE_GOOD}`));
  svg.append(svgPart("line", { x1: left, y1: yOf(SCORE_PRACTISE), x2: 376, y2: yOf(SCORE_PRACTISE), stroke: "var(--jp-red)", "stroke-dasharray": "4 3" }));
  svg.append(svgPart("text", { x: right - 4, y: yOf(SCORE_PRACTISE) + 12, "font-size": 10, fill: "var(--jp-red)", "text-anchor": "end" }, `Practise · ${SCORE_PRACTISE}`));

  for (const item of xLabels) {
    svg.append(svgPart("text", { x: xOf(item.x), y: 212, "font-size": 10, fill: "var(--jp-grey)", "text-anchor": "middle" }, item.text));
  }

  // End labels, pushed apart so two lines that end close together stay readable.
  const ends = [];
  for (const line of series) {
    if (line.points.length === 0) {
      continue;
    }
    const coords = line.points.map((point) => `${xOf(point.x).toFixed(1)},${yOf(point.y).toFixed(1)}`).join(" ");
    svg.append(svgPart("polyline", { points: coords, fill: "none", stroke: line.color, "stroke-width": 2.5, "stroke-linejoin": "round" }));
    for (const point of line.points) {
      const dot = svgPart("circle", { cx: xOf(point.x), cy: yOf(point.y), r: 4, fill: line.color, stroke: "var(--jp-white)", "stroke-width": 2 });
      dot.append(svgPart("title", {}, point.title));
      svg.append(dot);
    }
    const first = line.points[0].y;
    const last = line.points[line.points.length - 1].y;
    const change = line.points.length > 1 ? last - first : null;
    ends.push({ line: line, last: last, change: change, y: yOf(last) });
  }
  ends.sort((a, b) => a.y - b.y);
  for (let i = 1; i < ends.length; i++) {
    ends[i].y = Math.max(ends[i].y, ends[i - 1].y + 28);
  }
  for (const end of ends) {
    svg.append(svgPart("text", { x: 304, y: end.y - 2, "font-size": 12, "font-weight": 700, fill: end.line.color }, end.line.name));
    const change = end.change === null ? "" : ` ${changeText(end.change)}`;
    svg.append(svgPart("text", { x: 304, y: end.y + 11, "font-size": 11, fill: bandColor(end.last) }, `${Math.round(end.last)}%${change}`));
  }
  return svg;
}

// Today / Past buttons. onChange gets "today" or "past".
function setupViewButtons(onChange) {
  const buttons = document.querySelectorAll("[data-view]");
  for (const button of buttons) {
    button.addEventListener("click", () => {
      for (const other of buttons) {
        const on = other === button;
        other.setAttribute("aria-pressed", String(on));
        other.style.background = on ? "var(--lm-purple)" : "transparent";
        other.style.color = on ? "var(--jp-white)" : "var(--jp-black)";
      }
      onChange(button.dataset.view);
    });
  }
}

function bandClass(score) {
  if (score >= SCORE_GOOD) {
    return "g";
  }
  if (score >= SCORE_PRACTISE) {
    return "c";
  }
  return "p";
}

// Score meter like the wireframe: half circle, red / amber / green, marker at the score.
function scoreGauge(score) {
  const cx = 130;
  const cy = 130;
  const r = 100;
  function point(value) {
    const angle = Math.PI * (1 - value / 100);
    return `${(cx + r * Math.cos(angle)).toFixed(1)} ${(cy - r * Math.sin(angle)).toFixed(1)}`;
  }
  function arc(from, to, color) {
    return svgPart("path", { d: `M ${point(from)} A ${r} ${r} 0 0 1 ${point(to)}`, fill: "none", stroke: color, "stroke-width": 20 });
  }
  const label = score === null ? "No score yet" : `Score ${Math.round(score)} percent, ${bandName(score)}`;
  const svg = svgPart("svg", { viewBox: "0 0 260 160", role: "img", "aria-label": label });
  svg.style.cssText = "width:100%;max-width:260px;display:block;margin:0 auto";
  svg.append(arc(0, SCORE_PRACTISE, "var(--jp-red)"), arc(SCORE_PRACTISE, SCORE_GOOD, "var(--jp-warn)"), arc(SCORE_GOOD, 100, "var(--jp-ok)"));
  svg.append(svgPart("text", { x: 22, y: 148, "font-size": 11, fill: "var(--jp-grey)", "text-anchor": "middle" }, "0"));
  svg.append(svgPart("text", { x: 238, y: 148, "font-size": 11, fill: "var(--jp-grey)", "text-anchor": "middle" }, "100"));
  svg.append(svgPart("text", { x: 22, y: 160, "font-size": 10, fill: "var(--jp-red)", "text-anchor": "middle" }, "Practise"));
  svg.append(svgPart("text", { x: 238, y: 160, "font-size": 10, fill: "var(--jp-ok)", "text-anchor": "middle" }, "Good"));
  for (const value of [SCORE_PRACTISE, SCORE_GOOD]) {
    const angle = Math.PI * (1 - value / 100);
    svg.append(svgPart("text", { x: cx + (r + 18) * Math.cos(angle), y: cy - (r + 18) * Math.sin(angle) + 4, "font-size": 10, fill: "var(--jp-grey)", "text-anchor": "middle" }, String(value)));
  }
  if (score === null) {
    svg.append(svgPart("text", { x: cx, y: cy - 10, "font-size": 34, "font-weight": 700, fill: "var(--jp-grey-soft)", "text-anchor": "middle", "font-family": "var(--jp-font-display)" }, "—"));
    return svg;
  }
  const [mx, my] = point(score).split(" ");
  svg.append(svgPart("circle", { cx: mx, cy: my, r: 10, fill: "var(--jp-white)", stroke: "var(--jp-black)", "stroke-width": 3 }));
  svg.append(svgPart("text", { x: cx, y: cy - 14, "font-size": 40, "font-weight": 700, fill: bandColor(score), "text-anchor": "middle", "font-family": "var(--jp-font-display)" }, `${Math.round(score)}%`));
  svg.append(svgPart("text", { x: cx, y: cy + 6, "font-size": 12, "font-weight": 700, "letter-spacing": 2, fill: bandColor(score), "text-anchor": "middle" }, bandName(score).toUpperCase()));
  return svg;
}

// Pearson correlation of [{x, y}], or null with fewer than 3 points.
function correlation(points) {
  if (points.length < 3) {
    return null;
  }
  const mx = points.reduce((sum, p) => sum + p.x, 0) / points.length;
  const my = points.reduce((sum, p) => sum + p.y, 0) / points.length;
  let top = 0;
  let sx = 0;
  let sy = 0;
  for (const p of points) {
    top += (p.x - mx) * (p.y - my);
    sx += (p.x - mx) ** 2;
    sy += (p.y - my) ** 2;
  }
  if (sx === 0 || sy === 0) {
    return null;
  }
  return top / Math.sqrt(sx * sy);
}

// upWords: what a positive r means, downWords: what a negative r means.
function linkText(r, upWords, downWords) {
  if (r === null) {
    return "Needs 3+ sessions to compare";
  }
  const size = Math.abs(r);
  let words = "no clear link";
  if (size >= 0.5) {
    words = "strong link";
  } else if (size >= 0.2) {
    words = "weak link";
  }
  if (size < 0.2) {
    return `r = ${r.toFixed(2)} · ${words}`;
  }
  return `r = ${r.toFixed(2)} · ${words}: ${r > 0 ? upWords : downWords} → higher score`;
}

// Scatter of session score (y, 0-100) against a room value (x). points = [{x, y, title}].
function scatterChart(points, xMin, xMax, xUnit, label) {
  const left = 30;
  const right = 300;
  const top = 8;
  const bottom = 110;
  const svg = svgPart("svg", { viewBox: "0 0 310 130", role: "img", "aria-label": label });
  svg.style.cssText = "width:100%;display:block";
  const xOf = (value) => left + (Math.min(Math.max(value, xMin), xMax) - xMin) / (xMax - xMin) * (right - left);
  const yOf = (value) => bottom - value / 100 * (bottom - top);
  for (const value of [0, 50, 100]) {
    svg.append(svgPart("line", { x1: left, y1: yOf(value), x2: right, y2: yOf(value), stroke: "var(--jp-line)" }));
    svg.append(svgPart("text", { x: left - 4, y: yOf(value) + 4, "font-size": 9, fill: "var(--jp-grey)", "text-anchor": "end" }, String(value)));
  }
  svg.append(svgPart("line", { x1: left, y1: yOf(SCORE_PRACTISE), x2: right, y2: yOf(SCORE_PRACTISE), stroke: "var(--jp-red)", "stroke-dasharray": "4 3" }));
  const anchors = ["start", "middle", "end"];
  [xMin, (xMin + xMax) / 2, xMax].forEach((value, index) => {
    svg.append(svgPart("text", { x: xOf(value), y: 124, "font-size": 9, fill: "var(--jp-grey)", "text-anchor": anchors[index] }, `${Math.round(value)}${xUnit}`));
  });
  for (const p of points) {
    const dot = svgPart("circle", { cx: xOf(p.x), cy: yOf(p.y), r: 4, fill: bandColor(p.y), "fill-opacity": 0.8 });
    dot.append(svgPart("title", {}, p.title));
    svg.append(dot);
  }
  return svg;
}

// Average of one room value over a session (null when the nest sent nothing then).
function roomDuring(room, session, key) {
  const values = room.filter((line) => line.recorded_at >= session.start && line.recorded_at <= session.end + 60 && typeof line[key] === "number").map((line) => line[key]);
  if (values.length === 0) {
    return null;
  }
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

// Did practising help: per word read 2+ times, first and latest read (scores are oldest first).
function practiceOf(scores) {
  const table = {};
  for (const line of scores) {
    for (const word of line.words) {
      const key = word.word.toLowerCase();
      if (table[key] === undefined) {
        table[key] = { word: key, first: word.score, count: 0 };
      }
      table[key].last = word.score;
      table[key].lastRead = word;
      table[key].count += 1;
    }
  }
  const words = Object.values(table).filter((item) => item.count > 1);
  words.sort((a, b) => a.last - b.last);
  return words;
}

// One row per word: "three   56% → 53%   ▼ 3   still stuck", and the sound to work on.
function practiceRows(words) {
  return words.map((item) => {
    const row = document.createElement("div");
    row.style.cssText = "display:grid;grid-template-columns:80px 1fr auto;gap:2px 10px;align-items:baseline;padding:8px 0;border-bottom:var(--jp-border)";
    const name = document.createElement("span");
    name.style.cssText = "font-family:var(--jp-font-display);font-weight:700;font-size:16px";
    name.textContent = item.word;

    const scores = document.createElement("span");
    scores.style.fontSize = "14px";
    const first = document.createElement("span");
    first.style.color = "var(--jp-grey)";
    first.textContent = `${Math.round(item.first)}%`;
    const last = document.createElement("b");
    last.style.color = bandColor(item.last);
    last.textContent = `${Math.round(item.last)}%`;
    scores.append(first, " → ", last);

    const change = item.last - item.first;
    const chip = document.createElement("span");
    chip.style.cssText = `font-size:12px;font-weight:700;padding:2px 8px;border-radius:var(--jp-radius);color:var(--jp-white);background:${change > 0 ? "var(--jp-ok)" : (change < 0 ? "var(--jp-red)" : "var(--jp-grey-soft)")}`;
    chip.textContent = change === 0 ? "same" : `${changeText(change)} pts`;

    const note = document.createElement("span");
    note.style.cssText = "grid-column:2 / 4;font-size:11.5px;color:var(--jp-grey)";
    const stuck = item.last < SCORE_PRACTISE ? "Still stuck · " : "";
    note.textContent = `${stuck}${weakSoundsText(soundsInWords([item.lastRead]))} · ${item.count} reads`;
    row.append(name, scores, chip, note);
    return row;
  });
}
