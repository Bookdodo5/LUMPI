// Shared by the three pages (app.js, doll.js, session.js): constants, page start and polling,
// live doll state, score maths, and the score lists and charts.
// Text from the network always goes in as text (el(), textContent), never as HTML.

// ---- Constants ----

const DOLL_IDS = [1, 2];  // SHOWN_NODES in api/lumpi.py, without the nest (0)

const ACTIVITY_TEXT = {
  idle: "Idle",
  book_on: "Book on the mat",
  recording: "Recording",
  waiting_ai: "Waiting for AI",
  feedback: "Giving feedback",
  quiet_study: "Quiet study",
  stopped: "Stopped (STOP button)",
};

// Mode = what the child chose on the doll.
const READING_ACTIVITIES = ["book_on", "recording", "waiting_ai", "feedback"];  // READING in api/lumpi.py
const MODES = {
  reading: { name: "Reading", short: "Reading", color: "var(--jp-black)" },
  quiet: { name: "Quiet Study", short: "Quiet", color: "var(--lm-purple)" },
  idle: { name: "Idle", short: "Idle", color: "var(--jp-line)" },
};

// A gap longer than this between two messages is drawn as "no data" (offline in nodes(), api/lumpi.py).
const NO_DATA_AFTER_S = 60;

// Score bands: Practise under 60 (PASS_SCORE in api/lumpi.py), Close 60-84, Good 85+.
const SCORE_PRACTISE = 60;
const SCORE_GOOD = 85;
const BANDS = [
  { min: SCORE_GOOD, name: "Good", color: "var(--jp-ok)", css: "good" },
  { min: SCORE_PRACTISE, name: "Close", color: "var(--jp-warn)", css: "close" },
  { min: -Infinity, name: "Practise", color: "var(--jp-red)", css: "practise" },
];

const DOLL_COLOR = { 1: "var(--jp-chart-2)", 2: "var(--jp-chart-1)" };

// The LUMPI dog, drawn into every <svg class="logo"> of the page.
const LOGO = `
  <path d="M3 21 C12 29 20 27 27 21" fill="none" stroke="#D9A15B" stroke-width="5" stroke-linecap="round"/>
  <ellipse cx="38" cy="17" rx="19" ry="7.5" fill="#D9A15B"/>
  <path d="M28 22 L26 28 M47 22 L49 28" stroke="#5A3418" stroke-width="3" stroke-linecap="round"/>
  <ellipse cx="60" cy="13" rx="10" ry="5.2" fill="#D9A15B"/>
  <circle cx="63" cy="11" r="1.7" fill="#231A12"/>
  <circle cx="29" cy="15" r="2" fill="#5A3418"/>
  <circle cx="35" cy="13" r="2" fill="#5A3418"/>
  <circle cx="41" cy="16" r="2" fill="#5A3418"/>
  <circle cx="47" cy="14" r="2" fill="#5A3418"/>
  <circle cx="33" cy="19" r="1.7" fill="#5A3418"/>
  <circle cx="39" cy="20" r="1.7" fill="#5A3418"/>
  <path d="M70 14 L75 15.5 M75 15.5 L73.5 18 M75 15.5 L76 13.5" stroke="#7B2FBE" stroke-width="1.6" fill="none"
        stroke-linecap="round"/>`;

// ---- Small helpers ----

function byId(id) {
  return document.getElementById(id);
}

function setText(id, text) {
  byId(id).textContent = text;
}

// A new element: el("a", { class: "row", href: "/" }, "text", otherElement).
function el(tag, attributes = {}, ...children) {
  return build(document.createElement(tag), attributes, children);
}

// The same for SVG: svgPart("circle", { cx: 5, cy: 5, r: 4 }, svgPart("title", {}, "tooltip")).
function svgPart(tag, attributes = {}, ...children) {
  return build(document.createElementNS("http://www.w3.org/2000/svg", tag), attributes, children);
}

function build(element, attributes, children) {
  for (const [name, value] of Object.entries(attributes)) {
    element.setAttribute(name, value);
  }
  element.append(...children);
  return element;
}

// SVG text: small and grey unless `more` says otherwise.
function svgText(x, y, text, more = {}) {
  return svgPart("text", { x: x, y: y, "font-size": 10, fill: "var(--jp-grey)", ...more }, text);
}

