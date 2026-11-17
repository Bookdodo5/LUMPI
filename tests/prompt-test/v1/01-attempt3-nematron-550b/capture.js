#!/usr/bin/env node
/* Capture all 11 state screenshots for LUMPI Teacher Dashboard */

const { chromium } = require('playwright');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const BASE_URL = 'http://localhost:8203';
const SCREENSHOT_DIR = path.join(__dirname, 'result-screenshots');
const SCENARIOS_DIR = path.join(__dirname, 'tests', 'fixtures', 'dashboard');
const SERVER_SCRIPT = path.join(__dirname, 'laptop', 'dashboard_server.py');
const REPLAY_SCRIPT = path.join(__dirname, 'laptop', 'replay_events.py');

// Helper to post event directly from Node
async function postEvent(event) {
  const res = await fetch(`${BASE_URL}/api/telemetry`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-LUMPI-Token': 'test' },
    body: JSON.stringify(event)
  });
  const text = await res.text();
  console.log(`POST ${event.node_id}/${event.event_type}: ${res.status} ${text}`);
  return res;
}

// Ensure screenshot directory exists
if (!fs.existsSync(SCREENSHOT_DIR)) {
  fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
}

// Helper to sleep
const sleep = (ms) => new Promise(resolve => setTimeout(resolve, ms));

// Helper to run a command and wait for it
function runCommand(cmd, args, options = {}) {
  return new Promise((resolve, reject) => {
    const proc = spawn(cmd, args, { ...options, stdio: 'pipe' });
    let stdout = '';
    let stderr = '';
    proc.stdout.on('data', data => stdout += data.toString());
    proc.stderr.on('data', data => stderr += data.toString());
    proc.on('close', code => {
      if (code === 0) resolve({ stdout, stderr });
      else reject(new Error(`Command failed with code ${code}: ${stderr}`));
    });
  });
}

// Start the server
let serverProcess = null;

async function startServer() {
  console.log('Starting server...');
  serverProcess = spawn('python3', [SERVER_SCRIPT], {
    env: { ...process.env, LUMPI_LAN_KEY: 'test' },
    stdio: ['ignore', 'pipe', 'pipe']
  });
  
  serverProcess.stdout.on('data', data => console.log(`SERVER: ${data.toString().trim()}`));
  serverProcess.stderr.on('data', data => console.error(`SERVER ERR: ${data.toString().trim()}`));
  serverProcess.on('exit', (code, signal) => console.log(`Server exited with code ${code}, signal ${signal}`));
  
  // Wait for server to be ready
  await sleep(3000);
  
  // Test if server is responding
  for (let i = 0; i < 10; i++) {
    try {
      const response = await fetch(BASE_URL);
      console.log(`Health check attempt ${i+1}: status ${response.status}`);
      if (response.ok) {
        console.log('Server started successfully');
        return;
      }
    } catch (e) {
      console.log(`Health check attempt ${i+1} failed: ${e.message}`);
      await sleep(500);
    }
  }
  throw new Error('Server failed to start');
}

async function stopServer() {
  if (serverProcess) {
    console.log('Stopping server...');
    serverProcess.kill('SIGTERM');
    await sleep(500);
    serverProcess = null;
  }
}

async function replayScenario(scenarioFile) {
  console.log(`Replaying ${scenarioFile}...`);
  const { stdout, stderr } = await runCommand('python3', [REPLAY_SCRIPT, scenarioFile], {
    env: { ...process.env, LUMPI_LAN_KEY: 'test' }
  });
  console.log(stdout);
  if (stderr) console.error(stderr);
}

async function captureScreenshot(page, name, width = 1280, height = 900) {
  await page.setViewportSize({ width, height });
  await sleep(500); // Let things settle
  const filepath = path.join(SCREENSHOT_DIR, name);
  await page.screenshot({ path: filepath, fullPage: true });
  console.log(`Captured ${name}`);
}

