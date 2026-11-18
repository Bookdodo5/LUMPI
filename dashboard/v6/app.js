// Overview page: fill the live parts from the laptop server every 2 s.
// Live now: nest connection, light (lux) and teacher near nest.
// Everything else on the page (dolls, scores, words, noise dB) is still sample data.
// All text goes in with textContent, so data from the network cannot add HTML.

const POLL_MS = 2000;

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

function clockTime(unixSeconds) {
  return new Date(unixSeconds * 1000).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", hour12: false });
}

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

function setDot(color, outline) {
  const dot = document.getElementById("nest-dot");
  dot.style.background = outline ? "transparent" : color;
  dot.style.border = outline ? `2px solid ${color}` : "0";
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
  const nestText = document.getElementById("nest-text");
  const seen = document.getElementById("light-seen");
  const teacher = document.getElementById("teacher-near");

  // No message from the nest yet: keep the sample gauge.
  if (nest === undefined || nest.status === "unknown") {
    setDot("var(--jp-grey-soft)", true);
    nestText.textContent = "Laptop connected · waiting for the nest";
    seen.textContent = "Sample";
    return;
  }

  const clock = clockTime(nest.last_seen);
  if (nest.status === "online") {
    setDot("var(--jp-ok)", false);
    nestText.textContent = "Nest and laptop connected";
    seen.textContent = `Updated ${clock}`;
  } else if (nest.status === "stale") {
    setDot("var(--jp-warn)", true);
    nestText.textContent = `Nest quiet for ${Math.round(nest.age_s)} s`;
    seen.textContent = `Last reading ${clock}`;
  } else {
    setDot("var(--jp-red)", false);
    nestText.textContent = `Nest offline since ${clock}`;
    seen.textContent = `Last reading ${clock} · not current`;
  }

  drawLight(nest.values.lux);

  const distance = nest.values.distance_cm;
  if (distance === null || distance === undefined) {
    teacher.textContent = "Teacher near nest: no reading";
  } else if (distance < TEACHER_NEAR_CM) {
    teacher.textContent = "Teacher near nest: Yes";
  } else {
    teacher.textContent = "Teacher near nest: No";
  }
}

async function poll() {
  try {
    const response = await fetch("/api/dashboard/nodes", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`nodes returned ${response.status}`);
    }
    const nodes = await response.json();
    drawNest(nodes.find((node) => node.node_id === 0));
  } catch (error) {
    console.warn("dashboard poll failed:", error);
    setDot("var(--jp-red)", false);
    document.getElementById("nest-text").textContent = "Laptop server not reachable · retrying";
    document.getElementById("light-seen").textContent = "Not current";
  }
  setTimeout(poll, POLL_MS);
}

poll();