function logo() {
  const image = svgPart("svg", { class: "logo", viewBox: "0 0 76 30", "aria-hidden": "true" });
  image.innerHTML = LOGO;  // a fixed drawing from this file, no data in it
  return image;
}

function clockTime(unixSeconds) {
  const options = { hour: "2-digit", minute: "2-digit", hour12: false };
  return new Date(unixSeconds * 1000).toLocaleTimeString([], options);
}

// "2026-11-20": en-CA writes dates as YYYY-MM-DD.
function dayOf(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleDateString("en-CA");
}

// "20 Nov"
function shortDay(day) {
  return new Date(`${day}T12:00:00`).toLocaleDateString([], { day: "numeric", month: "short" });
}

function minutesText(seconds) {
  return `${Math.round(seconds / 60)} min`;
}

function percent(score) {
  return `${Math.round(score)}%`;
}

function band(score) {
  return BANDS.find((item) => score >= item.min);
}

function changeText(change) {
  return change >= 0 ? `▲ ${Math.round(change)}` : `▼ ${Math.round(-change)}`;
}

// A doll value it really sent: null = no book or a failed sensor, undefined = not in its last message.
function sent(value) {
  return value !== null && value !== undefined;
}

function emptyNote(text) {
  return el("p", { class: "empty-note" }, text);
}

// ---- Page start and polling ----

for (const spot of document.querySelectorAll("svg.logo")) {
  spot.replaceWith(logo());
}

// Runs task now, and again ms after each run. A failed run (cloud down, wrong password) is logged,
// failed() shows it on the page, and the next run simply tries again.
async function repeat(ms, task, failed) {
  try {
    await task();
  } catch (error) {
    console.warn(error);
    if (failed) {
      failed();
    }
  }
  setTimeout(repeat, ms, ms, task, failed);
}

async function getJson(path) {
  const response = await fetch(path);
  if (!response.ok) {
    throw new Error(`${path} returned ${response.status}`);
  }
  return response.json();
}

// Live state every 2 s: the nest dot, the help popup, and draw(nodes) for the page.
// nodes = one per node_id, sorted, always with 0, 1 and 2: nodes[0] = nest, nodes[1] = Doll 1.
// drawDown() marks the page's doll chips when the cloud cannot be reached.
function watchNodes(draw, drawDown) {
  const popup = el("div", { id: "help-popup", role: "alert" });
  popup.hidden = true;
  document.body.append(popup);
  repeat(2000, async () => {
    const nodes = await getJson("/api/dashboard/nodes");
    drawNestDot(nodes[0]);
    draw(nodes);
    drawHelpPopup(popup, nodes);
  }, () => {
    setDot("var(--jp-red)", false, "Cloud not reachable · retrying");
    drawDown();
  });
}

// Today / Past: which scores the page shows. Today's are read again every 10 s,
// Past only when its button is pressed. draw(data) gets the reply of /api/dashboard/scores.
let view = "today";

function watchScores(draw, failed) {
  async function load() {
    const asked = view;
    const data = await getJson(`/api/dashboard/scores?day=${asked}`);
    if (asked === view) {  // drop a slow reply for the view the teacher just left
      document.body.dataset.view = view;  // shows the cards of this view (see style.css)
      draw(data);
    }
  }
  const buttons = document.querySelectorAll("button[data-view]");
  for (const button of buttons) {
    button.addEventListener("click", () => {
      for (const other of buttons) {
        other.setAttribute("aria-pressed", other === button);
      }
      view = button.dataset.view;
      load().catch(console.warn);
    });
  }
  repeat(10000, async () => {
    if (view === "today") {
      await load();
    }
  }, failed);
}

// ---- Live doll state ----

function modeOf(activity) {
  if (activity === "quiet_study") {
    return "quiet";
  }
  return READING_ACTIVITIES.includes(activity) ? "reading" : "idle";
}

function setDot(color, outline, text) {
  const dot = byId("nest-dot");
  if (!dot) {
    return;  // the session page's header has no dot
  }
  dot.style.background = outline ? "transparent" : color;
  dot.style.border = outline ? `2px solid ${color}` : "0";
  setText("nest-text", text);
}

