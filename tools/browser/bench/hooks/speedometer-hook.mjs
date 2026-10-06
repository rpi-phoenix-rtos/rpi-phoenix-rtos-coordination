/*
 * speedometer-hook.mjs -- reports a Speedometer 3.1 run through bench-common.js (Phoenix-RTOS
 * browser benchmark suite). NOT part of Speedometer.
 *
 * Loaded by bench.html (a copy of Speedometer's index.html with this script and bench-common.js
 * added) as a module after resources/main.mjs, so it runs once main.mjs has created
 * globalThis.benchmarkClient and before DOMContentLoaded (when ?startAutomatically starts the
 * run). It wraps the client's callbacks; the benchmark itself is unchanged.
 *
 * Lines (see bench-common.js for the framing):
 *   env ...                        the page's capabilities
 *   start iterations=N suites=S tests=T official=0|1 params=...
 *   suite iter=I <suite> t=<s>     each suite as it starts
 *   step iter=I <suite>/<step> t=<s>          each test step as it starts: the hang/crash
 *                                  attribution (bench.sh's HUNG line names the last one)
 *   step-done iter=I <suite>/<step> wall_ms=  the step's wall time, harness included
 *   iteration I total_ms= geomean_ms= score= t=
 *   timing iter=I <suite>/<step> sync_ms= async_ms=   every step's measured times, per
 *                                  iteration (console only: the per-subtest profile)
 * ?phx-quiet=1 drops the step lines (a scoring run with the least reporting during measurement).
 *   result score= delta= geomean_ms=
 *   suite-result <suite> mean_ms= delta_ms=     every suite's total
 *   json metrics k/n ...           {name: [mean, delta, [values]]} for every metric
 *   error during=<suite/test> <message>
 *   BENCH-DONE speedometer score=<x> delta=<d> status=ok|error iterations=N official=0|1
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
const B = 'speedometer';
const P = window.PhxBench;
const client = globalThis.benchmarkClient;
const q = P.params;

P.hookErrors(B);
P.env(B);

/* the official configuration: 10 iterations, every suite, the rAF measurement, no shuffling */
const official = (!q.has('iterationCount') || q.get('iterationCount') === '10') && !q.has('suite') && !q.has('suites') &&
    !q.has('tags') && !q.has('measurementMethod') && !q.has('shuffleSeed') && !q.has('warmupBeforeSync') && !q.has('waitBeforeSync');
const quiet = q.get('phx-quiet') === '1';
let suite = null;
let lastTest = 'none';
let stepT0 = 0;
let iteration = 0;

const orig = {};
for (const name of ['willStartFirstIteration', 'willRunTest', 'didRunTest', 'didRunSuites', 'didFinishLastIteration', 'handleError'])
    orig[name] = client[name].bind(client);

client.willStartFirstIteration = (iterations) => {
    const keep = [...q.keys()].filter((k) => k !== 'run').map((k) => k + (q.get(k) ? '=' + q.get(k) : '')).join('&');
    P.line(B, 'start', 'iterations=' + iterations + ' official=' + (official ? 1 : 0) + ' params=' + (keep || 'none'));
    return orig.willStartFirstIteration(iterations);
};

client.willRunTest = (s, test) => {
    lastTest = s.name + '/' + test.name;
    if (s.name !== suite) {
        suite = s.name;
        P.line(B, 'suite', 'iter=' + (iteration + 1) + ' ' + s.name + ' t=' + P.secs());
    }
    if (!quiet)
        P.line(B, 'step', 'iter=' + (iteration + 1) + ' ' + lastTest + ' t=' + P.secs());
    stepT0 = performance.now();
    return orig.willRunTest(s, test);
};

client.didRunTest = (s, test) => {
    if (!quiet)
        P.line(B, 'step-done', 'iter=' + (iteration + 1) + ' ' + s.name + '/' + test.name + ' wall_ms=' +
            (performance.now() - stepT0).toFixed(1), { title: false });
    return orig.didRunTest(s, test);
};

client.didRunSuites = (measured) => {
    iteration++;
    suite = null;
    const f = (x) => (typeof x === 'number' ? x.toFixed(2) : String(x));
    P.line(B, 'iteration', iteration + ' total_ms=' + f(measured.total) + ' geomean_ms=' + f(measured.geomean) +
        ' score=' + f(measured.score) + ' t=' + P.secs());
    for (const [suiteName, suiteValues] of Object.entries(measured.tests || {})) {
        for (const [stepName, step] of Object.entries(suiteValues.tests || {})) {
            const t = step.tests || {};
            P.line(B, 'timing', 'iter=' + iteration + ' ' + suiteName + '/' + stepName + ' sync_ms=' + f(t.Sync) +
                ' async_ms=' + f(t.Async), { title: false });
        }
    }
    return orig.didRunSuites(measured);
};

function plainMetrics(metrics) {
    const out = {};
    for (const m of Object.values(metrics || {}))
        out[m.name] = [+m.mean.toFixed(3), +m.delta.toFixed(3), m.values.map((v) => +v.toFixed(2))];
    return out;
}

client.didFinishLastIteration = async (metrics) => {
    const r = orig.didFinishLastIteration(metrics);
    const score = metrics && metrics.Score;
    const geomean = metrics && metrics.Geomean;
    const s = score ? score.mean.toFixed(3) : 'NaN';
    const d = score ? score.delta.toFixed(3) : 'NaN';
    P.line(B, 'result', 'score=' + s + ' delta=' + d + ' geomean_ms=' + (geomean ? geomean.mean.toFixed(2) : 'NaN') +
        ' iterations=' + iteration + ' official=' + (official ? 1 : 0));
    for (const m of Object.values(metrics || {})) {
        if (m.name.includes('/') || m.name.startsWith('Iteration-') || m.name === 'Score' || m.name === 'Geomean')
            continue;
        P.line(B, 'suite-result', m.name + ' mean_ms=' + m.mean.toFixed(2) + ' delta_ms=' + m.delta.toFixed(2));
    }
    const plain = plainMetrics(metrics);
    P.json(B, 'metrics', plain);
    await P.done(B, { score: s, delta: d, status: 'ok', iterations: iteration, official: official ? 1 : 0 },
        { bench: B, run: P.run, official, params: Object.fromEntries(q), userAgent: navigator.userAgent, metrics: plain });
    return r;
};

client.handleError = async (error) => {
    const r = orig.handleError(error);
    const msg = error && error.stack ? String(error.stack).split('\n').slice(0, 4).join(' | ') : String(error);
    P.line(B, 'error', 'during=' + lastTest + ' ' + msg);
    await P.done(B, { score: 'NaN', status: 'error', failed_at: lastTest, iterations: iteration, official: official ? 1 : 0 },
        { bench: B, run: P.run, error: msg, failed_at: lastTest, completed_iterations: iteration });
    return r;
};

P.line(B, 'hooked', 'startAutomatically=' + (q.has('startAutomatically') ? 1 : 0));
