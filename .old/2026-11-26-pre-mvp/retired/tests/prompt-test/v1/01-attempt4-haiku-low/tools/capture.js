// Captures the 11 states from prompt.md section 11 with Playwright, from the real server.
// Run: node tools/capture.js   (uses NODE_PATH for the playwright package)
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..");
const OUT = path.join(ROOT, "result-screenshots");
const URL = "http://localhost:8204";
const KEY = "test";
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const fixture = (name) => path.join(ROOT, "tests/fixtures/dashboard", name);

const running = new Set();
const report = [];

function env() {
  return { ...process.env, LUMPI_LAN_KEY: KEY, LUMPI_URL: URL };
}

async function startServer() {
  const child = spawn("python3", [path.join(ROOT, "laptop/dashboard_server.py")], {
    cwd: ROOT, env: env(), stdio: ["ignore", "ignore", "ignore"],
  });
  child.exited = new Promise((resolve) => child.on("exit", resolve));
  running.add(child);
  for (let i = 0; i < 100; i++) {
    try {
      const response = await fetch(`${URL}/api/dashboard/nodes`);
      if (response.ok) return child;
    } catch (error) {
      // not up yet
    }
    await sleep(100);
  }
  throw new Error("server did not start on port 8204");
}

async function stopServer(child) {
  child.kill("SIGTERM");
  await child.exited;
  running.delete(child);
}

function replay(file) {
  const child = spawn("python3", [path.join(ROOT, "laptop/replay_events.py"), file], {
    cwd: ROOT, env: env(), stdio: ["ignore", "pipe", "pipe"],
  });
  child.output = "";
  child.stdout.on("data", (data) => { child.output += data; });
  child.stderr.on("data", (data) => { child.output += data; });
  child.exited = new Promise((resolve) => child.on("exit", (code) => resolve(code)));
  running.add(child);
  return child;
}

async function waitConnected(page) {
  await page.waitForFunction(
    () => document.getElementById("connection").textContent === "Server connected",
    null, { timeout: 15000 },
  );
}

async function badges(page) {
  return page.$$eval(".card", (cards) =>
    cards.map((card) => `${card.querySelector("h2").textContent}: ${card.querySelector(".badge").textContent}`));
}

async function shot(page, name, { full = false, locator = null } = {}) {
  const target = path.join(OUT, name);
  if (locator) {
    await locator.screenshot({ path: target });
  } else {
    await page.screenshot({ path: target, fullPage: full });
  }
  console.log("saved", name);
}

