// SPDX-License-Identifier: BSD-3-Clause
//
// SunSpider 1.0.2 driver for the `jsc` shell, one process (the Pi's psh has no loops):
//
//   jsc sunspider-run.js -- <dir-with-LIST-and-tests> [<runs>=3]
//
// Like SunSpider's own harness, every test runs in a fresh global object (the shell's
// runString()), timed with preciseTime(). One warm-up pass (not counted, primes the file cache
// and the allocator), then <runs> timed passes. Prints per-test mean ms and a summary line:
//
//   SUNSPIDER runs=<n> total-ms=<mean of pass totals> min-ms=<best pass> per-pass=[...]

const args = typeof arguments === "undefined" ? [] : arguments;
const dir = args[0];
const runs = args[1] ? parseInt(args[1]) : 3;
if (!dir)
    throw new Error("usage: jsc sunspider-run.js -- <dir> [runs]");

const names = readFile(dir + "/LIST").split("\n").map(s => s.trim()).filter(s => s.length);
const sources = names.map(n => readFile(dir + "/" + n + ".js"));

function pass() {
    const times = [];
    for (const src of sources) {
        const t = preciseTime();
        runString(src);
        times.push((preciseTime() - t) * 1000);
    }
    return times;
}

pass(); // warm-up

const perTest = names.map(() => 0);
const totals = [];
for (let r = 0; r < runs; r++) {
    const times = pass();
    let total = 0;
    times.forEach((ms, i) => { perTest[i] += ms; total += ms; });
    totals.push(total);
}

names.forEach((n, i) => print(n.padEnd(26) + (perTest[i] / runs).toFixed(1).padStart(9) + " ms"));
const mean = totals.reduce((a, b) => a + b, 0) / runs;
print("SUNSPIDER runs=" + runs + " total-ms=" + mean.toFixed(1) + " min-ms=" + Math.min(...totals).toFixed(1) +
    " per-pass=[" + totals.map(x => x.toFixed(0)).join(",") + "]");
