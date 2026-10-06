#!/usr/bin/env node
/*
 * pw-run.mjs -- the host stand-in for wpe-browser, through Playwright: one page in Chromium,
 * Firefox or WebKit (Playwright's builds, headless), logging what the Pi's launcher logs: every
 * title change as "WPEB t=<ms> title <title>" and console messages as "CONSOLE LOG <text>". It
 * exits when the title says BENCH-DONE (status 0), when the page crashes (3) or after --timeout (2).
 *
 *   NODE_PATH=<dir with node_modules> PLAYWRIGHT_BROWSERS_PATH=<browsers> \
 *       node pw-run.mjs [--browser chromium|firefox|webkit] [--timeout S] [--size WxH] [--headed]
 *                       [--env NAME=VALUE]... [--done PREFIX] URL
 *
 * --env sets a variable for the browser's processes (JSC_useJIT=false: the LLInt arm);
 * --done is the title prefix that ends the run (default "BENCH-DONE "; the B9 page: "B9-JS total=").
 *
 * tools/browser/bench/host/run-host-baseline.sh sets both paths (external/browser-bench/host-tools).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
import { createRequire } from 'node:module';
import path from 'node:path';

const require = createRequire(path.join(process.env.NODE_PATH || process.cwd(), 'x.js'));
const playwright = require('playwright');

const args = process.argv.slice(2);
const opt = { browser: 'chromium', timeout: 3600, size: '1280x800', headed: false, url: null, env: {}, done: 'BENCH-DONE ' };
for (let i = 0; i < args.length; i++) {
    const a = args[i];
    if (a === '--browser') opt.browser = args[++i];
    else if (a === '--timeout') opt.timeout = parseInt(args[++i], 10);
    else if (a === '--size') opt.size = args[++i];
    else if (a === '--headed') opt.headed = true;
    else if (a === '--env') { const [k, ...v] = args[++i].split('='); opt.env[k] = v.join('='); }
    else if (a === '--done') opt.done = args[++i];
    else opt.url = a;
}
if (!opt.url) {
    console.error('usage: pw-run.mjs [--browser chromium|firefox|webkit] [--timeout S] [--size WxH] [--headed] URL');
    process.exit(1);
}
const t0 = Date.now();
const log = (s) => process.stdout.write('WPEB t=' + (Date.now() - t0) + ' ' + s + '\n');
const [width, height] = opt.size.split('x').map((x) => parseInt(x, 10));

const engine = playwright[opt.browser];
const browser = await engine.launch({ headless: !opt.headed, env: { ...process.env, ...opt.env } });
const page = await browser.newPage({ viewport: { width, height } });
log('start browser=' + opt.browser + ' version=' + browser.version() + ' env=' + (Object.entries(opt.env).map(([k, v]) => k + '=' + v).join(',') || 'none') + ' uri=' + opt.url);

let lastTitle = null;
let resolveEnd;
const end = new Promise((resolve) => { resolveEnd = resolve; });
page.on('console', (msg) => process.stdout.write('CONSOLE ' + msg.type().toUpperCase() + ' ' + msg.text() + '\n'));
page.on('pageerror', (err) => log('page-error ' + String(err).split('\n')[0]));
page.on('crash', () => { log('web-process-terminated reason=crashed'); resolveEnd(3); });
/* Playwright has no title event: poll it, as the launcher's notify::title would see it */
const poll = setInterval(async () => {
    let title;
    try { title = await page.title(); } catch (e) { return; }
    if (title !== lastTitle) {
        lastTitle = title;
        log('title ' + title);
        if (title.startsWith(opt.done))
            resolveEnd(0);
    }
}, 5);
const timer = setTimeout(() => { log('timeout after ' + opt.timeout + ' s'); resolveEnd(2); }, opt.timeout * 1000);
await page.goto(opt.url, { waitUntil: 'commit', timeout: 120000 });
const status = await end;
clearInterval(poll);
clearTimeout(timer);
log('exit status=' + status);
await browser.close();
process.exit(status);
