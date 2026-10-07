// fmp4-harness: WebCore's FFmpegFMP4Parser (patch webkit-mse/0032) on the host.
//
//   fmp4-harness dump [--raw] FILE...        every file one append; prints the tracks and every
//                                            sample as FFmpeg's ffprobe prints packets
//                                            (pts,dts,duration,size,flags,MD5) for check-fmp4.py
//   fmp4-harness splits [--sparse] FILE...   every split point of the concatenation fed as two
//                                            appends; each run must give exactly the samples of
//                                            the one-append run
//   fmp4-harness random N SEED FILE...       N runs of random append sizes (1 byte .. 64 KiB)
//   fmp4-harness reset FILE...               a reset() in every box of the second media segment,
//                                            then the rest from the next segment on: the samples
//                                            of every complete segment, none of the reset one
//   fmp4-harness switch A_INIT A_SEG... -- B_INIT B_SEG...
//                                            a second initialization segment on the same parser
//   fmp4-harness mutate N SEED FILE...       N runs with random bytes changed (no result check:
//                                            the sanitizers are the check)
//
// Copyright 2026 Phoenix Systems
// SPDX-License-Identifier: BSD-3-Clause

#include "FFmpegFMP4Parser.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <openssl/evp.h>
#include <random>
#include <string>
#include <vector>

using WebCore::FFmpegFMP4Parser;

namespace {

using Bytes = std::vector<uint8_t>;

Bytes readFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fprintf(stderr, "fmp4-harness: cannot read %s\n", path);
        exit(2);
    }
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

std::string md5(const uint8_t* data, size_t size)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    EVP_Digest(data, size, digest, &length, EVP_md5(), nullptr);
    std::string text;
    char hex[3];
    for (unsigned i = 0; i < length; ++i) {
        snprintf(hex, sizeof(hex), "%02x", digest[i]);
        text += hex;
    }
    return text;
}

uint64_t fnv1a(const uint8_t* data, size_t size)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ data[i]) * 0x100000001b3ull;
    return hash;
}

struct Record {
    uint32_t track;
    int64_t dts, pts;
    uint32_t duration, size;
    bool sync;
    uint64_t hash;
    bool operator==(const Record&) const = default;
};

// Collects what the parser reports; `withMD5`: also the ffprobe-style lines
class Collector final : public FFmpegFMP4Parser::Client {
public:
    explicit Collector(bool withMD5 = false)
        : m_withMD5(withMD5)
    {
    }
    void didParseInitSegment(const FFmpegFMP4Parser::InitSegment& init) final
    {
        inits++;
        generation = init.generation;
        if (!m_withMD5)
            return;
        for (auto& track : init.tracks) {
            lines.push_back("TRACK id=" + std::to_string(track.id) + " kind=" + (track.kind == FFmpegFMP4Parser::TrackKind::Video ? "video" : "audio")
                + " entry=" + track.sampleEntry + " codec=" + track.codec + " timescale=" + std::to_string(track.timescale)
                + " size=" + std::to_string(track.width) + "x" + std::to_string(track.height) + " rate=" + std::to_string(track.sampleRate)
                + " channels=" + std::to_string(track.channels) + " edit=" + std::to_string(track.editOffset)
                + (track.editListIgnored ? " edit-ignored" : "") + " extradata=" + std::to_string(track.extradata.size()) + ":"
                + (track.extradata.empty() ? std::string("-") : md5(track.extradata.data(), track.extradata.size())));
        }
        lines.push_back("INIT generation=" + std::to_string(init.generation) + " duration=" + std::to_string(init.duration) + "/"
            + std::to_string(init.movieTimescale));
    }
    void didParseSample(const FFmpegFMP4Parser::Track& track, const FFmpegFMP4Parser::Sample& sample, const uint8_t* data) final
    {
        records.push_back({ sample.trackID, sample.decodeTime, sample.presentationTime, sample.duration, sample.size, sample.isSync, fnv1a(data, sample.size) });
        if (m_withMD5) {
            char line[256];
            snprintf(line, sizeof(line), "SAMPLE %s,%" PRId64 ",%" PRId64 ",%u,%u,%s,%s", track.kind == FFmpegFMP4Parser::TrackKind::Video ? "video" : "audio",
                sample.presentationTime, sample.decodeTime, sample.duration, sample.size, sample.isSync ? "K" : "_", md5(data, sample.size).c_str());
            lines.push_back(line);
        }
    }

    std::vector<Record> records;
    std::vector<std::string> lines;
    unsigned inits { 0 };
    unsigned generation { 0 };

private:
    bool m_withMD5;
};

Bytes concatenate(const std::vector<Bytes>& files)
{
    Bytes all;
    for (auto& file : files)
        all.insert(all.end(), file.begin(), file.end());
    return all;
}

bool feed(FFmpegFMP4Parser& parser, const Bytes& data, size_t from, size_t to, Collector& collector)
{
    if (!parser.append(data.data() + from, to - from, collector)) {
        fprintf(stderr, "fmp4-harness: parse error at [%zu, %zu): %s\n", from, to, parser.error().c_str());
        return false;
    }
    return true;
}

