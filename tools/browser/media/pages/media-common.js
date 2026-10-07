/*
 * media-common.js -- the reporting and <video> instrumentation shared by the streaming-video test
 * pages (b8-hls.html, b8-hlsjs.html, b8-mse.html, b8-hls-memory.html; docs/browser/MSE-DESIGN.md §9.3).
 *
 * Every event becomes ONE line "<TAG> t=<ms since load> r=<run> <event> key=value ..." on three
 * channels, because none is reliable alone on the Pi (the same reasoning as the bench's
 * bench-common.js):
 *   1. console.log -- wpe-browser writes console messages to stdout in window mode;
 *   2. document.title -- the launcher logs every title change ("WPEB t=<ms> title <title>");
 *      queued with a short gap so none is coalesced;
 *   3. POST /phx-log to serve-media.py, which prints "MEDIA-PAGE <client> <line>" in the host log
 *      (skipped for file:// pages and with ?beacon=0).
 * The last line is "<TAG>-DONE result=<ok|error|stopped|timeout> ..." (pw-run.mjs --done waits
 * for it on the host). The run id comes from ?run= (default "manual").
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
(function () {
    'use strict';
    const params = new URLSearchParams(location.search);
    const run = (params.get('run') || 'manual').replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 48);
    const t0 = performance.now();
    const beacon = params.get('beacon') !== '0' && location.protocol.startsWith('http');
    const titles = [];
    let titleTimer = null;
    let pending = [];
    let flushTimer = null;
    let tag = 'MEDIA';
    let finished = false;
    const stateEl = () => document.getElementById('state');

    function ms() { return Math.round(performance.now() - t0); }

    function pumpTitle() {
        if (!titles.length) { titleTimer = null; return; }
        document.title = titles.shift();
        titleTimer = setTimeout(pumpTitle, 20);
    }

    function flush() {
        flushTimer = null;
        if (!pending.length) return;
        const body = pending.join('\n');
        pending = [];
        try {
            fetch('/phx-log', { method: 'POST', body, headers: { 'Content-Type': 'text/plain' }, keepalive: true })
                .catch(() => {});
        } catch (e) { /* no fetch: the console and the title still carry it */ }
    }

    /* one event line on every channel */
    function report(event, fields) {
        let s = tag + ' t=' + ms() + ' r=' + run + ' ' + event;
        if (fields)
            for (const [k, v] of Object.entries(fields))
                s += ' ' + k + '=' + String(v).replace(/\s+/g, '_');
        s = s.slice(0, 700);
        console.log(s);
        const el = stateEl();
        if (el) el.textContent = s;
        titles.push(s);
        if (!titleTimer) titleTimer = setTimeout(pumpTitle, 0);
        if (beacon) {
            pending.push(s);
            if (!flushTimer) flushTimer = setTimeout(flush, 200);
        }
        return s;
    }

    function done(result, fields) {
        if (finished) return;
        finished = true;
        report('summary', fields);
        const line = tag + '-DONE result=' + result + ' r=' + run + ' t=' + ms();
        console.log(line);
        titles.push(line);
        if (!titleTimer) titleTimer = setTimeout(pumpTitle, 0);
        if (beacon) { pending.push(line); flush(); }
    }

    function ranges(tr) {
        if (!tr) return '-';
        const out = [];
        for (let i = 0; i < tr.length; i++) out.push(tr.start(i).toFixed(2) + '-' + tr.end(i).toFixed(2));
        return out.join(',') || 'none';
    }

    /* the answers that steer players: canPlayType, MediaSource.isTypeSupported, decodingInfo */
    const HLS_TYPES = ['application/vnd.apple.mpegurl', 'application/x-mpegurl', 'audio/mpegurl', 'audio/x-mpegurl'];
    const CODECS = [
        ['hevc8', 'hvc1.1.6.L120.90'], ['hev1', 'hev1.1.6.L120.90'], ['main10', 'hvc1.2.4.L120.90'],
        ['h264-31', 'avc1.64001f'], ['h264-40', 'avc1.640028'], ['h264-base', 'avc1.42E01E'],
        ['aac', 'mp4a.40.2'], ['opus', 'opus'], ['ec3', 'ec-3'],
        ['av1', 'av01.0.05M.08'], ['vp9', 'vp09.00.10.08'], ['dv', 'dvh1.05.06'],
    ];

    function canPlayMatrix(video) {
        for (const t of HLS_TYPES)
            report('canplaytype', { type: t, answer: video.canPlayType(t) || 'no' });
        for (const [name, c] of CODECS) {
            report('canplaytype', { type: 'hls+' + name, codecs: c,
                answer: video.canPlayType('application/vnd.apple.mpegurl; codecs="' + c + '"') || 'no' });
            const mp4 = (c.startsWith('mp4a') || c === 'opus' || c === 'ec-3' ? 'audio' : 'video') + '/mp4; codecs="' + c + '"';
            report('canplaytype', { type: 'mp4+' + name, codecs: c, answer: video.canPlayType(mp4) || 'no' });
        }
    }

    function mseInfo() {
        const MS = window.MediaSource, MMS = window.ManagedMediaSource;
        report('mse', { MediaSource: !!MS, ManagedMediaSource: !!MMS,
            canConstructInWorker: MS && 'canConstructInDedicatedWorker' in MS ? MS.canConstructInDedicatedWorker : '-' });
        return MS || MMS || null;
    }

    function isTypeSupportedMatrix(MS) {
        if (!MS) { report('istypesupported', { available: 0 }); return; }
        const types = CODECS.map(([name, c]) => [name,
            (c.startsWith('mp4a') || c === 'opus' || c === 'ec-3' ? 'audio' : 'video') + '/mp4; codecs="' + c + '"']);
        types.push(['webm-vp9', 'video/webm; codecs="vp9"'], ['webm-opus', 'audio/webm; codecs="opus"'],
            ['ts', 'video/mp2t; codecs="avc1.64001f,mp4a.40.2"'], ['hlsjs-probe', 'video/mp4; codecs="avc1.42E01E,mp4a.40.2"']);
        for (const [name, t] of types)
            report('istypesupported', { name, type: t, answer: MS.isTypeSupported(t) ? 'yes' : 'no' });
    }

    /* MediaCapabilities.decodingInfo: the §6.7 factory's answers ('file' and 'media-source') */
    async function decodingInfoMatrix(kinds) {
        if (!navigator.mediaCapabilities) { report('capabilities', { available: 0 }); return; }
        const probes = [
            ['hvc1.1.6.L120.90', 1920, 1080, 30], ['hvc1.1.6.L120.90', 1920, 1080, 60], ['hvc1.2.4.L120.90', 1920, 1080, 30],
            ['avc1.64001f', 1280, 720, 30], ['avc1.640028', 1920, 1080, 30], ['vp09.00.10.08', 640, 360, 30],
            ['av01.0.05M.08', 1920, 1080, 30],
        ];
        for (const kind of kinds) {
            for (const [codec, w, h, fps] of probes) {
                try {
                    const r = await navigator.mediaCapabilities.decodingInfo({ type: kind,
                        video: { contentType: 'video/mp4; codecs="' + codec + '"', width: w, height: h,
                            bitrate: w * h * 2, framerate: fps } });
                    report('capabilities', { type: kind, codec, size: w + 'x' + h + '@' + fps,
                        supported: r.supported ? 1 : 0, smooth: r.smooth ? 1 : 0, efficient: r.powerEfficient ? 1 : 0 });
                } catch (e) {
                    report('capabilities', { type: kind, codec, size: w + 'x' + h + '@' + fps, error: e.name });
                }
            }
            for (const codec of ['mp4a.40.2', 'opus']) {
                try {
                    const r = await navigator.mediaCapabilities.decodingInfo({ type: kind,
                        audio: { contentType: 'audio/mp4; codecs="' + codec + '"', channels: '2', bitrate: 128000,
                            samplerate: 48000 } });
                    report('capabilities', { type: kind, codec, supported: r.supported ? 1 : 0, smooth: r.smooth ? 1 : 0,
                        efficient: r.powerEfficient ? 1 : 0 });
                } catch (e) {
                    report('capabilities', { type: kind, codec, error: e.name });
                }
            }
        }
    }

    /*
     * The <video> element's story: metadata, size changes (the variant a native player chose shows
     * as videoWidth x videoHeight), playing/waiting with stall time, seeks, errors, and every
     * `every` seconds: currentTime, buffered, seekable (live), getVideoPlaybackQuality and the
     * frame rate measured with requestVideoFrameCallback when the engine has it.
     */
    function watchVideo(video, opts) {
        opts = opts || {};
        const every = (opts.every || 5) * 1000;
        const st = { created: performance.now(), startupMs: -1, playingAt: -1, waitingAt: -1, stalls: 0, stallMs: 0, maxStallMs: 0, ended: 0, errors: 0,
            sizes: [], frames: 0, lastMediaTime: -1, lastFrames: 0, lastTick: performance.now() };
        const q = () => (video.getVideoPlaybackQuality ? video.getVideoPlaybackQuality() : null);
        const name = opts.name ? { v: opts.name } : {};
        const f = (o) => Object.assign({}, name, o);
        video.addEventListener('loadstart', () => report('loadstart', f({ src: (video.currentSrc || video.src || '-').slice(0, 160) })));
        video.addEventListener('loadedmetadata', () => {
            const tracks = {};
            if (video.videoTracks) tracks.videotracks = video.videoTracks.length;
            if (video.audioTracks) tracks.audiotracks = video.audioTracks.length;
            report('loadedmetadata', f(Object.assign({ size: video.videoWidth + 'x' + video.videoHeight,
                duration: video.duration === Infinity ? 'Infinity' : (isNaN(video.duration) ? 'NaN' : video.duration.toFixed(3)),
                seekable: ranges(video.seekable) }, tracks)));
        });
        video.addEventListener('resize', () => {
            const s = video.videoWidth + 'x' + video.videoHeight;
            if (st.sizes[st.sizes.length - 1] !== s) st.sizes.push(s);
            report('resize', f({ size: s, current: video.currentTime.toFixed(2) }));
        });
        video.addEventListener('loadeddata', () => report('loadeddata', f({ ready: video.readyState })));
        video.addEventListener('canplay', () => report('canplay', f({ ready: video.readyState })));
        video.addEventListener('playing', () => {
            if (st.playingAt < 0) {
                st.playingAt = performance.now();
                st.startupMs = st.playingAt - st.created;
                st.waitingAt = -1;  /* waiting before the first frame is start-up, not a stall */
                report('playing', f({ current: video.currentTime.toFixed(2), startup_ms: Math.round(st.startupMs) }));
            } else if (st.waitingAt >= 0) {
                const gap = performance.now() - st.waitingAt;
                st.stallMs += gap;
                st.maxStallMs = Math.max(st.maxStallMs, gap);
                st.waitingAt = -1;
                report('playing', f({ current: video.currentTime.toFixed(2), after_stall_ms: Math.round(gap) }));
            } else {
                report('playing', f({ current: video.currentTime.toFixed(2) }));
            }
        });
        video.addEventListener('waiting', () => {
            if (st.waitingAt < 0 && st.playingAt >= 0 && !video.ended) { st.waitingAt = performance.now(); st.stalls++; }
            report('waiting', f({ current: video.currentTime.toFixed(2), ready: video.readyState, buffered: ranges(video.buffered) }));
        });
        video.addEventListener('stalled', () => report('stalled', f({ current: video.currentTime.toFixed(2) })));
        video.addEventListener('pause', () => report('pause', f({ current: video.currentTime.toFixed(2) })));
        video.addEventListener('seeking', () => report('seeking', f({ current: video.currentTime.toFixed(2) })));
        video.addEventListener('seeked', () => report('seeked', f({ current: video.currentTime.toFixed(2), buffered: ranges(video.buffered) })));
        video.addEventListener('ended', () => {
            st.ended++;
            report('ended', f({ current: video.currentTime.toFixed(2), n: st.ended }));
        });
        video.addEventListener('error', () => {
            st.errors++;
            const e = video.error;
            report('error', f({ code: e ? e.code : '?', message: e && e.message ? e.message : '-' }));
        });
        if (video.requestVideoFrameCallback) {
            const onFrame = (now, meta) => {
                if (meta.mediaTime !== st.lastMediaTime) { st.frames++; st.lastMediaTime = meta.mediaTime; }
                video.requestVideoFrameCallback(onFrame);
            };
            video.requestVideoFrameCallback(onFrame);
        }
        st.timer = setInterval(() => {
            const now = performance.now();
            const fields = { current: video.currentTime.toFixed(2), paused: video.paused, ready: video.readyState,
                buffered: ranges(video.buffered) };
            if (video.duration === Infinity) fields.seekable = ranges(video.seekable);
            const quality = q();
            if (quality) {
                fields.total = quality.totalVideoFrames;
                fields.dropped = quality.droppedVideoFrames;
            }
            if (video.webkitDecodedFrameCount !== undefined) {
                fields.decoded = video.webkitDecodedFrameCount;
                fields.wk_dropped = video.webkitDroppedFrameCount;
            }
            if (video.requestVideoFrameCallback) {
                fields.rvfc_fps = ((st.frames - st.lastFrames) * 1000 / (now - st.lastTick)).toFixed(1);
                st.lastFrames = st.frames;
            }
            st.lastTick = now;
            report('tick', f(fields));
        }, every);
        st.summary = () => {
            const quality = q();
            if (st.waitingAt >= 0) {
                const gap = performance.now() - st.waitingAt;
                st.stallMs += gap;
                st.maxStallMs = Math.max(st.maxStallMs, gap);
                st.waitingAt = -1;
            }
            return f({ played_s: st.playingAt < 0 ? '-' : ((performance.now() - st.playingAt) / 1000).toFixed(1),
                current: video.currentTime.toFixed(2), startup_ms: st.startupMs < 0 ? '-' : Math.round(st.startupMs), stalls: st.stalls, stall_ms: Math.round(st.stallMs),
                max_stall_ms: Math.round(st.maxStallMs), ended: st.ended, errors: st.errors,
                sizes: st.sizes.join('>') || (video.videoWidth + 'x' + video.videoHeight),
                total: quality ? quality.totalVideoFrames : '-', dropped: quality ? quality.droppedVideoFrames : '-',
                rvfc_frames: video.requestVideoFrameCallback ? st.frames : '-' });
        };
        st.stop = () => clearInterval(st.timer);
        return st;
    }

    /* actions from the query: seek=<to>[@<at>], pause=<a>-<b>, stop=<s of wall time after playing> */
    function actions(video, st, finish) {
        const seek = params.get('seek');
        if (seek) {
            const [to, at] = seek.split('@').map(Number);
            const when = isNaN(at) ? 5 : at;
            let doneSeek = false;
            video.addEventListener('timeupdate', () => {
                if (!doneSeek && video.currentTime >= when) {
                    doneSeek = true;
                    report('seek-request', { from: video.currentTime.toFixed(2), to });
                    video.currentTime = to;
                }
            });
        }
        const pause = params.get('pause');
        if (pause) {
            const [a, b] = pause.split('-').map(Number);
            let paused = false;
            video.addEventListener('timeupdate', () => {
                if (!paused && video.currentTime >= a) {
                    paused = true;
                    video.pause();
                    report('pause-request', { at: video.currentTime.toFixed(2), resume_after_s: (b - a) });
                    setTimeout(() => { report('resume-request', {}); video.play().catch((e) => report('play-rejected', { name: e.name })); },
                        (b - a) * 1000);
                }
            });
        }
        const stop = Number(params.get('stop') || 0);
        if (stop > 0) {
            const arm = () => setTimeout(() => finish('stopped'), stop * 1000);
            if (st.playingAt >= 0) arm(); else video.addEventListener('playing', arm, { once: true });
        }
        const limit = Number(params.get('limit') || 300);
        setTimeout(() => finish('timeout'), limit * 1000);
    }

    window.PhxMedia = { params, run, report, done, ranges, canPlayMatrix, mseInfo, isTypeSupportedMatrix,
        decodingInfoMatrix, watchVideo, actions, setTag: (t) => { tag = t; }, ms };
})();
