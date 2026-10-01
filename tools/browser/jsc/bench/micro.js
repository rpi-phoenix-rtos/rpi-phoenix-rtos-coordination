// SPDX-License-Identifier: BSD-3-Clause
//
// "JetStream-lite" for browser milestone B3: small kernels shaped like what pages do (object
// churn, property access, strings, JSON, regexps, sorting, closures, Map/Set, typed arrays),
// plus the allocator and footprint numbers the go/no-go needs, in one jsc process:
//
//   jsc micro.js [-- <scale>=1]
//
// Prints one line per kernel ("<name> <ms> ms"), then
//   MICRO scale=<s> geomean-ms=<g> total-ms=<t>
//   MALLOC fastMalloc ns/pair=<x>    (the shell's mallocInALoop(): 5000 x 2 KiB fastMalloc+fastFree)
//   FOOTPRINT current=<bytes> peak=<bytes> gc-heap=<bytes>
// Each kernel prints a checksum; README.md records the host reference values, so a wrong result
// (an interpreter or libc bug on the target) shows up as a checksum mismatch, not as "fast".

const args = typeof arguments === "undefined" ? [] : arguments;
const scale = args[0] ? Number(args[0]) : 1;

const kernels = [
    ["object-churn", () => {
        let sum = 0;
        for (let i = 0; i < 200000 * scale; i++) {
            const o = { x: i, y: i * 2, z: [i, i + 1] };
            sum = (sum + o.x + o.y + o.z[1]) % 1000003;
        }
        return sum;
    }],
    ["property-access", () => {
        class P { constructor(a, b) { this.a = a; this.b = b; } get s() { return this.a + this.b; } }
        const ps = [];
        for (let i = 0; i < 1000; i++)
            ps.push(new P(i, -i));
        let acc = 0;
        for (let r = 0; r < 300 * scale; r++)
            for (const p of ps)
                acc += p.s;
        return acc;
    }],
    ["string-build", () => {
        let total = 0;
        for (let r = 0; r < 40 * scale; r++) {
            let s = "";
            for (let i = 0; i < 2000; i++)
                s += String.fromCharCode(97 + (i % 26)) + i;
            total += s.length;
        }
        return total % 1000003;
    }],
    ["json", () => {
        const obj = { list: [] };
        for (let i = 0; i < 2000; i++)
            obj.list.push({ id: i, name: "item" + i, tags: ["a", "b", "c"], v: i / 7 });
        let n = 0;
        for (let r = 0; r < 10 * scale; r++)
            n += JSON.parse(JSON.stringify(obj)).list.length;
        return n;
    }],
    ["regexp", () => {
        const text = ("The quick brown fox jumps over the lazy dog 2026-10-01 user@example.org ").repeat(200);
        let n = 0;
        for (let r = 0; r < 20 * scale; r++) {
            n += (text.match(/\b\w+@\w+\.\w+\b/g) || []).length;
            n += (text.match(/\d{4}-\d{2}-\d{2}/g) || []).length;
            n += text.replace(/o/g, "0").length % 7;
        }
        return n;
    }],
    ["array-sort", () => {
        let seed = 12345;
        const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff);
        let check = 0;
        for (let r = 0; r < 10 * scale; r++) {
            const a = [];
            for (let i = 0; i < 20000; i++)
                a.push(rnd());
            a.sort((x, y) => x - y);
            check = (check + a[100] + a[19000]) % 1000003;
        }
        return check;
    }],
    ["closures", () => {
        const make = k => x => x + k;
        let acc = 0;
        for (let i = 0; i < 300000 * scale; i++)
            acc = make(i & 7)(acc) % 1000003;
        return acc;
    }],
    ["map-set", () => {
        let n = 0;
        for (let r = 0; r < 10 * scale; r++) {
            const m = new Map(), s = new Set();
            for (let i = 0; i < 10000; i++) {
                m.set("k" + i, i);
                s.add(i * 3);
            }
            for (let i = 0; i < 10000; i++)
                if (s.has(i))
                    n += m.get("k" + i);
        }
        return n % 1000003;
    }],
    ["typed-array", () => {
        const f = new Float64Array(100000);
        let acc = 0;
        for (let r = 0; r < 20 * scale; r++) {
            for (let i = 0; i < f.length; i++)
                f[i] = i * 0.5 + r;
            for (let i = 0; i < f.length; i++)
                acc += f[i];
        }
        return acc % 1000003;
    }],
];

let logSum = 0, total = 0;
for (const [name, fn] of kernels) {
    fn(); // warm-up: keeps parsing and first-call costs out of the timed run
    const t = preciseTime();
    const value = fn();
    const ms = (preciseTime() - t) * 1000;
    print(name.padEnd(18) + ms.toFixed(1).padStart(9) + " ms   checksum " + value);
    logSum += Math.log(ms);
    total += ms;
}
print("MICRO scale=" + scale + " geomean-ms=" + Math.exp(logSum / kernels.length).toFixed(1) +
    " total-ms=" + total.toFixed(1));

// fastMalloc rate: mallocInALoop() does 5000 fastMalloc(2048) then 5000 fastFree.
mallocInALoop();
const loops = 200;
let t = preciseTime();
for (let i = 0; i < loops; i++)
    mallocInALoop();
const nsPerPair = (preciseTime() - t) * 1e9 / (loops * 5000);
print("MALLOC fastMalloc ns/pair=" + nsPerPair.toFixed(1));

gc();
const fp = MemoryFootprint();
print("FOOTPRINT current=" + fp.current + " peak=" + fp.peak + " gc-heap=" + gcHeapSize());
