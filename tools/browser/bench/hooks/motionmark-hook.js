/*
 * motionmark-hook.js -- starts a MotionMark 1.3.2 run by itself and reports it through
 * bench-common.js (Phoenix-RTOS browser benchmark suite). NOT part of MotionMark.
 *
 * Loaded (defer, after MotionMark's own scripts) by MotionMark/bench.html, a copy of index.html
 * with bench-common.js and this script added. MotionMark's page first measures the display's
 * frame rate (300 requestAnimationFrame callbacks) and maps it to 15/30/45/60/90/120/144 fps,
 * which becomes the target frame rate the score is measured against; the hook waits for that,
 * then presses "Run Benchmark" (benchmarkController.startBenchmark()) -- the stock path.
 *
 * Query knobs (each makes the run non-official, reported as official=0):
 *   phx-test-interval=S   seconds per test instead of 30 (a quick pipeline check)
 *   phx-frame-rate=F      the target frame rate instead of the measured one
 *   phx-post-samples=1    POST the raw frame samples as well (large)
 *
 * Lines: env, framerate detected=<fps>, start interval_s= fps= official=, test-start <suite>/<test>
 * t=, test <test> score= low= high= t=, result score= low= high= fps= official=, test-result <test>
 * score=, json tests k/n ..., BENCH-DONE motionmark score=<x> fps=<f> status=ok official=0|1.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
(function () {
    'use strict';
    const B = 'motionmark';
    const P = window.PhxBench;
    const q = P.params;
    const J = Strings.json;
    const fmt = (x) => (typeof x === 'number' && isFinite(x) ? x.toFixed(2) : 'NaN');
    const interval = q.get('phx-test-interval');
    const frameRate = q.get('phx-frame-rate');
    const official = !interval && !frameRate;

    P.hookErrors(B);
    P.env(B);

    const client = window.benchmarkRunnerClient;
    const orig = {
        didRunTest: client.didRunTest.bind(client),
        didFinishLastIteration: client.didFinishLastIteration.bind(client),
    };
    let current = '?';

    client.willRunTest = function (suite, test) {
        current = test.name;
        P.line(B, 'test-start', suite.name + '/' + test.name + ' t=' + P.secs());
    };

    client.didRunTest = function (testData) {
        orig.didRunTest(testData);
        const r = testData[J.result] || {};
        P.line(B, 'test', current.replace(/\s+/g, '_') + ' score=' + fmt(r[J.score]) + ' low=' + fmt(r[J.scoreLowerBound]) +
            ' high=' + fmt(r[J.scoreUpperBound]) + ' t=' + P.secs());
    };

    client.didFinishLastIteration = function () {
        orig.didFinishLastIteration();
        const dashboard = client.results;
        const fps = dashboard._targetFrameRate;
        const score = dashboard.score;
        P.line(B, 'result', 'score=' + fmt(score) + ' low=' + fmt(dashboard.scoreLowerBound) + ' high=' +
            fmt(dashboard.scoreUpperBound) + ' fps=' + fps + ' official=' + (official ? 1 : 0));
        const tests = [];
        const iteration = dashboard.results[0] || {};
        for (const [suite, suiteTests] of Object.entries(iteration[J.results.tests] || {})) {
            for (const [name, r] of Object.entries(suiteTests)) {
                tests.push([suite, name, +fmt(r[J.score]), +fmt(r[J.scoreLowerBound]), +fmt(r[J.scoreUpperBound])]);
                P.line(B, 'test-result', name.replace(/\s+/g, '_') + ' score=' + fmt(r[J.score]));
            }
        }
        P.json(B, 'tests', tests);
        const full = { bench: B, run: P.run, official, version: dashboard.version, options: dashboard.options,
            score, fps, results: dashboard.results, userAgent: navigator.userAgent };
        if (q.get('phx-post-samples') === '1')
            full.data = dashboard.data;
        P.done(B, { score: fmt(score), fps: fps, status: 'ok', official: official ? 1 : 0 }, full);
    };

    /* the stock start: once the frame-rate detection has enabled the start button */
    function startWhenReady() {
        const button = document.getElementById('start-button');
        if (!button || button.disabled) {
            setTimeout(startWhenReady, 500);
            return;
        }
        const params = benchmarkController.benchmarkDefaultParameters;
        P.line(B, 'framerate', 'detected=' + params['frame-rate'] + ' label=' +
            (document.getElementById('frame-rate-label') || {}).textContent);
        if (interval)
            params['test-interval'] = parseInt(interval, 10);
        if (frameRate)
            params['frame-rate'] = params['system-frame-rate'] = parseInt(frameRate, 10);
        P.line(B, 'start', 'interval_s=' + params['test-interval'] + ' fps=' + params['frame-rate'] + ' official=' +
            (official ? 1 : 0) + ' t=' + P.secs());
        benchmarkController.startBenchmark();
    }
    window.addEventListener('load', () => setTimeout(startWhenReady, 500));
    P.line(B, 'hooked', 'interval=' + (interval || 'default') + ' frame_rate=' + (frameRate || 'detect'));
})();
