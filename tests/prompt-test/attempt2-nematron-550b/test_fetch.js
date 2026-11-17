const { chromium } = require('playwright');

async function test() {
    const browser = await chromium.launch({ headless: true });
    const page = await browser.newPage();
    
    // Listen for console messages
    page.on('console', msg => console.log('[BROWSER]', msg.type(), msg.text()));
    page.on('pageerror', err => console.log('[BROWSER ERROR]', err.message));
    
    // Listen for network failures
    page.on('requestfailed', req => console.log('[REQUEST FAILED]', req.url(), req.failure()));
    page.on('response', resp => {
        if (!resp.ok()) console.log('[RESPONSE]', resp.status(), resp.url());
    });

    await page.goto('http://localhost:8202/', { waitUntil: 'networkidle' });
    
    // Wait a bit
    await page.waitForTimeout(5000);
    
    // Check connection text
    const text = await page.textContent('#connection');
    console.log('Connection text:', text);
    
    // Try fetching from page context
    const result = await page.evaluate(async () => {
        try {
            const resp = await fetch('/api/dashboard/nodes', { cache: 'no-store' });
            const data = await resp.json();
            return { ok: resp.ok, data };
        } catch (e) {
            return { error: e.message };
        }
    });
    console.log('Fetch result:', JSON.stringify(result));
    
    await browser.close();
}

test().catch(console.error);