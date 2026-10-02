// SPDX-License-Identifier: BSD-3-Clause
//
// Browser milestone B9: is the JIT really on in this `jsc`, and does code that tiers up and
// then deoptimizes still compute the right thing? One process, no arguments:
//
//   jsc [--useJIT=false | --forceEagerCompilation=true | ...] jit-check.js
//
// Prints
//   JITCHECK useJIT=<b> baseline=<b> dfg=<b> ftl=<b> regexp=<b> concurrentJIT=<b>
//            concurrentGC=<b> pollingTraps=<b> pool=<bytes>
//   JITTIER loop=<checksum> dfg-compiles=<n> poly=<checksum> poly-dfg-compiles=<n> regexp=<checksum>
//   JITCHECK result=<PASS|FAIL> ...
// The checksums are the same with and without the JIT (host reference in README.md). With the
// JIT on, dfg-compiles must be > 0: the hot loop reached an optimizing tier (with the DFG off
// the shell reports 1000000, "pretend compiled"). `poly` is first
// optimized for int32 arguments, then fed doubles and strings, which forces OSR exits and
// recompilation: a JIT that mishandles the exit returns a wrong checksum.

const o = jscOptions();
print("JITCHECK useJIT=" + o.useJIT + " baseline=" + o.useBaselineJIT + " dfg=" + o.useDFGJIT +
    " ftl=" + o.useFTLJIT + " regexp=" + o.useRegExpJIT + " concurrentJIT=" + o.useConcurrentJIT +
    " concurrentGC=" + o.useConcurrentGC + " pollingTraps=" + o.usePollingTraps +
    " pool=" + o.jitMemoryReservationSize);

function hot(n) {
    let s = 0;
    for (let i = 0; i < n; i++)
        s = (s + i * i) % 1000003;
    return s;
}
noInline(hot);

let loop = 0;
for (let k = 0; k < 3000; k++)
    loop = (loop + hot(1000 + (k & 7))) % 1000003;

function poly(a, b) {
    return a + b;
}
noInline(poly);

let acc = 0;
for (let i = 0; i < 200000; i++)
    acc = poly(acc, i) % 1000003;          // int32: optimized for small integers
for (let i = 0; i < 20000; i++)
    acc = Math.floor(poly(acc, 0.5) * 3) % 1000003; // doubles: OSR exit, recompile
let str = "";
for (let i = 0; i < 2000; i++)
    str = poly(str, i % 10).slice(-64);    // strings: exit again
for (let i = 0; i < 200000; i++)
    acc = poly(acc, i & 255) % 1000003;    // and back to integers
const polySum = (acc + str.length * 7 + str.charCodeAt(5)) % 1000003;

let re = 0;
const rx = /([a-f]+)(\d+)/g;
for (let i = 0; i < 20000; i++) {
    const m = ("xx" + (i % 7 ? "cafe" : "beef") + i + "yy").match(rx);
    re = (re + m[0].length * 31 + i) % 1000003;
}

const dfg = numberOfDFGCompiles(hot);
const polyDfg = numberOfDFGCompiles(poly);
print("JITTIER loop=" + loop + " dfg-compiles=" + dfg + " poly=" + polySum + " poly-dfg-compiles=" + polyDfg +
    " regexp=" + re);

const jitOn = o.useJIT && o.useBaselineJIT && o.useDFGJIT;
const tiered = !jitOn || (dfg > 0 && dfg < 1000000);
print("JITCHECK result=" + (tiered ? "PASS" : "FAIL") + (tiered ? "" : " (the JIT is on but the hot loop never reached the DFG)"));
