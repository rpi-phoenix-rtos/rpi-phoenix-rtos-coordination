/*
 * bench-common.js -- the result channel of the Phoenix-RTOS browser benchmark suite.
 *
 * NOT part of any benchmark: a small helper the suite's hook scripts load into a benchmark's
 * top-level page (Speedometer, JetStream, MotionMark) or into a wrapper page (Acid3, css3test).
 * It changes nothing a benchmark measures; it only reports.
 *
 * Three channels, because none is reliable alone on the Pi:
 *   1. console.log("BENCH <bench> <run> <seq> <kind> <text>") -- wpe-browser writes console
 *      messages to stdout in window mode (headless runs have shown none reach the UART).
 *   2. document.title, the same strings, one at a time -- the launcher logs every title change
 *      ("WPEB t=<ms> title <title>") in both modes; a short gap between changes so none is
 *      coalesced, and the sequence number keeps consecutive titles distinct.
 *      The last one is "BENCH-DONE <bench> score=<x> status=<ok|partial|error> ... run=<run>".
 *   3. POST /phx-report?bench=&run=&part= with the full JSON, to the suite's own server
 *      (tools/browser/bench/serve.py), which writes it to its results directory.
 * Lines stay short (UART lines over ~1000 bytes are at risk; the launcher's line buffer is 1 KiB
 * and the console line carries the page URL in front): JSON is cut into chunks of CHUNK chars,
 * "json <k>/<n> <part>", which tools/browser/bench/parse-bench-log.py puts together again.
 *
 * The run id comes from the page's query (?run=<id>, set by bench.sh) and is in every line, so a
 * line replayed from an earlier cycle is never taken for this run's.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
(function () {
    'use strict';
    if (window.PhxBench)
        return;

    const params = new URLSearchParams(location.search);
    const run = (params.get('run') || 'manual').replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 64);
    const CHUNK = 450;          /* chars of JSON per line */
    const TITLE_GAP_MS = 25;    /* between title changes */
    const MAX_ERRORS = 40;
    const t0 = performance.now();
    let seq = 0;
    let errors = 0;
    let finished = false;
    const titles = [];
    let titleTimer = null;
    let titleDrained = null;

    function secs() {
        return ((performance.now() - t0) / 1000).toFixed(1);
    }

    function pumpTitle() {
        if (!titles.length) {
            titleTimer = null;
            if (titleDrained) {
                const resolve = titleDrained;
                titleDrained = null;
                resolve();
            }
            return;
        }
        document.title = titles.shift();
        titleTimer = setTimeout(pumpTitle, TITLE_GAP_MS);
    }

    function queueTitle(text) {
        titles.push(text);
        if (!titleTimer)
            titleTimer = setTimeout(pumpTitle, 0);
    }

    function drained() {
        if (!titles.length && !titleTimer)
            return Promise.resolve();
        return new Promise((resolve) => {
            const previous = titleDrained;
            titleDrained = () => { if (previous) previous(); resolve(); };
        });
    }

    /* one result line on every channel; opts.title=false: console only (bulky progress) */
    function line(bench, kind, text, opts) {
        const s = ('BENCH ' + bench + ' ' + run + ' ' + (++seq) + ' ' + kind + (text ? ' ' + text : ''))
            .replace(/[\r\n\t]+/g, ' ').slice(0, 700);
        console.log(s);
        if (!opts || opts.title !== false)
            queueTitle(s);
    }

    function json(bench, kind, value) {
        const s = JSON.stringify(value);
        const n = Math.max(1, Math.ceil(s.length / CHUNK));
        for (let k = 0; k < n; k++)
            line(bench, 'json', kind + ' ' + (k + 1) + '/' + n + ' ' + s.slice(k * CHUNK, (k + 1) * CHUNK));
    }

    function post(bench, part, value) {
        const url = '/phx-report?bench=' + encodeURIComponent(bench) + '&run=' + encodeURIComponent(run) +
            '&part=' + encodeURIComponent(part);
        let body;
        try {
            body = JSON.stringify(value);
        } catch (e) {
            body = JSON.stringify({ unserialisable: String(e) });
        }
        const sent = fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: body })
            .then((r) => r.ok, () => false);
        const timeout = new Promise((resolve) => setTimeout(() => resolve(false), 8000));
        return Promise.race([sent, timeout]);
    }

    function env(bench, extra) {
        const e = {
            wasm: typeof WebAssembly !== 'undefined' ? 1 : 0,
            workers: typeof Worker !== 'undefined' ? 1 : 0,
            sab: typeof SharedArrayBuffer !== 'undefined' ? 1 : 0,
            webgl: typeof WebGLRenderingContext !== 'undefined' ? 1 : 0, /* the API only: a context costs a GL setup */
            webgpu: navigator.gpu ? 1 : 0,
            audio: typeof AudioContext !== 'undefined' ? 1 : 0,
            cores: navigator.hardwareConcurrency || 0,
            view: innerWidth + 'x' + innerHeight,
            dpr: devicePixelRatio,
        };
        Object.assign(e, extra || {});
        line(bench, 'env', Object.entries(e).map(([k, v]) => k + '=' + v).join(' ') + ' ua=' + navigator.userAgent);
    }

    /* the end: chunks first, the POST, then BENCH-DONE as the very last title */
    async function done(bench, summary, full) {
        if (finished)
            return;
        finished = true;
        if (full !== undefined) {
            const ok = await post(bench, 'final', full);
            line(bench, 'report', 'posted=' + (ok ? 1 : 0));
        }
        const kv = Object.entries(summary).map(([k, v]) => k + '=' + String(v).replace(/\s+/g, '_')).join(' ');
        const text = 'BENCH-DONE ' + bench + ' ' + kv + ' run=' + run + ' t=' + secs();
        await drained();
        console.log(text);
        document.title = text;
    }

    function hookErrors(bench) {
        window.addEventListener('error', (ev) => {
            if (errors++ < MAX_ERRORS)
                line(bench, 'page-error', (ev.message || 'error') + ' at ' + (ev.filename || '?') + ':' + (ev.lineno || 0));
        });
        window.addEventListener('unhandledrejection', (ev) => {
            const r = ev.reason;
            if (errors++ < MAX_ERRORS)
                line(bench, 'page-rejection', r && r.stack ? String(r.stack).split('\n').slice(0, 3).join(' | ') : String(r));
        });
    }

    window.PhxBench = { run, params, line, json, post, env, done, secs, hookErrors, get finished() { return finished; } };
})();