function drawNestDot(nest) {
  if (nest.status === "unknown") {
    setDot("var(--jp-grey-soft)", true, "Cloud connected · waiting for the nest");
  } else if (nest.status === "stale") {
    setDot("var(--jp-warn)", true, `No message from the nest for ${Math.round(nest.age_s)} s`);
  } else if (nest.status === "offline") {
    setDot("var(--jp-red)", false, `Nest offline since ${clockTime(nest.last_seen)}`);
  } else {
    setDot("var(--jp-ok)", false, "Nest and cloud connected");
  }
}

// look: "filled" (white text on the colour), "line" (coloured text and border) or "dashed".
function setChip(chip, text, color, look) {
  chip.textContent = text;
  chip.style.background = look === "filled" ? color : "transparent";
  chip.style.color = look === "filled" ? "var(--jp-white)" : color;
  chip.style.border = `1px ${look === "dashed" ? "dashed" : "solid"} ${color}`;
}

function setModeChip(chip, doll) {
  if (doll.status === "unknown") {
    setChip(chip, "—", "var(--jp-grey-soft)", "dashed");
    return;
  }
  const mode = MODES[modeOf(doll.activity)];
  setChip(chip, mode.name, mode.color, "filled");
  if (mode === MODES.idle) {
    chip.style.color = "var(--jp-black)";
  }
}

// Needs help, OK, no message for a while (not "quiet": that word is Quiet Study's), or offline.
function setStateChip(chip, doll) {
  if (doll.status === "unknown") {
    setChip(chip, "Waiting", "var(--jp-grey-soft)", "dashed");
  } else if (doll.needs_help) {
    setChip(chip, "⚑ Needs help", "var(--jp-red)", "filled");
  } else if (doll.status === "online") {
    setChip(chip, "✓ OK", "var(--jp-ok)", "line");
  } else if (doll.status === "stale") {
    setChip(chip, `! No msg ${Math.round(doll.age_s)} s`, "var(--jp-warn)", "line");
  } else {
    setChip(chip, `Last seen ${clockTime(doll.last_seen)}`, "var(--jp-red)", "dashed");
  }
}

// ["Book 04A1B2C3", "p.2"]: the book and page the doll sent.
function bookParts(values) {
  const parts = [];
  if (values.uid) {
    parts.push(`Book ${values.uid}`);
  }
  if (sent(values.passage)) {
    parts.push(`p.${values.passage}`);
  }
  return parts;
}

// Help popup: shows when a child presses help on a doll. OK only hides it on this screen;
// the dashboard never sends anything back to the doll.
const helpSeen = {};  // node_id -> the help press the teacher already answered

function drawHelpPopup(popup, nodes) {
  const pending = nodes.filter((node) => DOLL_IDS.includes(node.node_id)
    && node.needs_help && node.help_at !== helpSeen[node.node_id]);
  const key = JSON.stringify(pending.map((node) => [node.node_id, node.help_at, ...bookParts(node.values)]));
  if (popup.dataset.key === key) {
    return;  // same list as last time: leave it alone (keeps keyboard focus, read out once)
  }
  popup.dataset.key = key;
  popup.hidden = pending.length === 0;
  if (popup.hidden) {
    return;
  }
  const lines = pending.map((doll) => el("a", { class: "row", href: `/doll.html?doll=${doll.node_id}` },
    el("b", {}, `Doll ${doll.node_id}`),
    [` · asked at ${clockTime(doll.help_at)}`, ...bookParts(doll.values)].join(" · ")));
  const ok = el("button", { type: "button" }, "OK, I am going");
  ok.addEventListener("click", () => {
    for (const doll of pending) {
      helpSeen[doll.node_id] = doll.help_at;
    }
    popup.hidden = true;
  });
  popup.replaceChildren(el("div", { class: "popup-title" }, "⚑ A child needs help"), ...lines, ok);
}

// ---- Mode timeline ----

// One doll's history points, in the order the doll measured them (late messages arrive last).
function sortedPoints(history, dollId) {
  return history.nodes[dollId].slice().sort((a, b) => a.recorded_at - b.recorded_at);
}

// Cuts [start, end] into parts [{mode, from, to}]; mode is null where no message came.
function modeSegments(allPoints, start, end) {
  // Help and noise messages carry no mode, so they draw no block.
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

  points.forEach((point, i) => {
    const at = Math.max(start, Math.min(point.recorded_at, end));
    const next = i + 1 < points.length ? Math.min(points[i + 1].recorded_at, end) : end;
    const stop = Math.max(at, Math.min(next, at + NO_DATA_AFTER_S));
    if (at > cursor) {
      add(null, cursor, at);
    }
    if (stop > at) {
      add(modeOf(point.activity), at, stop);
    }
    cursor = Math.max(cursor, stop);
  });
  if (cursor < end) {
    add(null, cursor, end);
  }
  return segments;
}

