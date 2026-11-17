// Playwright screenshots of dashboard v2, one per case, from the real server.
// Based on tests/prompt-test/v1/01-attempt4-haiku-low/tools/capture.js.
//
// Needs: python3, node, and the playwright package (any install, via NODE_PATH).
// Run from the repo root (port 8080 must be free, takes about 4 minutes):
//   NODE_PATH=~/.npm/_npx/9833c18b2d85bc59/node_modules node tools/capture_dashboard_v2.js
// Result: docs/dashboard-v2-screenshots/*.png and capture-report.json
const { chromium } = require("playwright");
const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..");
const OUT = path.join(ROOT, "docs/dashboard-v2-screenshots");
const URL = "http://localhost:8080";
const KEY = "test";
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const fixture = (name) => path.join(ROOT, "tests/fixtures/dashboard", name);

const running = new Set();
const report = [];

function env() {
  return { ...process.env, LUMPI_LAN_KEY: KEY, LUMPI_DASHBOARD: "v2" };
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
      if (response.ok) {
        return child;
      }
    } catch (error) {
      // not up yet
    }
    await sleep(100);
  }
  throw new Error("server did not start on port 8080");
}

async function stopServer(child) {
  child.kill("SIGTERM");
  await child.exited;
  running.delete(child);
}

function replay(file) {
  const child = spawn("python3", [path.join(ROOT, "laptop/replay_events.py"), file], {
    cwd: ROOT, env: env(), stdio: ["ignore", "ignore", "ignore"],
  });
  child.exited = new Promise((resolve) => child.on("exit", resolve));
  running.add(child);
  return child;
}

async function waitConnected(page) {
  await page.waitForFunction(
    () => document.getElementById("connection").textContent === "Server connected",
    null, { timeout: 15000 },
  );
}

// Wait until the count line shows messages from this node, so the replay is drawn.
async function waitCount(page, name) {
  await page.waitForFunction((node) => new RegExp(`${node}: [1-9]`).test(document.getElementById("counts").textContent), name, { timeout: 20000 });
}

async function badges(page) {
  return page.$$eval(".card", (cards) =>
    cards.map((card) => `${card.querySelector("h2").textContent}: ${card.querySelector(".badge").textContent}`));
}

async function shot(page, name, locator) {
  const target = path.join(OUT, name);
  if (locator) {
    await locator.screenshot({ path: target });
  } else {
    await page.screenshot({ path: target, fullPage: true });
  }
  console.log("saved", name);
}

async function main() {
  fs.mkdirSync(OUT, { recursive: true });
  // Use the installed Google Chrome, so any playwright version works without a browser download.
  const browser = await chromium.launch({ channel: "chrome" });
  const page = await (await browser.newContext({ viewport: { width: 1440, height: 900 } })).newPage();
  // The page loads Google Fonts; block it so captures do not depend on the network.
  await page.route(/fonts\.(googleapis|gstatic)\.com/, (route) => route.abort());
  const sensorRow = page.locator(".row:has(#chart-light)");

  try {
    // 01: nothing received yet
    let server = await startServer();
    await page.goto(`${URL}/`, { waitUntil: "domcontentloaded" });
    await waitConnected(page);
    await sleep(1500);
    report.push({ state: "01 nothing received", badges: await badges(page) });
    await shot(page, "01_empty.png");

    // 02: all online, normal scenario
    await replay(fixture("normal.jsonl")).exited;
    await sleep(1500);
    report.push({ state: "02 all online", badges: await badges(page) });
    await shot(page, "02_normal_full.png");
    await shot(page, "02b_light_chart_error_band.png", page.locator("#chart-light"));

    // 07: hover a line chart
    const box = await page.locator("#chart-distance svg").boundingBox();
    await page.mouse.move(box.x + box.width * 0.9, box.y + box.height * 0.5);
    await sleep(600);
    await shot(page, "07_chart_hover_tooltip.png", page.locator("#chart-distance"));
    await page.mouse.move(0, 0);

    // 09: open the JSON of the newest event; it must stay open through refreshes
    await page.locator("#event-rows details summary").first().click();
    await sleep(2500);
    const stillOpen = await page.$eval("#event-rows details", (details) => details.open);
    report.push({ state: "09 JSON stays open", open_after_refresh: stillOpen });
    await shot(page, "09_events_json_open.png", page.locator(".events"));

    // 05: server lost while the page is open, then back
    await stopServer(server);
    await page.waitForFunction(() => document.body.classList.contains("disconnected"), null, { timeout: 15000 });
    await sleep(500);
    report.push({ state: "05 server lost", header: await page.textContent("#connection") });
    await shot(page, "05_server_disconnected.png");
    server = await startServer();
    await waitConnected(page);
    report.push({ state: "05 recovered after restart", header: await page.textContent("#connection") });

    // 03 + 04: Doll 2 stops after 20 s; stale at ~50 s, offline at ~80 s
    const replayRun = replay(fixture("doll2_dies.jsonl"));
    await page.waitForSelector('.card[data-node="2"] .badge.stale', { timeout: 120000 });
    await sleep(800);
    report.push({ state: "03 doll 2 stale", badges: await badges(page) });
    await shot(page, "03_doll2_stale.png");
    await page.waitForSelector('.card[data-node="2"] .badge.offline', { timeout: 120000 });
    await sleep(1000);
    report.push({ state: "04 doll 2 offline", badges: await badges(page) });
    await shot(page, "04_doll2_offline.png");
    await shot(page, "04b_offline_timeline_gap.png", page.locator("#chart-timeline"));
    await replayRun.exited;

    // 06: failed sensors (null readings show as gaps)
    await stopServer(server);
    server = await startServer();
    await replay(fixture("sensor_fail.jsonl")).exited;
    await waitCount(page, "Nest");
    await sleep(1500);
    report.push({ state: "06 sensor fail", badges: await badges(page), low_light: await page.isVisible(".card.nest .low-light") });
    await shot(page, "06_sensor_fail.png");

    // 12: busy classroom
    await stopServer(server);
    server = await startServer();
    await replay(fixture("busy.jsonl")).exited;
    await waitCount(page, "Doll 1");
    await sleep(1500);
    report.push({ state: "12 busy", badges: await badges(page) });
    await shot(page, "12_busy.png");

    // 08: flaky Wi-Fi, 15 min then 5 min. 11: phone width on the same data.
    await stopServer(server);
    server = await startServer();
    await replay(fixture("flaky_wifi.jsonl")).exited;
    await waitCount(page, "Nest");
    await sleep(1500);
    await shot(page, "08_flaky_wifi_full.png");
    await page.locator('label:has(input[value="5"])').click();
    await sleep(1500);
    await shot(page, "08b_range_5min_sensors.png", sensorRow);
    report.push({ state: "08 counts", counts: await page.textContent("#counts") });

    await page.setViewportSize({ width: 390, height: 900 });
    await sleep(1500);
    const sideways = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
    report.push({ state: "11 phone 390 px", sideways_scroll: sideways });
    await shot(page, "11_phone_390.png");
    await page.setViewportSize({ width: 1440, height: 900 });

    // 10: rejected packets, developer panel with one error open
    await stopServer(server);
    server = await startServer();
    await replay(fixture("bad_packets.jsonl")).exited;
    await sleep(1500);
    await page.locator("#dev-panel > summary").click();
    await page.locator("#error-list details > summary").first().click();
    await sleep(1500);
    report.push({ state: "10 bad packets", rejected_total: await page.textContent("#rejected-total") });
    await shot(page, "10_developer_panel_error_open.png", page.locator(".dev"));

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