// The reference: the whole stream in one append
Collector reference(const Bytes& all)
{
    FFmpegFMP4Parser parser;
    Collector collector;
    if (!feed(parser, all, 0, all.size(), collector) || parser.pendingBytes()) {
        fprintf(stderr, "fmp4-harness: the one-append run failed (pending %zu)\n", parser.pendingBytes());
        exit(1);
    }
    return collector;
}

struct BoxSpan {
    uint64_t start, payload, end;
    bool isMdat;
};

std::vector<BoxSpan> topLevelBoxes(const Bytes& all)
{
    std::vector<BoxSpan> boxes;
    uint64_t position = 0;
    while (position + 8 <= all.size()) {
        const uint8_t* p = all.data() + position;
        uint64_t size = (uint64_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
        uint64_t header = 8;
        if (size == 1) {
            size = 0;
            for (int i = 8; i < 16; ++i)
                size = size << 8 | p[i];
            header = 16;
        }
        if (size < header || position + size > all.size())
            break;
        boxes.push_back({ position, position + header, position + size, !memcmp(p + 4, "mdat", 4) });
        position += size;
    }
    return boxes;
}

int dump(bool raw, const std::vector<Bytes>& files)
{
    FFmpegFMP4Parser parser;
    parser.setApplyEditLists(!raw);
    Collector collector(true);
    for (auto& file : files) {
        if (!feed(parser, file, 0, file.size(), collector))
            return 1;
    }
    for (auto& line : collector.lines)
        puts(line.c_str());
    printf("END samples=%zu pending=%zu\n", collector.records.size(), parser.pendingBytes());
    return 0;
}

// Every split point (sparse: in an mdat payload only its first and last 256 bytes and every 4099th
// byte, since the parser looks at nothing in a payload but where it ends)
int splits(bool sparse, const std::vector<Bytes>& files)
{
    Bytes all = concatenate(files);
    auto expected = reference(all).records;
    auto boxes = topLevelBoxes(all);
    size_t points = 0, failures = 0;
    for (size_t split = 1; split < all.size(); ++split) {
        if (sparse) {
            bool inside = false;
            for (auto& box : boxes) {
                if (box.isMdat && split > box.payload + 256 && split + 256 < box.end && (split - box.payload) % 4099) {
                    inside = true;
                    break;
                }
            }
            if (inside)
                continue;
        }
        FFmpegFMP4Parser parser;
        Collector collector;
        bool ok = feed(parser, all, 0, split, collector) && feed(parser, all, split, all.size(), collector);
        if (!ok || collector.records != expected || parser.pendingBytes()) {
            if (failures++ < 5)
                fprintf(stderr, "fmp4-harness: split at %zu: %zu samples, expected %zu\n", split, collector.records.size(), expected.size());
        }
        points++;
    }
    printf("SPLITS bytes=%zu points=%zu mode=%s samples=%zu failures=%zu\n", all.size(), points, sparse ? "sparse" : "every", expected.size(), failures);
    return failures ? 1 : 0;
}

int randomSplits(unsigned runs, unsigned seed, const std::vector<Bytes>& files)
{
    Bytes all = concatenate(files);
    auto expected = reference(all).records;
    std::mt19937_64 random(seed);
    size_t failures = 0, appends = 0;
    for (unsigned run = 0; run < runs; ++run) {
        FFmpegFMP4Parser parser;
        Collector collector;
        // small chunks in some runs, large ones in others
        size_t maxChunk = size_t(1) << (random() % 17);
        bool ok = true;
        for (size_t position = 0; ok && position < all.size();) {
            size_t chunk = std::min<size_t>(1 + random() % maxChunk, all.size() - position);
            ok = feed(parser, all, position, position + chunk, collector);
            position += chunk;
            appends++;
        }
        if (!ok || collector.records != expected || parser.pendingBytes()) {
            if (failures++ < 5)
                fprintf(stderr, "fmp4-harness: random run %u: %zu samples, expected %zu\n", run, collector.records.size(), expected.size());
        }
    }
    printf("RANDOM runs=%u seed=%u bytes=%zu appends=%zu samples=%zu failures=%zu\n", runs, seed, all.size(), appends, expected.size(), failures);
    return failures ? 1 : 0;
}

// files[0] the initialization segment, files[1..] media segments
int resets(const std::vector<Bytes>& files)
{
    if (files.size() < 4) {
        fprintf(stderr, "fmp4-harness: reset needs an init segment and three media segments\n");
        return 2;
    }
    // per media segment, the samples it gives on its own
    std::vector<std::vector<Record>> perSegment;
    {
        FFmpegFMP4Parser parser;
        Collector collector;
        feed(parser, files[0], 0, files[0].size(), collector);
        for (size_t i = 1; i < files.size(); ++i) {
            size_t before = collector.records.size();
            feed(parser, files[i], 0, files[i].size(), collector);
            perSegment.emplace_back(collector.records.begin() + before, collector.records.end());
        }
    }
    const Bytes& victim = files[2];
    auto boxes = topLevelBoxes(victim);
    size_t cases = 0, failures = 0;
    for (auto& box : boxes) {
        for (uint64_t cut : { box.start + 1, box.start + 8, (box.start + box.end) / 2, box.end - 1 }) {
            if (cut <= box.start || cut >= box.end)
                continue;
            FFmpegFMP4Parser parser;
            Collector collector;
            feed(parser, files[0], 0, files[0].size(), collector);
            feed(parser, files[1], 0, files[1].size(), collector);
            // the media segment stops in the middle of a box, then abort()
            feed(parser, victim, 0, cut, collector);
            size_t afterVictim = collector.records.size();
            parser.reset();
            bool ok = true;
            for (size_t i = 3; ok && i < files.size(); ++i)
                ok = feed(parser, files[i], 0, files[i].size(), collector);
            std::vector<Record> expected = perSegment[0];
            // samples of the victim reported before the cut stay reported (a complete mdat before it)
            expected.insert(expected.end(), collector.records.begin() + perSegment[0].size(), collector.records.begin() + afterVictim);
            for (size_t i = 2; i < perSegment.size(); ++i)
                expected.insert(expected.end(), perSegment[i].begin(), perSegment[i].end());
            if (!ok || collector.records != expected || parser.pendingBytes()) {
                if (failures++ < 5)
                    fprintf(stderr, "fmp4-harness: reset at %" PRIu64 ": %zu samples, expected %zu\n", cut, collector.records.size(), expected.size());
            }
            cases++;
        }
    }
    printf("RESET cases=%zu failures=%zu\n", cases, failures);
    return failures ? 1 : 0;
}

int switchInit(const std::vector<Bytes>& first, const std::vector<Bytes>& second)
{
    FFmpegFMP4Parser parser;
    Collector collector;
    for (auto& file : first)
        feed(parser, file, 0, file.size(), collector);
    size_t firstSamples = collector.records.size();
    std::string firstCodec = parser.initSegment()->tracks[0].codec;
    for (auto& file : second) {
        if (!feed(parser, file, 0, file.size(), collector))
            return 1;
    }
    auto expectedSecond = reference(concatenate(second)).records;
    std::vector<Record> secondSamples(collector.records.begin() + firstSamples, collector.records.end());
    bool ok = collector.inits == 2 && collector.generation == 2 && secondSamples == expectedSecond;
    printf("SWITCH inits=%u generation=%u codec=%s->%s samples=%zu+%zu %s\n", collector.inits, collector.generation, firstCodec.c_str(),
        parser.initSegment()->tracks[0].codec.c_str(), firstSamples, secondSamples.size(), ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int mutate(unsigned runs, unsigned seed, const std::vector<Bytes>& files)
{
    Bytes all = concatenate(files);
    std::mt19937_64 random(seed);
    size_t errors = 0;
    for (unsigned run = 0; run < runs; ++run) {
        Bytes copy = all;
        unsigned changes = 1 + random() % 8;
        for (unsigned i = 0; i < changes; ++i) {
            size_t at = random() % copy.size();
            // box headers are where it hurts: aim at the first bytes of a box half the time
            if (random() % 2) {
                auto boxes = topLevelBoxes(all);
                auto& box = boxes[random() % boxes.size()];
                at = box.start + random() % std::min<uint64_t>(box.end - box.start, 64);
            }
            copy[at] = static_cast<uint8_t>(random());
        }
        FFmpegFMP4Parser parser;
        Collector collector;
        size_t position = 0;
        while (position < copy.size()) {
            size_t chunk = std::min<size_t>(1 + random() % 65536, copy.size() - position);
            if (!parser.append(copy.data() + position, chunk, collector)) {
                errors++;
                break;
            }
            position += chunk;
        }
    }
    printf("MUTATE runs=%u seed=%u parse-errors=%zu (no sanitizer report = pass)\n", runs, seed, errors);
    return 0;
}

std::vector<Bytes> load(char** argv, int from, int to)
{
    std::vector<Bytes> files;
    for (int i = from; i < to; ++i)
        files.push_back(readFile(argv[i]));
    return files;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: fmp4-harness dump|splits|random|reset|switch|mutate ... FILE...\n");
        return 2;
    }
    std::string mode = argv[1];
    if (mode == "dump") {
        bool raw = !strcmp(argv[2], "--raw");
        return dump(raw, load(argv, raw ? 3 : 2, argc));
    }
    if (mode == "splits") {
        bool sparse = !strcmp(argv[2], "--sparse");
        return splits(sparse, load(argv, sparse ? 3 : 2, argc));
    }
    if (mode == "random" && argc > 4)
        return randomSplits(atoi(argv[2]), atoi(argv[3]), load(argv, 4, argc));
    if (mode == "reset")
        return resets(load(argv, 2, argc));
    if (mode == "switch") {
        int separator = 2;
        while (separator < argc && strcmp(argv[separator], "--"))
            separator++;
        return switchInit(load(argv, 2, separator), load(argv, separator + 1, argc));
    }
    if (mode == "mutate" && argc > 4)
        return mutate(atoi(argv[2]), atoi(argv[3]), load(argv, 4, argc));
    fprintf(stderr, "fmp4-harness: unknown mode %s\n", mode.c_str());
    return 2;
}