// ---- Score maths ----
// One score line = one reading attempt: {node_id, recorded_at, session, uid, passage, attempt, score,
// words: [{word, score, phonemes: [{ipa, score}]}]}. session = when the reading session began.

function ofDoll(scores, dollId) {
  return scores.filter((line) => line.node_id === dollId);
}

function mean(items, valueOf) {
  return items.reduce((sum, item) => sum + valueOf(item), 0) / items.length;
}

// Average score of score lines, or null when there are none.
function average(lines) {
  return lines.length === 0 ? null : mean(lines, (line) => line.score);
}

// Groups items by key, in the order the keys first appear: [{key, items}].
function groupBy(items, keyOf) {
  const groups = new Map();
  for (const item of items) {
    const key = keyOf(item);
    if (!groups.has(key)) {
      groups.set(key, []);
    }
    groups.get(key).push(item);
  }
  return [...groups].map(([key, members]) => ({ key: key, items: members }));
}

function weakestFirst(list) {
  return list.sort((a, b) => a.avg - b.avg);
}

// Reading sessions: [{start, end, lines, count, avg}], oldest first.
function sessionsOf(lines) {
  const sessions = groupBy(lines, (line) => line.session).map((group) => ({
    start: group.key,
    end: Math.max(...group.items.map((line) => line.recorded_at)),
    lines: group.items,
    count: group.items.length,
    avg: average(group.items),
  }));
  return sessions.sort((a, b) => a.start - b.start);
}

// Class days: [{day, lines, count, sessions, avg}], oldest first.
function classesOf(lines) {
  const days = groupBy(lines, (line) => dayOf(line.recorded_at)).map((group) => ({
    day: group.key,
    lines: group.items,
    count: group.items.length,
    sessions: new Set(group.items.map((line) => `${line.node_id}/${line.session}`)).size,
    avg: average(group.items),
  }));
  return days.sort((a, b) => (a.day < b.day ? -1 : 1));
}

// Every word read in these lines, worst first: [{word, avg, count, sounds}]
// (sounds = that word's sounds, weakest first).
function wordsOf(lines) {
  const reads = lines.flatMap((line) => line.words);
  const words = groupBy(reads, (read) => read.word.toLowerCase()).map((group) => ({
    word: group.key,
    avg: mean(group.items, (read) => read.score),
    count: group.items.length,
    sounds: soundsInWords(group.items),
  }));
  return words.sort((a, b) => a.avg - b.avg || b.count - a.count);
}

// The sounds of some word reads, weakest first: [{ipa, avg}].
function soundsInWords(reads) {
  const sounds = groupBy(reads.flatMap((read) => read.phonemes), (sound) => sound.ipa);
  const averages = sounds.map((group) => {
    return { ipa: group.key, avg: mean(group.items, (sound) => sound.score) };
  });
  return weakestFirst(averages);
}

// Why a word is wrong: its two weakest sounds under Good, like "/θ/ 35% · /r/ 50%".
function weakSoundsText(sounds) {
  const weak = sounds.filter((sound) => sound.avg < SCORE_GOOD).slice(0, 2);
  if (weak.length === 0) {
    return "All sounds Good";
  }
  return `Weak sound: ${weak.map((sound) => `/${sound.ipa}/ ${percent(sound.avg)}`).join(" · ")}`;
}

// Every sound, worst first: [{ipa, avg, count, change, words: [{word, avg}]}].
// change = last class day minus first class day (null with one day).
function soundsOf(lines) {
  const reads = [];
  for (const line of lines) {
    for (const word of line.words) {
      for (const sound of word.phonemes) {
        reads.push({ ipa: sound.ipa, score: sound.score, word: word.word.toLowerCase(), wordScore: word.score,
                     day: dayOf(line.recorded_at) });
      }
    }
  }
  return weakestFirst(groupBy(reads, (read) => read.ipa).map((group) => {
    const days = groupBy(group.items, (read) => read.day).sort((a, b) => (a.key < b.key ? -1 : 1));
    const dayAvg = (index) => mean(days[index].items, (read) => read.score);
    const words = groupBy(group.items, (read) => read.word)
      .map((byWord) => ({ word: byWord.key, avg: mean(byWord.items, (read) => read.wordScore) }));
    return {
      ipa: group.key,
      avg: mean(group.items, (read) => read.score),
      count: group.items.length,
      change: days.length > 1 ? dayAvg(days.length - 1) - dayAvg(0) : null,
      words: weakestFirst(words),
    };
  }));
}

