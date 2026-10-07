# css3test: where the Pi's WPE 2.54 is behind the host's WebKit 26.6

css3test (LeaVerou/css3test `bc9731cf`, [BENCHMARKS.md](BENCHMARKS.md)) checks which CSS features
the engine *recognises* (`CSS.supports()`, inline-style assignment, CSSOM members), not how it
renders them. Build 47 on the Pi: **69 % (4181/6419)**. Playwright's WPE MiniBrowser (WebKit 26.6)
on the host: **71 % (4349/6419)**; Chromium 153: 72 % (4453/6419). The 168-check gap is explained
below to within 2 checks; three features that WebKit 2.54 implements but ships switched off are
now switched on by the launcher.

## How the attribution was made

- The Pi's final JSON (`artifacts/browser-bench/pi-results/20261007-102137-css3test-*-final.json`)
  holds per-spec percentages only. The hook now records per-feature results too (below), so
  future runs can be compared exactly.
- The host was re-run with that hook (`artifacts/browser-bench/host-20261007-css3test/`; same
  4349/6419). That gives the host's exact failures for all 1262 features.
- Model: the Pi = the host's per-feature results, changed only where the WebKit 2.54 source
  (`port-sources/webkit_wpe-2.54.0`) differs: `CSSProperties.json` settings flags,
  `UnifiedWebPreferences.yaml` defaults, the IDL files and the parsers. With the nine changes in
  the table, the model gives **every one of the 140 per-spec percentages** the Pi reported, and
  4183 checks against the Pi's 4181. The 2 unattributed checks are within one feature of some
  large specification and show up on the next run with the new hook.
- Upstream status: WebKit `main`'s `UnifiedWebPreferences.yaml`, fetched 2026-10-07.

css3test's headline percentage is the **mean over the 1262 features** (each feature scores the
fraction of its tests passed), not passed/total checks. Report both.

## The gap, feature by feature

Checks = host minus Pi (positive: the Pi is behind). Class: **(a)** WebKit version: 2.54 does not
have it, nothing to switch on. **(b)** runtime preference: implemented in 2.54, off by default,
the launcher can turn it on. **(c)** compile-time: a CMake option of our build.

| Spec | Feature(s) | Checks | Pi | Host 26.6 | Class | 2.54 → upstream `main` | Decision |
|---|---|---:|---|---|---|---|---|
| css-borders-4 | `corner-shape` + its 16 longhands/shorthands (`corner-top-left-shape` … `corner-inline-end-shape`) | 139 | 0 % | 100 % | (b) `CSSCornerShapeEnabled` | testable, off → **stable, on** | **enable** |
| css-values-5 | `inherit()` | 12 | 0 % | 100 % | (a) | absent (not one of the substitution functions in `CSSSubstitutionParser.cpp`: var, env, attr, random-item, if) → `CSSInheritFunctionEnabled` stable | upgrade only |
| css-fonts-4 / css-font-loading-3 | `@font-face` `ascent-override`, `descent-override`, `line-gap-override`; `FontFace.lineGapOverride` | 6 + 1 | 0 % | 100 % | (b) `CSSFontFaceMetricOverrideDescriptorsEnabled` | testable, off → stable, on | **not enabled**: 2.54 only parses and stores the descriptors (`CSSFontSelector`, `CSSFontFace`); nothing in `platform/graphics` applies them to font metrics. Turning it on would buy 7 checks and change no rendering |
| css-text-4 | `white-space` shorthand with `discard-before`/`-after`/`-inner` | 5 | 6/13 | 11/13 | (a) | the 2.54 shorthand's longhands are `white-space-collapse` + `text-wrap-mode` only; later WebKit adds `white-space-trim` | upgrade only |
| css-images-5 | `object-view-box` | 4 | 0 % | 100 % | (b) `CSSObjectViewBoxEnabled` | testable, off → **stable, on** | **enable** |
| css-values-5 | `ident()` | 4 | 2/6 | 6/6 | (b) `CSSIdentFunctionEnabled` | testable, off → **stable, on** | **enable** (the 2 that pass already contain `var()`, which any value may) |
| css-paint-api-1 | `paint()`, `CSS.paintWorklet`, `Worklet.addModule` | 3.6 | 0 % | 93 % | (b) `CSSPaintingAPIEnabled` | testable, on only with `ENABLE_EXPERIMENTAL_FEATURES` → **still testable** | **not enabled**: upstream does not ship it either; it runs page JavaScript in a paint worklet during painting. Not worth 3.6 checks |
| css-conditional-3 | `CSSMediaRule.matches`, `CSSSupportsRule.matches` | 2 | 2/3, 1/2 | 100 % | (a) | `// FIXME: Add support for matches` in both 2.54 IDLs | upgrade only |
| css-box-4 | `margin-trim` | **−10** | 14/14 | 4/14 | (a), reversed | 2.54 accepts every value; 26.6 accepts `none`, `block`, `block-start`, `block-end` only | nothing: an upgrade will *lose* these 10 checks; do not chase it as a regression |
| — | not attributed | 2 | | | | | the next run's `css3test-diff.py` names them |
| | **total** | **168** | | | | | |