async function waitForNodeStatus(page, nodeId, expectedStatus, timeout = 60000) {
  const start = Date.now();
  while (Date.now() - start < timeout) {
    // Check the badge on the page (DOM) - this is what the user sees
    const badge = await page.locator(`.card[data-node="${nodeId}"] .badge`);
    const text = await badge.textContent();
    if (text && text.trim() === expectedStatus) {
      return true;
    }
    await sleep(500);
  }
  // Debug: check what the API returns via Node.js fetch
  try {
    const response = await fetch(`${BASE_URL}/api/dashboard/nodes`);
    const data = await response.json();
    console.log(`Debug API for node ${nodeId}:`, data.find(n => n.node_id === nodeId));
  } catch (e) {
    console.log('Debug API failed:', e.message);
  }
  throw new Error(`Timeout waiting for node ${nodeId} to become ${expectedStatus}`);
}

async function main() {
  console.log('=== Starting LUMPI Dashboard Screenshot Capture ===\n');
  
  await startServer();
  
  const browser = await chromium.launch({ headless: true });
  const context = await browser.newContext();
  const page = await context.newPage();
  
  try {
    // Navigate to dashboard
    await page.goto(BASE_URL, { waitUntil: 'networkidle' });
    await sleep(1000);
    
    // ========================================
    // State 1: Nothing received (01_unknown_empty.png)
    // ========================================
    console.log('\n--- State 1: Nothing received ---');
    await captureScreenshot(page, '01_unknown_empty.png');
    
    // ========================================
    // State 2: All online (02_online_full.png, 02b_online_top.png)
    // ========================================
    console.log('\n--- State 2: All online ---');
    // Send fresh events with current timestamps to ensure all nodes are online
    const now = Math.floor(Date.now() / 1000);
    console.log(`Current time (Node): ${now}`);
    
    // Check server time via API
    const serverTimeRes = await fetch(`${BASE_URL}/api/dashboard/nodes`);
    const serverTimeData = await serverTimeRes.json();
    console.log('Server nodes API response:', JSON.stringify(serverTimeData, null, 2));
    
    await postEvent({
      event_id: "lumpi_fresh/1",
      node_id: 0,
      event_type: "room",
      recorded_at: now,
      activity: null,
      data: { lux: 250.0, distance_cm: 150.0 }
    });
    await postEvent({
      event_id: "lumpi_fresh/2",
      node_id: 1,
      event_type: "status",
      recorded_at: now,
      activity: "recording",
      data: { uid: "04A1B2C3", passage: 1, attempt: 1, fsr: 3100 }
    });
    await postEvent({
      event_id: "lumpi_fresh/3",
      node_id: 2,
      event_type: "status",
      recorded_at: now,
      activity: "recording",
      data: { uid: "04D4E5F6", passage: 2, attempt: 1, fsr: 2950 }
    });
    
    // Check API immediately after posting
    const afterPostRes = await fetch(`${BASE_URL}/api/dashboard/nodes`);
    const afterPostData = await afterPostRes.json();
    console.log('Nodes after POST:', JSON.stringify(afterPostData, null, 2));
    
    // Check server time directly
    const serverTimeCheck = await fetch(`${BASE_URL}/api/dashboard/nodes`);
    const serverTimeCheckData = await serverTimeCheck.json();
    console.log('Server nodes check:', JSON.stringify(serverTimeCheckData, null, 2));
    
    await sleep(2000); // Wait for polling to pick up
    
    // Direct API check before waitForNodeStatus
    const directCheck = await fetch(`${BASE_URL}/api/dashboard/nodes`);
    const directCheckData = await directCheck.json();
    console.log('Direct API check before waitForNodeStatus:', JSON.stringify(directCheckData, null, 2));
    
    // Debug: check what the page sees
    const pageBadge0 = await page.locator('.card[data-node="0"] .badge').textContent();
    const pageBadge1 = await page.locator('.card[data-node="1"] .badge').textContent();
    const pageBadge2 = await page.locator('.card[data-node="2"] .badge').textContent();
    console.log('Page badges before waitForNodeStatus:', { node0: pageBadge0, node1: pageBadge1, node2: pageBadge2 });
    
    // Check if page has connection status
    const connText = await page.locator('#connection').textContent();
    console.log('Page connection status:', connText);
    
    // Check if poll is running by evaluating a variable
    const pollTimer = await page.evaluate(() => window.pollTimer);
    console.log('Page pollTimer:', pollTimer);
    
    // Check for JS errors
    const consoleMessages = [];
    page.on('console', msg => consoleMessages.push(`${msg.type()}: ${msg.text()}`));
    page.on('pageerror', err => consoleMessages.push(`ERROR: ${err.message}`));
    
    // Wait a bit for any console messages
    await sleep(1000);
    console.log('Console messages:', consoleMessages);
    
    // Check if app.js loaded
    const scriptsLoaded = await page.evaluate(() => {
      return {
        drawLineChartFull: typeof drawLineChartFull,
        drawTimeline: typeof drawTimeline,
        drawActivityStrip: typeof drawActivityStrip,
        poll: typeof poll,
        POLL_MS: typeof POLL_MS,
        NODE_NAMES: typeof NODE_NAMES,
        chosenMinutes: typeof chosenMinutes,
        drawnEventsKey: typeof drawnEventsKey,
        drawnErrorsKey: typeof drawnErrorsKey,
        isHoveringTrends: typeof isHoveringTrends
      };
    });
    console.log('Scripts loaded:', scriptsLoaded);
    
    // Check if DOMContentLoaded fired
    const domReady = await page.evaluate(() => document.readyState);
    console.log('Document readyState:', domReady);
    
    // Check if script tags exist
    const scripts = await page.evaluate(() => {
      const scripts = document.querySelectorAll('script[src]');
      return Array.from(scripts).map(s => s.src);
    });
    console.log('Script tags:', scripts);
    
    // Check for any global errors
    const globalErrors = await page.evaluate(() => {
      return window.onerror ? 'has onerror' : 'no onerror';
    });
    console.log('Global error handler:', globalErrors);
    
    // Try to trigger poll manually
    await page.evaluate(() => {
      if (typeof poll === 'function') {
        poll();
      }
    });
    console.log('Manually triggered poll');
    
    await sleep(2000);
    
    const pageBadge0After = await page.locator('.card[data-node="0"] .badge').textContent();
    const pageBadge1After = await page.locator('.card[data-node="1"] .badge').textContent();
    const pageBadge2After = await page.locator('.card[data-node="2"] .badge').textContent();
    console.log('Page badges after manual poll:', { node0: pageBadge0After, node1: pageBadge1After, node2: pageBadge2After });
    
    const connTextAfter = await page.locator('#connection').textContent();
    console.log('Page connection status after manual poll:', connTextAfter);
    
    // Check network requests
    const networkLogs = [];
    page.on('request', req => networkLogs.push(`${req.method()} ${req.url()}`));
    page.on('response', res => networkLogs.push(`${res.status()} ${res.url()}`));
    await sleep(1000);
    console.log('Network logs:', networkLogs.filter(l => l.includes('api')).slice(0, 20));
    
    await waitForNodeStatus(page, 0, 'ONLINE');
    await waitForNodeStatus(page, 1, 'ONLINE');
    await waitForNodeStatus(page, 2, 'ONLINE');
    await captureScreenshot(page, '02_online_full.png');
    // Also capture top portion (02b_online_top.png) - same as full for now
    await captureScreenshot(page, '02b_online_top.png');
    
    // ========================================
    // State 3: One doll stale (03_stale_doll2.png)
    // ========================================
    console.log('\n--- State 3: Doll 2 stale ---');
    await replayScenario(path.join(SCENARIOS_DIR, 'doll2_dies.jsonl'));
    // Wait 35 seconds for doll 2 to go stale (30s threshold)
    console.log('Waiting 35s for doll 2 to go stale...');
    await sleep(35000);
    await waitForNodeStatus(page, 2, 'STALE');
    await captureScreenshot(page, '03_stale_doll2.png');
    
    // ========================================
    // State 4: One doll offline (04_offline_doll2.png, 04b_offline_timeline_gap.png)
    // ========================================
    console.log('\n--- State 4: Doll 2 offline ---');
    // Wait another 30 seconds for offline (60s total)
    console.log('Waiting 30s more for doll 2 to go offline...');
    await sleep(30000);
    await waitForNodeStatus(page, 2, 'OFFLINE');
    await captureScreenshot(page, '04_offline_doll2.png');
    await captureScreenshot(page, '04b_offline_timeline_gap.png');
    
    // ========================================
    // State 5: Server lost (05_server_disconnected.png)
    // ========================================
    console.log('\n--- State 5: Server disconnected ---');
    await stopServer();
    await sleep(2000); // Wait for page to detect disconnect
    await captureScreenshot(page, '05_server_disconnected.png');
    
    // Restart server
    await startServer();
    await sleep(1000);
    await page.reload({ waitUntil: 'networkidle' });
    await sleep(2000);
    
    // ========================================
    // State 6: Dark room / failed sensor (06_dark_room_sensor_fail_top.png, 06b_dark_room_trends.png)
    // ========================================
    console.log('\n--- State 6: Dark room / sensor fail ---');
    await replayScenario(path.join(SCENARIOS_DIR, 'sensor_fail.jsonl'));
    await sleep(2000);
    await captureScreenshot(page, '06_dark_room_sensor_fail_top.png');
    await captureScreenshot(page, '06b_dark_room_trends.png');
    
    // ========================================
    // State 7: Chart hover (07_chart_hover_tooltip.png)
    // ========================================
    console.log('\n--- State 7: Chart hover ---');
    // Hover over the lux chart
    const chartLux = page.locator('#chart-lux');
    await chartLux.hover({ position: { x: 300, y: 100 } });
    await sleep(500);
    await captureScreenshot(page, '07_chart_hover_tooltip.png');
    
    // ========================================
    // State 8: Flaky Wi-Fi (08_flaky_wifi_trends.png, 08b_range_5min.png)
    // ========================================
    console.log('\n--- State 8: Flaky Wi-Fi ---');
    await replayScenario(path.join(SCENARIOS_DIR, 'flaky_wifi.jsonl'));
    await sleep(2000);
    await captureScreenshot(page, '08_flaky_wifi_trends.png');
    
    // Switch to 5 min range
    await page.locator('.range input[value="5"]').click();
    await sleep(1000);
    await captureScreenshot(page, '08b_range_5min.png');
    // Switch back to 15 min
    await page.locator('.range input[value="15"]').click();
    await sleep(1000);
    
    // ========================================
    // State 9: Event detail (09_events_json_open.png)
    // ========================================
    console.log('\n--- State 9: Event detail open ---');
    // Click the first JSON details in the events table
    await page.locator('#events-body details summary').first().click();
    await sleep(500);
    await captureScreenshot(page, '09_events_json_open.png');
    
    // ========================================
    // State 10: Bad packets (10_developer_panel_error_open.png)
    // ========================================
    console.log('\n--- State 10: Bad packets / developer panel ---');
    await replayScenario(path.join(SCENARIOS_DIR, 'bad_packets.jsonl'));
    await sleep(2000);
    // Open developer panel
    await page.locator('#dev-panel summary').click();
    await sleep(500);
    // Open first error
    await page.locator('#error-list details summary').first().click();
    await sleep(500);
    await captureScreenshot(page, '10_developer_panel_error_open.png');
    
    // ========================================
    // State 11: Phone width (11_mobile_390.png)
    // ========================================
    console.log('\n--- State 11: Phone width (390px) ---');
    await captureScreenshot(page, '11_mobile_390.png', 390, 900);
    
    console.log('\n=== All screenshots captured successfully! ===');
    
  } catch (error) {
    console.error('Error during capture:', error);
    throw error;
  } finally {
    await browser.close();
    await stopServer();
  }
}

main().catch(e => {
  console.error('Fatal error:', e);
  process.exit(1);
});