// ---- Score lists, cells and charts ----

// One row: label, bar (width = score), "72%" + right, and a small line under it.
function scoreRow(label, score, right, under) {
  const color = band(score).color;
  return el("div", { class: "score-row" },
    el("span", { class: "name" }, label),
    el("div", { class: "track" }, el("div", { style: `width:${Math.round(score)}%;background:${color}` })),
    el("span", { class: "value" }, el("b", { style: `color:${color}` }, percent(score)), right),
    el("span", { class: "under" }, under));
}

// Sounds, worst first, each with its weakest words.
function soundRows(sounds, shown) {
  if (sounds.length === 0) {
    return [emptyNote("No sounds scored yet. They appear once the AI sends scores.")];
  }
  return sounds.slice(0, shown).map((sound) => scoreRow(`/${sound.ipa}/`, sound.avg,
    sound.change === null ? ` ×${sound.count}` : ` ${changeText(sound.change)}`,
    sound.words.slice(0, 3).map((item) => `${item.word} ${percent(item.avg)}`).join(" · ")));
}

// Words, worst first: word, average, times read, weak sounds.
function wordRows(words, shown) {
  if (words.length === 0) {
    return [emptyNote("No words scored yet.")];
  }
  return words.slice(0, shown).map((item) => scoreRow(item.word, item.avg, ` ×${item.count}`,
    weakSoundsText(item.sounds)));
}

// A small label over a big value, for the summary and timing cards.
function statCell(label, value, color = "var(--jp-black)") {
  return el("div", {}, el("div", { class: "stat-label" }, label),
    el("div", { class: "stat-value", style: `color:${color}` }, value));
}

// Score line chart, 0-100, with the Good and Practise lines.
// series = [{name, color, points: [{x, y, title}]}]; labels = [{x, text}] under the chart.
// Each line ends with its name, last score and change since its first point.
function scoreChart(series, xMin, xMax, labels, description) {
  const [left, right, top, bottom] = [36, 296, 12, 196];
  const xOf = (x) => {
    if (xMax === xMin) {
      return (left + right) / 2;
    }
    return left + 10 + (x - xMin) / (xMax - xMin) * (right - left - 20);
  };
  const yOf = (y) => bottom - y / 100 * (bottom - top);
  const chart = svgPart("svg", { class: "chart", viewBox: "0 0 380 220", role: "img",
                                 "aria-label": description });

  for (const value of [0, 20, 40, 60, 80, 100]) {
    const stroke = value === 0 ? "var(--jp-grey-soft)" : "var(--jp-line)";
    chart.append(svgPart("line", { x1: left, y1: yOf(value), x2: 376, y2: yOf(value), stroke: stroke }),
      svgText(left - 6, yOf(value) + 4, String(value), { "text-anchor": "end" }));
  }
  const guides = [
    { value: SCORE_GOOD, name: "Good", line: "var(--jp-grey-soft)", text: "var(--jp-grey)", dy: -4 },
    { value: SCORE_PRACTISE, name: "Practise", line: "var(--jp-red)", text: "var(--jp-red)", dy: 12 },
  ];
  for (const guide of guides) {
    const y = yOf(guide.value);
    chart.append(
      svgPart("line", { x1: left, y1: y, x2: 376, y2: y, stroke: guide.line, "stroke-dasharray": "4 3" }),
      svgText(right - 4, y + guide.dy, `${guide.name} · ${guide.value}`,
              { fill: guide.text, "text-anchor": "end" }));
  }
  for (const item of labels) {
    chart.append(svgText(xOf(item.x), 212, item.text, { "text-anchor": "middle" }));
  }

  // End labels, pushed apart so two lines that end close together stay readable.
  const ends = [];
  for (const line of series.filter((item) => item.points.length > 0)) {
    const coords = line.points.map((point) => `${xOf(point.x).toFixed(1)},${yOf(point.y).toFixed(1)}`);
    chart.append(svgPart("polyline", { points: coords.join(" "), fill: "none", stroke: line.color,
                                       "stroke-width": 2.5, "stroke-linejoin": "round" }));
    for (const point of line.points) {
      const dot = { cx: xOf(point.x), cy: yOf(point.y), r: 4, fill: line.color,
                    stroke: "var(--jp-white)", "stroke-width": 2 };
      chart.append(svgPart("circle", dot, svgPart("title", {}, point.title)));
    }
    const first = line.points[0].y;
    const last = line.points[line.points.length - 1].y;
    ends.push({ line: line, last: last, change: line.points.length > 1 ? last - first : null, y: yOf(last) });
  }
  ends.sort((a, b) => a.y - b.y);
  for (let i = 1; i < ends.length; i++) {
    ends[i].y = Math.max(ends[i].y, ends[i - 1].y + 28);
  }
  for (const end of ends) {
    const change = end.change === null ? "" : ` ${changeText(end.change)}`;
    chart.append(
      svgText(304, end.y - 2, end.line.name, { "font-size": 12, "font-weight": 700, fill: end.line.color }),
      svgText(304, end.y + 11, `${percent(end.last)}${change}`,
              { "font-size": 11, fill: band(end.last).color }));
  }
  return chart;
}

