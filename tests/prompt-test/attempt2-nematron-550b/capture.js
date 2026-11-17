const { chromium } = require('playwright');
const { spawn } = require('child_process');
const fs = require('fs');
const path = require('path');

const BASE_URL = 'http://localhost:8202';
const SCREENSHOT_DIR = path.join(__dirname, 'result-screenshots');

if (!fs.existsSync(SCREENSHOT_DIR)) {
    fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });
}

function runCommand(cmd, args, options = {}) {
    return new Promise((resolve, reject) => {
        const proc = spawn(cmd, args, { ...options, stdio: 'pipe' });
        let stdout = '';
        let stderr = '';
        proc.stdout.on('data', d => stdout += d.toString());
        proc.stderr.on('data', d => stderr += d.toString());
        proc.on('close', code => {
            if (code === 0) resolve(stdout);
            else reject(new Error(`Command failed (${code}): ${stderr}`));
        });
    });
}

async function waitFor(page, condition, timeout = 10000) {
    const start = Date.now();
    while (Date.now() - start < timeout) {
        try {
            if (await condition()) return;
        } catch (e) {}
        await page.waitForTimeout(100);
    }
    throw new Error('Timeout waiting for condition');
}

async function captureState(name, page, width = 1280, height = 900) {
    await page.setViewportSize({ width, height });
    await page.waitForTimeout(500);
    const filepath = path.join(SCREENSHOT_DIR, `${name}.png`);
    await page.screenshot({ path: filepath, fullPage: true });
    console.log(`Captured: ${name}.png`);
}

async function startServer() {
    const serverEnv = { ...process.env, LUMPI_LAN_KEY: 'test' };
    const server = spawn('python3', ['laptop/dashboard_server.py'], {
        cwd: __dirname,
        env: serverEnv,
        stdio: ['ignore', 'pipe', 'pipe']
    });
    server.stdout.on('data', d => console.log('[SERVER]', d.toString().trim()));
    server.stderr.on('data', d => console.error('[SERVER ERR]', d.toString().trim()));
    await new Promise(r => setTimeout(r, 1500));
    return server;
}

async function stopServer(server) {
    return new Promise((resolve) => {
        server.on('close', resolve);
        server.kill('SIGTERM');
        // Force kill after 2 seconds if not closed
        setTimeout(() => {
            server.kill('SIGKILL');
            resolve();
        }, 2000);
    });
}

async function waitForConnection(page) {
    // Wait for the page to fully load and first poll to complete
    await page.waitForTimeout(3000);
    await waitFor(page, async () => {
        const text = await page.textContent('#connection');
        console.log('[DEBUG] Connection text:', text);
        return text && text.includes('Server connected');
    }, 20000);
}