**No (c) items.** Every CMake option we turn off (`ENABLE_VIDEO` in the default build,
`WEB_AUDIO`, `WEB_RTC`, `MEDIA_*`, `WEB_CRYPTO`, `WEBXR`, …) touches no css3test check that the
host passes and the Pi fails; the per-spec percentages match the model everywhere. The only
CMake knob that touches the gap is `ENABLE_EXPERIMENTAL_FEATURES` (it defaults the Paint API on),
and it also turns on encrypted media, WebDriver BiDi, web extensions and WebXR: not wanted.

## What changed

- **Ports** `webkit_wpe/files/launcher/wpe-browser.cpp` (branch `webkit-css-features`):
  `enableCSSFeatures()` calls `webkit_settings_set_feature_enabled()` for
  `CSSCornerShapeEnabled`, `CSSObjectViewBoxEnabled` and `CSSIdentFunctionEnabled`, found by
  identifier in `webkit_settings_get_all_features()`, and logs
  `WPEB ... features enabled=<ids> absent=<ids>`; an identifier a later WebKit drops is logged as
  absent instead of failing silently. `--stock-features` (or `WPE_BROWSER_STOCK_FEATURES=1`)
  keeps WebKit's defaults, for an A/B on the same binary when a page renders oddly.
- **Coordination** `tools/browser/bench/hooks/css3test.html`: the POSTed final JSON also holds
  `failures`, every feature not fully passed as
  `[spec, group, feature, percent, tests, [failed tests]]` (a values test passed in only some of its
  properties carries css3test's "Failed in: …" note; css3test classes a test that passed in at least
  11/12 of its properties as a pass). Console and title lines are unchanged. About 80 kB per run.
- **Coordination** `tools/browser/bench/css3test-diff.py A.json B.json`: every feature whose pass
  fraction differs, the check difference, and the tests that fail on one side only. Runs without
  `failures` fall back to the per-spec comparison.

### Risk

The three features are paint/parse features reached only by pages that use them; a page that does
not mention `corner-shape`, `object-view-box` or `ident()` runs exactly the code it ran before.
Upstream turned all three on by default after 2.54. What can go wrong is a page written for
Chrome (which ships `corner-shape`) now taking its `@supports (corner-shape: …)` branch
and hitting a 2.54-era rendering bug in `BorderShape`/`CornerShapeUtilities`; that is a visual
defect, and `--stock-features` reverts it without a rebuild.

## Expected result

Model with the three features on: **70 %** headline (feature mean 70.11 %, was 68.6 %), about
**4328/6419** checks (+147: 139 corner-shape, 4 object-view-box, 4 `ident()`). The model takes
the host's results for the three features; the 2.54 grammars accept every value css3test tries
(`<corner-shape-value>` = `round | scoop | bevel | notch | square | squircle |
superellipse(<number> | infinity | -infinity)`; `object-view-box: none | <basic-shape-rect>`;
`ident()` takes idents, strings and integers), so the one uncertain check is
`ident("cool-" sibling-index())`. The headline has 0.1 point of margin above 70 %. The host's 71 %
(70.70 %) stays ahead by `inherit()`, the `white-space` shorthand, `matches`, the metric override
descriptors and the Paint API, less `margin-trim`: 21 checks, all (a) or deliberately not enabled.

## For the coordinator

1. Build: the webkit_wpe port only. The change is in the launcher, no CMake option or WebKit
   patch moved, so `build-wpe.sh` reuses the configured tree: ninja recompiles `wpe-browser.cpp`
   and relinks `wpe-browser`. Check `strings wpe-browser | grep CSSCornerShapeEnabled`.
2. Stage the new hook: `tools/browser/bench/stage.sh` copies `hooks/*` to the NFS root's
   `browser-bench/phx/`; without it the Pi's JSON has no `failures`.
3. Run css3test once (bench.sh `compat` or `smoke`). The UART shows
   `features enabled=CSSCornerShapeEnabled,CSSObjectViewBoxEnabled,CSSIdentFunctionEnabled absent=-`.
4. `tools/browser/bench/css3test-diff.py <build-47 Pi JSON> <new Pi JSON>` (per spec: the
   build-47 JSON has no per-feature list): expect css-borders-4 0 → 44 %, css-images-5 0 → 100 %,
   css-values-5 28 → 30 %, and nothing else. Then diff the new Pi JSON against
   `artifacts/browser-bench/host-20261007-css3test/*webkit*` (per feature): it should list exactly
   the remaining rows of the table above, plus the 2 unattributed checks by name.