// Score chart with one point per class day; days = every day on the x axis, oldest first.
// At most 6 date labels, so they never overlap.
function dayChart(series, days, description) {
  const step = Math.max(1, Math.ceil(days.length / 6));
  const labels = days.map((day, index) => ({ x: index, text: shortDay(day) }))
    .filter((item, index) => index % step === 0 || index === days.length - 1);
  return scoreChart(series, 0, days.length - 1, labels, description);
}

// Half-circle score meter: red / amber / green, a marker at the score. score null = no score yet.
function scoreGauge(score) {
  const [cx, cy, r] = [130, 130, 100];
  const angle = (value) => Math.PI * (1 - value / 100);
  const point = (value) => {
    const x = cx + r * Math.cos(angle(value));
    const y = cy - r * Math.sin(angle(value));
    return `${x.toFixed(1)} ${y.toFixed(1)}`;
  };
  const arc = (from, to, color) => svgPart("path", {
    d: `M ${point(from)} A ${r} ${r} 0 0 1 ${point(to)}`, fill: "none", stroke: color, "stroke-width": 20 });
  const description = score === null ? "No score yet"
    : `Score ${Math.round(score)} percent, ${band(score).name}`;
  const gauge = svgPart("svg", { class: "meter", viewBox: "0 0 260 160", role: "img",
                                 "aria-label": description },
    arc(0, SCORE_PRACTISE, "var(--jp-red)"), arc(SCORE_PRACTISE, SCORE_GOOD, "var(--jp-warn)"),
    arc(SCORE_GOOD, 100, "var(--jp-ok)"),
    svgText(22, 148, "0", { "font-size": 11, "text-anchor": "middle" }),
    svgText(238, 148, "100", { "font-size": 11, "text-anchor": "middle" }),
    svgText(22, 160, "Practise", { fill: "var(--jp-red)", "text-anchor": "middle" }),
    svgText(238, 160, "Good", { fill: "var(--jp-ok)", "text-anchor": "middle" }));
  for (const value of [SCORE_PRACTISE, SCORE_GOOD]) {
    const x = cx + (r + 18) * Math.cos(angle(value));
    const y = cy - (r + 18) * Math.sin(angle(value)) + 4;
    gauge.append(svgText(x, y, String(value), { "text-anchor": "middle" }));
  }
  const big = { "font-weight": 700, "text-anchor": "middle", "font-family": "var(--jp-font-display)" };
  if (score === null) {
    gauge.append(svgText(cx, cy - 10, "—", { ...big, "font-size": 34, fill: "var(--jp-grey-soft)" }));
    return gauge;
  }
  const [mx, my] = point(score).split(" ");
  const color = band(score).color;
  gauge.append(svgPart("circle", { cx: mx, cy: my, r: 10, fill: "var(--jp-white)", stroke: "var(--jp-black)",
                                   "stroke-width": 3 }),
    svgText(cx, cy - 14, percent(score), { ...big, "font-size": 40, fill: color }),
    svgText(cx, cy + 6, band(score).name.toUpperCase(),
            { "font-size": 12, "font-weight": 700, "letter-spacing": 2, fill: color,
              "text-anchor": "middle" }));
  return gauge;
}
