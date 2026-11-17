const { chromium } = require("playwright");
const { spawnSync } = require("child_process");

const BASE_URL = "http://localhost:8201";

function runReplay(file) {
  const result = spawnSync("python3", ["laptop/replay_events.py", file], {
    cwd: "/home/kaikub/work",
    env: { ...process.env, LUMPI_LAN_KEY: "test" },
    timeout: 30000,
    encoding: "utf-8",
  });
  console.log(`Replay ${file}: ${result.stdout}`);
  if (result.stderr) console.error(result.stderr);
}

function stopServer() {
  spawnSync("pkill", ["-f", "dashboard_server.py"], { timeout: 5000 });
}

function startServer() {
  const child = spawnSync("bash", ["-c", "LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py &"], {
    cwd: "/home/kaikub/work",
    timeout: 5000,
    encoding: "utf-8",
  });
  // Wait for server to start
  require("child_process").execSync("sleep 2");
}

async function captureOne(name, options = {}) {
  const { setup, wait = 2000, hover, clickJson, openError, mobile, stopServerAfter } = options;

  console.log(`\n=== ${name} ===`);

  // Restart server for clean state
  stopServer();
  startServer();

  const browser = await chromium.launch({ headless: true });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });

  await page.goto(BASE_URL, { waitUntil: "networkidle" });

  if (setup) {
    runReplay(setup);
  }

  await page.waitForTimeout(wait);

  if (hover) {
    const chart = await page.$("#chart-lux");
    if (chart) {
      const box = await chart.boundingBox();
      if (box) {
        await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
        await page.waitForTimeout(500);
      }
    }
  }

  if (clickJson) {
    const summary = await page.$("#events-body details summary");
    if (summary) {
      await summary.click();
      await page.waitForTimeout(500);
    }
  }

  if (openError) {
    const summary = await page.$("#error-list details summary");
    if (summary) {
      await summary.click();
      await page.waitForTimeout(500);
    }
  }

  if (mobile) {
    await page.setViewportSize({ width: 390, height: 900 });
    await page.waitForTimeout(500);
  }

  if (stopServerAfter) {
    stopServer();
    await page.waitForTimeout(1000);
  }

  await page.screenshot({ path: `result-screenshots/${name}.png`, fullPage: true });
  console.log(`Saved ${name}.png`);

  await browser.close();
}

async function main() {
  await captureOne("01_unknown_empty", { wait: 1000 });
  await captureOne("02_online_full", { setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000 });
  await captureOne("03_stale_doll2", { setup: "tests/fixtures/dashboard/doll2_dies.jsonl", wait: 35000 });
  await captureOne("04_offline_doll2", { wait: 30000 });
  await captureOne("05_server_disconnected", { stopServerAfter: true, wait: 2000 });
  await captureOne("06_dark_room_sensor_fail_top", { setup: "tests/fixtures/dashboard/sensor_fail.jsonl", wait: 2000 });
  await captureOne("07_chart_hover_tooltip", { setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000, hover: true });
  await captureOne("08_flaky_wifi_trends", { setup: "tests/fixtures/dashboard/flaky_wifi.jsonl", wait: 2000 });
  await captureOne("09_events_json_open", { setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000, clickJson: true });
  await captureOne("10_developer_panel_error_open", { setup: "tests/fixtures/dashboard/bad_packets.jsonl", wait: 3000, openError: true });
  await captureOne("11_mobile_390", { setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000, mobile: true });

  console.log("\n=== All done ===");
}

main().catch(console.error);