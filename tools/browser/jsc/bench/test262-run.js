// SPDX-License-Identifier: BSD-3-Clause
//
// test262 subset runner for the `jsc` shell, all in one process (the Pi's psh has no loops):
//
//   jsc test262-run.js -- <bundle.json> [<failures-out.txt>]
//
// The bundle comes from test262-bundle.py. Every run evaluates harness + test in a FRESH realm
// (the shell's runString(), a new global object each time), then drains microtasks for async
// tests. Output: one "FAIL <path> [<mode>]: <reason>" line per failure on stdout (and in the
// failures file when given), then one summary line:
//
//   TEST262 rev=<sha> runs=<n> pass=<p> fail=<f> skipped-files=<s> seconds=<t>

const args = typeof arguments === "undefined" ? [] : arguments;
const bundlePath = args[0];
const failuresPath = args[1];
if (!bundlePath)
    throw new Error("usage: jsc test262-run.js -- <bundle.json> [<failures-out.txt>]");

const t0 = preciseTime();
const bundle = JSON.parse(readFile(bundlePath));
const harness = bundle.harness;
const sources = bundle.sources;

// Async tests report through $DONE (test262 INTERPRETING.md); doneprintHandle.js is replaced by
// this, which records the outcome on the realm's global object.
const asyncPrelude =
    "var __t262 = { done: false, error: undefined };\n" +
    "function $DONE(error) { __t262.done = true; __t262.error = error; }\n";

function errorName(e) {
    try {
        if (e && typeof e === "object" && e.constructor && e.constructor.name)
            return e.constructor.name;
    } catch (_) { }
    return typeof e;
}

function describe(e) {
    try {
        return errorName(e) + ": " + String(e && e.message !== undefined ? e.message : e);
    } catch (_) {
        return "<unprintable>";
    }
}

let pass = 0, fail = 0;
const failures = [];

for (const t of bundle.tests) {
    let src = t.m === "strict" ? '"use strict";\n' : "";
    if (t.a)
        src += asyncPrelude;
    for (const inc of t.i)
        src += harness[inc] + "\n";
    src += sources[t.p];

    let thrown = undefined, didThrow = false, realm;
    try {
        realm = runString(src);
        if (t.a)
            drainMicrotasks();
    } catch (e) {
        didThrow = true;
        thrown = e;
    }

    let reason = null;
    if (t.n) {
        if (!didThrow)
            reason = "expected " + t.n.type + " (" + t.n.phase + "), completed normally";
        else if (errorName(thrown) !== t.n.type)
            reason = "expected " + t.n.type + " (" + t.n.phase + "), got " + describe(thrown);
    } else if (didThrow) {
        reason = describe(thrown);
    } else if (t.a) {
        const r = realm && realm.__t262;
        if (!r || !r.done)
            reason = "async test did not call $DONE";
        else if (r.error !== undefined)
            reason = "async: " + describe(r.error);
    }

    if (reason === null)
        pass++;
    else {
        fail++;
        const line = "FAIL " + t.p + " [" + t.m + "]: " + reason;
        failures.push(line);
        print(line);
    }
}

const seconds = (preciseTime() - t0).toFixed(1);
const skippedFiles = Object.values(bundle.skipped).reduce((a, b) => a + b, 0);
const summary = "TEST262 rev=" + bundle.revision.slice(0, 12) + " runs=" + bundle.tests.length +
    " pass=" + pass + " fail=" + fail + " skipped-files=" + skippedFiles + " seconds=" + seconds;
if (failuresPath)
    writeFile(failuresPath, failures.join("\n") + "\n" + summary + "\n");
print(summary);