async function main() {
    console.log('Starting LUMPI Dashboard screenshot capture...');

    // Kill any existing server
    await runCommand('pkill', ['-f', 'dashboard_server.py']).catch(() => {});
    await new Promise(r => setTimeout(r, 1000));

    const browser = await chromium.launch({ headless: true });
    const page = await browser.newPage();

    // ============ State 1: Nothing received (UNKNOWN) ============
    let server = await startServer();
    await page.goto(BASE_URL, { waitUntil: 'networkidle' });
    await waitForConnection(page);
    await captureState('01_unknown_empty', page);

    // ============ State 2: All online (normal.jsonl) ============
    console.log('\n--- Replaying normal.jsonl ---');
    await runCommand('python3', ['laptop/replay_events.py', 'tests/fixtures/dashboard/normal.jsonl'], { cwd: __dirname });
    await page.waitForTimeout(2000);
    await captureState('02_online_full', page);
    await captureState('02b_online_top', page);

    // ============ State 3: Doll 2 stale (doll2_dies.jsonl, wait 30s) ============
    console.log('\n--- Replaying doll2_dies.jsonl ---');
    await stopServer(server);
    server = await startServer();
    await page.reload({ waitUntil: 'networkidle' });
    await waitForConnection(page);

    await runCommand('python3', ['laptop/replay_events.py', 'tests/fixtures/dashboard/doll2_dies.jsonl'], { cwd: __dirname });
    console.log('Waiting 35 seconds for Doll 2 to become stale...');
    await page.waitForTimeout(35000);
    await captureState('03_stale_doll2', page);

    // ============ State 4: Doll 2 offline (wait 60s more) ============
    console.log('Waiting 30 more seconds for Doll 2 to become offline...');
    await page.waitForTimeout(30000);
    await captureState('04_offline_doll2', page);
    await captureState('04b_offline_timeline_gap', page);

    // ============ State 5: Server lost ============
    console.log('\n--- Stopping server for disconnected state ---');
    await stopServer(server);
    await page.waitForTimeout(2000);
    await captureState('05_server_disconnected', page);

    // ============ State 6: Dark room / failed sensor ============
    console.log('\n--- Replaying sensor_fail.jsonl ---');
    server = await startServer();
    await page.reload({ waitUntil: 'networkidle' });
    await waitForConnection(page);

    await runCommand('python3', ['laptop/replay_events.py', 'tests/fixtures/dashboard/sensor_fail.jsonl'], { cwd: __dirname });
    await page.waitForTimeout(2000);
    await captureState('06_dark_room_sensor_fail_top', page);
    await captureState('06b_dark_room_trends', page);

    // ============ State 7: Chart hover ============
    console.log('\n--- Capturing chart hover ---');
    const lightChart = await page.$('#chart-light');
    if (lightChart) {
        const box = await lightChart.boundingBox();
        if (box) {
            await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
            await page.waitForTimeout(500);
            await captureState('07_chart_hover_tooltip', page);
        }
    }

    // ============ State 8: Flaky Wi-Fi ============
    console.log('\n--- Replaying flaky_wifi.jsonl ---');
    await stopServer(server);
    server = await startServer();
    await page.reload({ waitUntil: 'networkidle' });
    await waitForConnection(page);

    await runCommand('python3', ['laptop/replay_events.py', 'tests/fixtures/dashboard/flaky_wifi.jsonl'], { cwd: __dirname });
    await page.waitForTimeout(2000);
    await captureState('08_flaky_wifi_trends', page);

    // Switch to 5 min range
    await page.click('input[value="5"]');
    await page.waitForTimeout(1000);
    await captureState('08b_range_5min', page);

    // Switch back to 15 min
    await page.click('input[value="15"]');
    await page.waitForTimeout(500);

    // ============ State 9: Event detail open ============
    console.log('\n--- Opening event JSON detail ---');
    const firstDetails = await page.$('#events-body details summary');
    if (firstDetails) {
        await firstDetails.click();
        await page.waitForTimeout(500);
        await captureState('09_events_json_open', page);
        await firstDetails.click();
    }

    // ============ State 10: Bad packets (developer panel) ============
    console.log('\n--- Replaying bad_packets.jsonl ---');
    await stopServer(server);
    server = await startServer();
    await page.reload({ waitUntil: 'networkidle' });
    await waitForConnection(page);

    await runCommand('python3', ['laptop/replay_events.py', 'tests/fixtures/dashboard/bad_packets.jsonl'], { cwd: __dirname });
    await page.waitForTimeout(2000);

    const devSummary = await page.$('#dev-panel summary');
    if (devSummary) {
        await devSummary.click();
        await page.waitForTimeout(500);
        const firstError = await page.$('#error-list details summary');
        if (firstError) {
            await firstError.click();
            await page.waitForTimeout(500);
            await captureState('10_developer_panel_error_open', page);
        }
    }

    // ============ State 11: Phone width (390px) ============
    console.log('\n--- Capturing mobile view (390px) ---');
    await stopServer(server);
    server = await startServer();
    await page.reload({ waitUntil: 'networkidle' });
    await waitForConnection(page);

    await runCommand('python3', ['laptop/replay_events.py', 'tests/fixtures/dashboard/normal.jsonl'], { cwd: __dirname });
    await page.waitForTimeout(2000);

    await captureState('11_mobile_390', page, 390, 900);

    // Cleanup
    await stopServer(server);
    await browser.close();
    console.log('\nAll screenshots captured!');
}

main().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});