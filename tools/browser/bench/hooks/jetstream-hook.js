/*
 * jetstream-hook.js -- reports a JetStream 2.2 run through bench-common.js (Phoenix-RTOS browser
 * benchmark suite). NOT part of JetStream.
 *
 * Loaded by bench.html (a copy of JetStream's index.html with bench-common.js and this script
 * added after JetStreamDriver.js). bench.sh opens bench.html?report=true, so JetStream starts by
 * itself 4 s after its resources are loaded and POSTs its own resultsJSON() to /report (the
 * suite's server keeps it). The hook adds, per benchmark, a start line (hang attribution) and a
 * result line, and the summary at the end.
 *
 * Deviations from a stock run, both reported in the lines and off with ?phx-strict=1:
 *   - one benchmark failing (an exception, or no result within ?phx-test-timeout=S, default
 *     900 s) does not end the run: it is reported ("test-error"/"test-timeout") and the rest
 *     still run. The stock driver stops at the first failure and shows no score at all. With a
 *     failure the overall score is "partial" (the geomean of the benchmarks that finished).
 *   - none for the WebAssembly benchmarks: JetStream itself leaves them out when the page has no
 *     WebAssembly (Phoenix's JSC: useWasm=false); the hook says so ("skip <name> reason=no-wasm")
 *     and reports both the score over what ran and the JS-only score (without the *-wasm
 *     benchmarks), the number to compare with a browser that ran them.
 *
 * Lines: env, plan count= wasm=0|1 tests=..., skip <name> reason=..., test-start i/N <name> t=,
 * test i/N <name> score= first= worst= avg= (or startup= runtime=) wall_s=, test-error, test-timeout,
 * result score= js_score= ran= failed= skipped= status=, category <name> score=,
 * json tests k/n ..., BENCH-DONE jetstream score=<x> js_score=<y> status=ok|partial|error.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
(function () {
    'use strict';
    const B = 'jetstream';
    const P = window.PhxBench;
    const q = P.params;
    const strict = q.get('phx-strict') === '1';
    const testTimeoutMs = (parseInt(q.get('phx-test-timeout') || '900', 10) || 900) * 1000;

    P.hookErrors(B);
    P.env(B);

    const geomean = (xs) => (xs.length ? Math.pow(xs.reduce((a, b) => a * b, 1), 1 / xs.length) : NaN);
    const fmt = (x) => (typeof x === 'number' && isFinite(x) ? x.toFixed(3) : 'NaN');
    const results = [];
    let failed = 0;

    /* what JetStream would have run with WebAssembly: the plans it left out */
    const planned = JetStream.benchmarks.map((b) => b.name);
    const skipped = [];
    try {
        if (typeof WebAssembly === 'undefined' && !q.has('test') && typeof testsByGroup !== 'undefined') {
            for (const name of testsByGroup.get(Symbol.for('Wasm')) || [])
                skipped.push(name);
        }
    } catch (e) { /* testsByGroup is JetStream's own; a different driver has none */ }
    P.line(B, 'plan', 'count=' + planned.length + ' wasm=' + (typeof WebAssembly !== 'undefined' ? 1 : 0) +
        ' strict=' + (strict ? 1 : 0) + ' test_timeout_s=' + testTimeoutMs / 1000);
    for (const name of skipped)
        P.line(B, 'skip', name + ' reason=no-wasm');

    const total = planned.length;
    let started = 0;
    JetStream.benchmarks.forEach((bench) => {
        const run = bench.run.bind(bench);
        const after = bench.updateUIAfterRun.bind(bench);
        let label = bench.name;
        bench.updateUIAfterRun = function () {
            if (bench.phxFailed)
                return;
            after();
        };
        bench.run = async function () {
            label = (++started) + '/' + total + ' ' + bench.name;
            P.line(B, 'test-start', label + ' t=' + P.secs());
            const t0 = performance.now();
            let timer;
            const timeout = new Promise((resolve, reject) => {
                timer = setTimeout(() => reject(new Error('phx-timeout')), testTimeoutMs);
            });
            try {
                await Promise.race([run(), timeout]);
                clearTimeout(timer);
            } catch (e) {
                clearTimeout(timer);
                failed++;
                bench.phxFailed = true;
                const timedOut = e && e.message === 'phx-timeout';
                const msg = e && e.stack ? String(e.stack).split('\n').slice(0, 3).join(' | ') : String(e);
                P.line(B, timedOut ? 'test-timeout' : 'test-error', label + ' after_s=' + ((performance.now() - t0) / 1000).toFixed(1) +
                    (timedOut ? '' : ' ' + msg));
                results.push({ name: bench.name, error: timedOut ? 'timeout' : msg });
                if (timedOut) {
                    /* a late answer from the abandoned frame must not resolve the next benchmark */
                    try { document.getElementById('magic').contentDocument.body.textContent = ''; } catch (x) { }
                }
                if (strict)
                    throw e;
                return;
            }
            const sub = bench.subTimes();
            const r = { name: bench.name, score: bench.score, wall_s: +((performance.now() - t0) / 1000).toFixed(2) };
            for (const [k, v] of Object.entries(sub))
                r[k.toLowerCase()] = v;
            results.push(r);
            P.line(B, 'test', label + ' score=' + fmt(bench.score) + ' ' +
                Object.entries(sub).map(([k, v]) => k.toLowerCase() + '=' + fmt(v)).join(' ') + ' wall_s=' + r.wall_s);
        };
    });

    async function finish(error) {
        const ok = results.filter((r) => typeof r.score === 'number' && isFinite(r.score));
        const js = ok.filter((r) => !/-wasm$/.test(r.name));
        const score = geomean(ok.map((r) => r.score));
        const jsScore = geomean(js.map((r) => r.score));
        const status = error ? 'error' : (failed || ok.length !== total) ? 'partial' : 'ok';
        P.line(B, 'result', 'score=' + fmt(score) + ' js_score=' + fmt(jsScore) + ' ran=' + ok.length + '/' + total +
            ' failed=' + failed + ' skipped=' + skipped.length + ' status=' + status);
        /* the category geomeans (JetStream's "d" view) */
        const cats = {};
        for (const r of ok) {
            for (const k of ['first', 'worst', 'average', 'startup', 'runtime']) {
                if (typeof r[k] === 'number')
                    (cats[k] = cats[k] || []).push(r[k]);
            }
        }
        for (const [k, v] of Object.entries(cats))
            P.line(B, 'category', k + ' score=' + fmt(geomean(v)) + ' n=' + v.length);
        P.json(B, 'tests', results.map((r) => r.error ? [r.name, 'ERR', r.error.slice(0, 80)] :
            [r.name, +r.score.toFixed(3), +(r.first ?? r.startup ?? 0).toFixed(3), +(r.worst ?? r.runtime ?? 0).toFixed(3),
                +(r.average ?? 0).toFixed(3), r.wall_s]));
        await P.done(B, { score: fmt(score), js_score: fmt(jsScore), status, ran: ok.length + '/' + total, failed, skipped: skipped.length },
            { bench: B, run: P.run, status, score, js_score: jsScore, skipped, error: error ? String(error) : null, results,
                userAgent: navigator.userAgent });
    }

    const start = JetStream.start.bind(JetStream);
    JetStream.start = async function () {
        P.line(B, 'start', 'tests=' + total + ' t=' + P.secs());
        try {
            await start();
        } catch (e) {
            await finish(e && e.message ? e.message : String(e));
            return;
        }
        await finish(null);
    };

    /* a page that never starts (a resource failed to load: index.html refuses to run) */
    setTimeout(function check() {
        if (P.finished)
            return;
        const status = document.getElementById('status');
        if (status && status.classList.contains('error')) {
            P.line(B, 'error', 'load: ' + status.textContent.trim().slice(0, 200));
            P.done(B, { score: 'NaN', status: 'error', reason: 'load-failed' }, { bench: B, run: P.run, error: 'load-failed' });
            return;
        }
        setTimeout(check, 5000);
    }, 5000);

    P.line(B, 'hooked', 'report=' + (q.get('report') || '0') + ' tests=' + (q.getAll('test').join(',') || 'all'));
})();
