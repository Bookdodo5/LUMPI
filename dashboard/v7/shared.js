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

// Same as OFFLINE_AFTER_S in laptop/dashboard_server.py. A longer gap between
// two messages is drawn as "no data", not as the mode before it.
const NO_DATA_AFTER_S = 60;

// shortcut: "needs help" = 3rd try or more on the same page, or the STOP button.
// Replace with the nest's own "ask the teacher" event when it exists (T11/T12).
const NEEDS_HELP_TRIES = 3;

function clockTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: false });
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
  if (doll.activity === "stopped") {
    return "STOP button pressed";
  }
  const tries = doll.values.attempt;
  if (typeof tries === "number" && tries >= NEEDS_HELP_TRIES) {
    return `Try ${tries} on this page`;
  }
  return null;
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
  let words = "Nest and laptop connected";

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
function modeSegments(points, start, end) {
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
      if (seconds / total > 0.18) {
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