async function main() {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await chromium.launch();
  const page = await (await browser.newContext({ viewport: { width: 1280, height: 900 } })).newPage();
  // The page loads Google Fonts; block it so captures do not depend on the network.
  await page.route(/fonts\.(googleapis|gstatic)\.com/, (route) => route.abort());

  try {
    // 01: nothing received yet
    let server = await startServer();
    await page.goto(`${URL}/`, { waitUntil: "domcontentloaded" });
    await waitConnected(page);
    await sleep(1500);
    report.push({ state: "01 nothing received", badges: await badges(page) });
    await shot(page, "01_unknown_empty.png", { full: true });

    // 02: all online, normal scenario
    await replay(fixture("normal.jsonl")).exited;
    await sleep(1000);
    report.push({ state: "02 all online", badges: await badges(page) });
    await shot(page, "02_online_full.png", { full: true });
    await shot(page, "02b_online_top.png");

    // 07: hover a line chart
    const lightSvg = await page.locator("#light-body svg").boundingBox();
    await page.mouse.move(lightSvg.x + lightSvg.width * 0.55, lightSvg.y + lightSvg.height * 0.5);
    await sleep(600);
    await shot(page, "07_chart_hover_tooltip.png", { locator: page.locator("#chart-light") });
    await page.mouse.move(0, 0);

    // 09: open the JSON of the newest event; it must stay open through refreshes
    await page.locator("#events-body details summary").first().click();
    await sleep(2500);
    const stillOpen = await page.$eval("#events-body details", (d) => d.open);
    report.push({ state: "09 JSON stays open", open_after_refresh: stillOpen });
    await shot(page, "09_events_json_open.png", { locator: page.locator(".events-section") });

    // 05: server lost while the page is open
    await stopServer(server);
    await page.waitForFunction(() => document.body.classList.contains("disconnected"), null, { timeout: 15000 });
    await sleep(500);
    report.push({ state: "05 server lost", header: await page.textContent("#connection") });
    await shot(page, "05_server_disconnected.png", { full: true });

    // Recovery: the page comes back by itself when the server is back
    server = await startServer();
    await waitConnected(page);
    report.push({ state: "05 recovered after restart", header: await page.textContent("#connection") });

    // 03 + 04: Doll 2 stops after 20 s; stale at ~50 s, offline at ~80 s
    let replayRun = replay(fixture("doll2_dies.jsonl"));
    await page.waitForSelector('.card[data-node="2"] .badge.stale', { timeout: 120000 });
    await sleep(800);
    report.push({ state: "03 doll 2 stale", badges: await badges(page), warning: await page.textContent('.card[data-node="2"] .warning') });
    await shot(page, "03_stale_doll2.png", { full: true });
    await page.waitForSelector('.card[data-node="2"] .badge.offline', { timeout: 120000 });
    await sleep(1000);
    report.push({ state: "04 doll 2 offline", badges: await badges(page), warning: await page.textContent('.card[data-node="2"] .warning') });
    await shot(page, "04_offline_doll2.png", { full: true });
    await shot(page, "04b_offline_timeline_gap.png", { locator: page.locator("#chart-timeline") });
    await replayRun.exited;

    // 06: dark room and failed sensors
    await stopServer(server);
    server = await startServer();
    await replay(fixture("sensor_fail.jsonl")).exited;
    await page.waitForFunction(() => /Nest: [1-9]/.test(document.getElementById("counters").textContent), null, { timeout: 20000 });
    await sleep(1000);
    report.push({ state: "06 dark room, sensor fail", badges: await badges(page), low_light: await page.isVisible(".card.nest .low-light") });
    await shot(page, "06_dark_room_sensor_fail_top.png");
    await shot(page, "06b_dark_room_trends.png", { locator: page.locator(".trends") });

    // 08: flaky Wi-Fi, 15 min then 5 min. 11: phone width on the same data.
    await stopServer(server);
    server = await startServer();
    // A resting mouse over a chart freezes trend redraws (by design), so move it away first.
    await page.mouse.move(0, 0);
    await replay(fixture("flaky_wifi.jsonl")).exited;
    await page.waitForFunction(() => /Nest: [1-9]/.test(document.getElementById("counters").textContent), null, { timeout: 20000 });
    await sleep(1000);
    await shot(page, "08_flaky_wifi_trends.png", { locator: page.locator(".trends") });
    await page.locator('label:has(input[value="5"])').click();
    await sleep(1500);
    await shot(page, "08b_range_5min.png", { locator: page.locator(".trends") });
    report.push({ state: "08 counters", counters: await page.textContent("#counters") });

    await page.setViewportSize({ width: 390, height: 900 });
    await sleep(1500);
    const sideways = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
    report.push({ state: "11 phone 390 px", sideways_scroll: sideways });
    await shot(page, "11_mobile_390.png", { full: true });
    await page.setViewportSize({ width: 1280, height: 900 });

    // 10: rejected packets, developer panel with one error open
    await stopServer(server);
    server = await startServer();
    await replay(fixture("bad_packets.jsonl")).exited;
    await sleep(1000);
    await page.locator("#dev-panel > summary").click();
    await page.locator("#error-list details.error > summary").first().click();
    await sleep(1500);
    report.push({ state: "10 bad packets", rejected_total: await page.textContent("#rejected-total"), error_cards: await page.locator("#error-list details.error").count() });
    await shot(page, "10_developer_panel_error_open.png", { locator: page.locator(".dev") });

    await stopServer(server);
  } finally {
    for (const child of running) {
      child.kill("SIGTERM");
    }
    await browser.close();
    fs.writeFileSync(path.join(OUT, "capture-report.json"), JSON.stringify(report, null, 2));
    console.log(JSON.stringify(report, null, 2));
  }
}

main().catch((error) => {
  console.error("capture failed:", error);
  process.exitCode = 1;
});
