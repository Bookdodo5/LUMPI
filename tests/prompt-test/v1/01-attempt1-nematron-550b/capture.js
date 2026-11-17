const { chromium } = require("playwright");

const BASE_URL = "http://localhost:8201";
const SCENARIOS = [
  // State 1: Nothing received - just start server, open page
  { name: "01_unknown_empty", setup: null, wait: 1000 },

  // State 2: All online - replay normal.jsonl
  { name: "02_online_full", setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000 },

  // State 3: One doll stale - replay doll2_dies.jsonl, wait 30s
  { name: "03_stale_doll2", setup: "tests/fixtures/dashboard/doll2_dies.jsonl", wait: 35000 },

  // State 4: One doll offline - wait 60s more (from doll2_dies state)
  { name: "04_offline_doll2", setup: null, wait: 30000 },

  // State 5: Server lost - stop server with page open
  { name: "05_server_disconnected", setup: "server_stop", wait: 2000 },

  // State 6: Dark room / failed sensor - replay sensor_fail.jsonl
  { name: "06_dark_room_sensor_fail_top", setup: "tests/fixtures/dashboard/sensor_fail.jsonl", wait: 2000 },

  // State 7: Chart hover - mouse over a line chart
  { name: "07_chart_hover_tooltip", setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000, hover: true },

  // State 8: Flaky Wi-Fi - replay flaky_wifi.jsonl
  { name: "08_flaky_wifi_trends", setup: "tests/fixtures/dashboard/flaky_wifi.jsonl", wait: 2000 },

  // State 9: Event detail - click JSON
  { name: "09_events_json_open", setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000, clickJson: true },

  // State 10: Bad packets - replay bad_packets.jsonl
  { name: "10_developer_panel_error_open", setup: "tests/fixtures/dashboard/bad_packets.jsonl", wait: 3000, openError: true },

  // State 11: Phone width - 390px
  { name: "11_mobile_390", setup: "tests/fixtures/dashboard/normal.jsonl", wait: 2000, mobile: true },
];

async function replayScenario(file) {
  const { execSync } = require("child_process");
  try {
    execSync(`LUMPI_LAN_KEY=test python3 laptop/replay_events.py ${file}`, { cwd: "/home/kaikub/work", stdio: "pipe" });
  } catch (e) {
    console.log(`Replay ${file} completed with exit code ${e.status}`);
  }
}

async function stopServer() {
  const { execSync } = require("child_process");
  try {
    execSync("pkill -f 'dashboard_server.py'", { stdio: "pipe" });
  } catch (e) {
    // Ignore
  }
  await new Promise(r => setTimeout(r, 500));
}

async function startServer() {
  const { execSync } = require("child_process");
  execSync("LUMPI_LAN_KEY=test python3 laptop/dashboard_server.py &", { cwd: "/home/kaikub/work", stdio: "pipe" });
  await new Promise(r => setTimeout(r, 2000));
}

async function captureAll() {
  const browser = await chromium.launch({ headless: true });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });

  for (const scenario of SCENARIOS) {
    console.log(`\n=== Capturing ${scenario.name} ===`);

    // Reset: restart server for clean state
    await stopServer();
    await startServer();

    // Navigate to dashboard
    await page.goto(BASE_URL, { waitUntil: "networkidle" });

    // Run setup if needed
    if (scenario.setup && scenario.setup !== "server_stop") {
      await replayScenario(scenario.setup);
    }

    // Wait for state to stabilize
    if (scenario.wait) {
      await page.waitForTimeout(scenario.wait);
    }

    // Special actions
    if (scenario.hover) {
      // Hover over the lux chart
      const chart = await page.$("#chart-lux");
      if (chart) {
        const box = await chart.boundingBox();
        if (box) {
          await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
          await page.waitForTimeout(500);
        }
      }
    }

    if (scenario.clickJson) {
      // Click first JSON summary in events table
      const summary = await page.$("#events-body details summary");
      if (summary) {
        await summary.click();
        await page.waitForTimeout(500);
      }
    }

    if (scenario.openError) {
      // Open first error in dev panel
      const summary = await page.$("#error-list details summary");
      if (summary) {
        await summary.click();
        await page.waitForTimeout(500);
      }
    }

    if (scenario.mobile) {
      await page.setViewportSize({ width: 390, height: 900 });
      await page.waitForTimeout(500);
    } else {
      await page.setViewportSize({ width: 1280, height: 900 });
      await page.waitForTimeout(500);
    }

    // Take screenshot
    await page.screenshot({ path: `result-screenshots/${scenario.name}.png`, fullPage: true });
    console.log(`Saved result-screenshots/${scenario.name}.png`);

    // Special: for server disconnected, stop server after page load
    if (scenario.setup === "server_stop") {
      await stopServer();
      await page.waitForTimeout(1000);
      await page.screenshot({ path: `result-screenshots/${scenario.name}.png`, fullPage: true });
      console.log(`Saved result-screenshots/${scenario.name}.png (server stopped)`);
    }
  }

  await browser.close();
  console.log("\n=== All screenshots captured ===");
}

captureAll().catch(console.error);