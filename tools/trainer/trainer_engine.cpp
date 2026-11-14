// Multi-Threaded PC Trainer for RubikStudioPro 4x4 & 5x5 Optimal Solvers
// Phase A: Generates exact BFS center pruning databases (9 tables, RLE-compressed)
// Phase B: Evolutionary + coordinate weight & beam search over WCA 4x4 & 5x5 scrambles
// Periodically checkpoints verified C++ payloads atomically to CoreCpp/Common/TrainedPayload.hpp
// and live JSON telemetry to tools/trainer/status.json for Web UI monitoring.

#include "Cube3x3/Solver3x3_Min2Phase.hpp"
#include "Common/TrainedSolverCore.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace fs = std::filesystem;
using namespace TrainedData;
using namespace TrainedSolver;

namespace {

double getProcessRamMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS info;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info))) {
        return static_cast<double>(info.WorkingSetSize) / (1024.0 * 1024.0);
    }
#else
    struct rusage u;
    if (getrusage(RUSAGE_SELF, &u) == 0) {
        return static_cast<double>(u.ru_maxrss) / 1024.0;
    }
#endif
    return 0.0;
}

uint32_t fnv1a32(const uint8_t* data, size_t len, uint32_t seed = 2166136261u) {
    uint32_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t computeBankChecksum(const TableBank& bank) {
    uint32_t h = 2166136261u;
    const std::vector<uint8_t>* bufs[9] = {
        &bank.c4_ud, &bank.c4_s2, &bank.c4_s3,
        &bank.t5_ud, &bank.t5_s2, &bank.t5_s3,
        &bank.x5_ud, &bank.x5_s2, &bank.x5_s3
    };
    for (const auto* b : bufs) h = fnv1a32(b->data(), b->size(), h);
    return h;
}

bool atomicWriteFile(const fs::path& dst, const std::string& content) {
    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    fs::path tmp = dst;
    tmp += ".tmp";
    {
        std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.flush();
        if (!ofs.good()) return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(tmp.wstring().c_str(), dst.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::rename(tmp, dst, ec);
        if (ec) return false;
    }
#else
    fs::rename(tmp, dst, ec);
    if (ec) return false;
#endif
    return true;
}

struct Config {
    double hours = 5.0;
    int threads = 0;
    int valCount = 40;
    int batchCount = 16;
    double maxRamMB = 1500.0;
    bool phaseAOnly = false;
    fs::path outHeader = "CoreCpp/Common/TrainedPayload.hpp";
    fs::path statusPath = "tools/trainer/status.json";
    fs::path stopFlag = "tools/trainer/stop.flag";
};

struct CompressedTables {
    struct Entry {
        std::string name;
        size_t rawBytes;
        std::vector<uint8_t> rle;
    };
    std::vector<Entry> entries;
    uint32_t checksum = 0;
};

bool buildAndVerifyCompressedTables(const TableBank& bank, CompressedTables& out) {
    struct RawRef {
        const char* name;
        const std::vector<uint8_t>* vec;
    };
    RawRef refs[9] = {
        {"kC4_UD", &bank.c4_ud}, {"kC4_S2", &bank.c4_s2}, {"kC4_S3", &bank.c4_s3},
        {"kT5_UD", &bank.t5_ud}, {"kT5_S2", &bank.t5_s2}, {"kT5_S3", &bank.t5_s3},
        {"kX5_UD", &bank.x5_ud}, {"kX5_S2", &bank.x5_s2}, {"kX5_S3", &bank.x5_s3}
    };
    out.entries.clear();
    out.entries.reserve(9);
    std::vector<uint8_t> verifyBuf;
    for (const auto& r : refs) {
        if (r.vec->empty()) return false;
        auto enc = compressRLE(r.vec->data(), r.vec->size());
        verifyBuf.assign(r.vec->size(), 0);
        if (!decompressRLE(enc.data(), enc.size(), verifyBuf.data(), r.vec->size())) return false;
        if (std::memcmp(verifyBuf.data(), r.vec->data(), r.vec->size()) != 0) return false;
        out.entries.push_back({r.name, r.vec->size(), std::move(enc)});
    }
    out.checksum = computeBankChecksum(bank);
    return true;
}

void writeWeightAssigns(std::ostringstream& os, const HeuristicWeights& w) {
    os << std::fixed << std::setprecision(4);
    os << "    w.wPairedEdge = " << w.wPairedEdge << ";\n"
       << "    w.wHalfPair5x5 = " << w.wHalfPair5x5 << ";\n"
       << "    w.wUDParked = " << w.wUDParked << ";\n"
       << "    w.wSetup1Away = " << w.wSetup1Away << ";\n"
       << "    w.wMoveCost = " << w.wMoveCost << ";\n"
       << "    w.wEqMisaligned = " << w.wEqMisaligned << ";\n"
       << "    w.wBadFlip = " << w.wBadFlip << ";\n"
       << "    w.beamWidthFreeslice = " << w.beamWidthFreeslice << ";\n"
       << "    w.beamWidthL4E = " << w.beamWidthL4E << ";\n"
       << "    w.centerTopK = " << w.centerTopK << ";\n";
}

bool emitTrainedPayloadHeader(const fs::path& outPath,
                              const CompressedTables& tbl,
                              const HeuristicWeights& w4,
                              const HeuristicWeights& w5) {
    if (tbl.entries.size() != 9) return false;
    std::ostringstream os;
    os << "#pragma once\n"
       << "#include <cstdint>\n"
       << "#include <vector>\n\n"
       << "// Auto-generated by tools/trainer/trainer_engine.cpp\n"
       << "// Real precomputed RLE BFS pruning tables + WCA-evolved heuristic weights.\n\n"
       << "namespace TrainedData {\n\n"
       << "inline constexpr bool kHasPrecomputedTables = true;\n"
       << "inline constexpr uint32_t kPrecomputedChecksum = 0x"
       << std::hex << tbl.checksum << std::dec << "u;\n\n"
       << "inline HeuristicWeights defaultWeights4x4() {\n"
       << "    HeuristicWeights w;\n";
    writeWeightAssigns(os, w4);
    os << "    return w;\n}\n\n"
       << "inline HeuristicWeights defaultWeights5x5() {\n"
       << "    HeuristicWeights w;\n";
    writeWeightAssigns(os, w5);
    os << "    return w;\n}\n\n";

    static const char kHex[] = "0123456789abcdef";
    for (const auto& e : tbl.entries) {
        os << "inline const uint8_t " << e.name << "_rle[] = {\n";
        const size_t n = e.rle.size();
        for (size_t i = 0; i < n; ++i) {
            if (i % 24 == 0) os << "    ";
            uint8_t b = e.rle[i];
            os << "0x" << kHex[(b >> 4) & 0xF] << kHex[b & 0xF];
            if (i + 1 < n) os << ',';
            if ((i % 24 == 23) || (i + 1 == n)) os << '\n';
        }
        os << "};\n\n";
    }

    os << "inline bool loadCompressedInto(TableBank& bank) {\n"
       << "    bank.w4x4 = defaultWeights4x4();\n"
       << "    bank.w5x5 = defaultWeights5x5();\n"
       << "    bank.checksum = kPrecomputedChecksum;\n";

    static const char* fieldNames[9] = {
        "c4_ud", "c4_s2", "c4_s3",
        "t5_ud", "t5_s2", "t5_s3",
        "x5_ud", "x5_s2", "x5_s3"
    };
    for (int i = 0; i < 9; ++i) {
        const auto& e = tbl.entries[i];
        os << "    bank." << fieldNames[i] << ".resize(" << e.rawBytes << ");\n"
           << "    if (!decompressRLE(" << e.name << "_rle, sizeof(" << e.name
           << "_rle), bank." << fieldNames[i] << ".data(), " << e.rawBytes << ")) return false;\n";
    }
    os << "    bank.ready = true;\n"
       << "    return true;\n"
       << "}\n\n"
       << "} // namespace TrainedData\n";

    return atomicWriteFile(outPath, os.str());
}

struct EvalMetrics {
    int count = 0;
    int solved = 0;
    double successRate = 0.0;
    double lenMean = 0.0;
    int lenP95 = 0;
    int lenMax = 0;
    double msMean = 0.0;
    double msP50 = 0.0;
    double msP95 = 0.0;
    double loss = 1e9;
};

template <typename StateT, typename SolveFn>
EvalMetrics evaluateStatesParallel(const std::vector<StateT>& states, int numThreads, SolveFn&& solveFn) {
    const int n = static_cast<int>(states.size());
    EvalMetrics m;
    m.count = n;
    if (n == 0) return m;

    std::vector<int> lens(n, 0);
    std::vector<double> mss(n, 0.0);
    std::vector<uint8_t> ok(n, 0);
    std::atomic<int> nextIdx{0};

    int workers = std::max(1, std::min(numThreads, n));
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (int w = 0; w < workers; ++w) {
        pool.emplace_back([&]() {
            while (true) {
                int i = nextIdx.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;
                auto t0 = std::chrono::high_resolution_clock::now();
                SolveResult r = solveFn(states[i]);
                auto t1 = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                StateT chk = states[i];
                if (r.isValid) {
                    for (const auto& ph : r.phases) {
                        for (const auto& mv : ph.moves) chk.applyMove(mv);
                    }
                }
                ok[i] = (r.isValid && chk.isSolved()) ? 1 : 0;
                lens[i] = r.totalMoves;
                mss[i] = ms;
            }
        });
    }
    for (auto& th : pool) th.join();

    int solved = 0;
    double sumL = 0.0, sumMs = 0.0;
    for (int i = 0; i < n; ++i) {
        if (ok[i]) solved++;
        sumL += lens[i];
        sumMs += mss[i];
    }
    std::sort(lens.begin(), lens.end());
    std::sort(mss.begin(), mss.end());
    auto pctIdx = [&](double p) -> size_t {
        size_t k = static_cast<size_t>(std::ceil(p * n));
        return (k == 0) ? 0 : std::min<size_t>(n - 1, k - 1);
    };

    m.solved = solved;
    m.successRate = static_cast<double>(solved) / n;
    m.lenMean = sumL / n;
    m.lenP95 = lens[pctIdx(0.95)];
    m.lenMax = lens.back();
    m.msMean = sumMs / n;
    m.msP50 = mss[pctIdx(0.50)];
    m.msP95 = mss[pctIdx(0.95)];
    return m;
}

double score4x4Objective(const EvalMetrics& m) {
    if (m.successRate < 1.0) return 1e6 + (1.0 - m.successRate) * 1e5;
    double pen = 0.0;
    if (m.msP50 > 1350.0) pen += (m.msP50 - 1350.0) * 0.008;
    if (m.msP95 > 3600.0) pen += (m.msP95 - 3600.0) * 0.008;
    if (m.lenMax > 96) pen += (m.lenMax - 96) * 1.5;
    return m.lenMean + 0.05 * m.lenP95 + pen;
}

double score5x5Objective(const EvalMetrics& m) {
    if (m.successRate < 1.0) return 1e6 + (1.0 - m.successRate) * 1e5;
    double pen = 0.0;
    if (m.msP50 > 3150.0) pen += (m.msP50 - 3150.0) * 0.006;
    if (m.msP95 > 7200.0) pen += (m.msP95 - 7200.0) * 0.006;
    if (m.lenMax > 155) pen += (m.lenMax - 155) * 1.5;
    return m.lenMean + 0.05 * m.lenP95 + pen;
}

std::vector<State4x4> generateScrambles4x4(int count, std::mt19937& rng, int depth = 40) {
    static const uint8_t masks[2] = {0x01, 0x03};
    std::vector<State4x4> out(count);
    for (int i = 0; i < count; ++i) {
        State4x4 s;
        int la = -1, ls = -1;
        for (int k = 0; k < depth; ++k) {
            uint8_t a, sl;
            do {
                a = static_cast<uint8_t>(rng() % 6);
                sl = masks[rng() % 2];
            } while (a == la && sl == ls);
            la = a; ls = sl;
            s.applyMove(a, static_cast<uint8_t>((rng() % 3) + 1), sl);
        }
        out[i] = s;
    }
    return out;
}

std::vector<State5x5> generateScrambles5x5(int count, std::mt19937& rng, int depth = 60) {
    static const uint8_t masks[2] = {0x01, 0x03};
    std::vector<State5x5> out(count);
    for (int i = 0; i < count; ++i) {
        State5x5 s;
        int la = -1, ls = -1;
        for (int k = 0; k < depth; ++k) {
            uint8_t a, sl;
            do {
                a = static_cast<uint8_t>(rng() % 6);
                sl = masks[rng() % 2];
            } while (a == la && sl == ls);
            la = a; ls = sl;
            s.applyMove(a, static_cast<uint8_t>((rng() % 3) + 1), sl);
        }
        out[i] = s;
    }
    return out;
}

double clampD(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
int clampI(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

HeuristicWeights perturbWeights(const HeuristicWeights& base, std::mt19937& rng, bool is5x5, double temp) {
    HeuristicWeights w = base;
    std::normal_distribution<double> N(0.0, 1.0);
    // Perturb 2 to 4 parameters per candidate (coordinate + local joint search)
    int numChanges = 2 + static_cast<int>(rng() % 3);
    for (int c = 0; c < numChanges; ++c) {
        int idx = static_cast<int>(rng() % (is5x5 ? 9 : 8));
        if (!is5x5 && idx >= 1) idx++; // skip wHalfPair5x5 on 4x4
        switch (idx) {
            case 0: w.wPairedEdge   = clampD(w.wPairedEdge + N(rng) * 12.0 * temp, 40.0, 180.0); break;
            case 1: w.wHalfPair5x5  = clampD(w.wHalfPair5x5 + N(rng) * 5.0 * temp, 4.0, 60.0); break;
            case 2: w.wUDParked     = clampD(w.wUDParked + N(rng) * 4.5 * temp, 0.0, 45.0); break;
            case 3: w.wSetup1Away   = clampD(w.wSetup1Away + N(rng) * 3.5 * temp, 0.0, 35.0); break;
            case 4: w.wMoveCost     = clampD(w.wMoveCost + N(rng) * 2.2 * temp, 1.0, 22.0); break;
            case 5: w.wEqMisaligned = clampD(w.wEqMisaligned + N(rng) * 2.5 * temp, 0.0, 24.0); break;
            case 6: w.wBadFlip      = clampD(w.wBadFlip + N(rng) * 2.0 * temp, 0.0, 20.0); break;
            case 7: {
                int step = (static_cast<int>(std::round(N(rng) * 16.0 * temp)));
                if (step == 0) step = (rng() % 2 == 0) ? 8 : -8;
                w.beamWidthFreeslice = clampI(w.beamWidthFreeslice + step, 64, is5x5 ? 176 : 176);
                break;
            }
            case 8: {
                int step = (rng() % 2 == 0) ? 1 : -1;
                w.centerTopK = clampI(w.centerTopK + step, 3, is5x5 ? 6 : 8);
                break;
            }
        }
    }
    return w;
}

struct HistoryPoint {
    int epoch = 0;
    double elapsedSec = 0.0;
    double mean4 = 0.0;
    double p95_4 = 0.0;
    double ms4 = 0.0;
    double mean5 = 0.0;
    double p95_5 = 0.0;
    double ms5 = 0.0;
    bool accepted = false;
};

struct TrainerStatus {
    std::string state = "starting"; // bfs_tables | training | validating | completed | stopped | error
    std::string message = "Initializing trainer...";
    int numThreads = 1;
    double targetHours = 5.0;
    double elapsedSec = 0.0;
    double etaSec = 0.0;
    double ramMB = 0.0;
    double maxRamMB = 1500.0;
    double tablesPct = 0.0;
    int tablesBuilt = 0;
    int epoch = 0;
    int valCount = 0;
    int batchCount = 0;
    int acceptedUpdates = 0;
    bool payloadSaved = false;
    uint32_t checksum = 0;
    size_t rleTotalBytes = 0;
    EvalMetrics best4, cur4;
    EvalMetrics best5, cur5;
    HeuristicWeights w4, w5;
    std::vector<HistoryPoint> history;
    std::vector<std::string> logs;
};

std::string jsonEscape(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        if (c == '\\') o << "\\\\";
        else if (c == '"') o << "\\\"";
        else if (c == '\n') o << "\\n";
        else if (c == '\r') {}
        else o << c;
    }
    return o.str();
}

void writeMetricsJson(std::ostringstream& os, const EvalMetrics& m) {
    os << std::fixed << std::setprecision(2)
       << "{\"count\":" << m.count
       << ",\"solved\":" << m.solved
       << ",\"success\":" << std::setprecision(4) << m.successRate
       << std::setprecision(2)
       << ",\"len_mean\":" << m.lenMean
       << ",\"len_p95\":" << m.lenP95
       << ",\"len_max\":" << m.lenMax
       << ",\"ms_p50\":" << m.msP50
       << ",\"ms_p95\":" << m.msP95 << "}";
}

void writeWeightsJson(std::ostringstream& os, const HeuristicWeights& w) {
    os << std::fixed << std::setprecision(2)
       << "{\"wPairedEdge\":" << w.wPairedEdge
       << ",\"wHalfPair5x5\":" << w.wHalfPair5x5
       << ",\"wUDParked\":" << w.wUDParked
       << ",\"wSetup1Away\":" << w.wSetup1Away
       << ",\"wMoveCost\":" << w.wMoveCost
       << ",\"wEqMisaligned\":" << w.wEqMisaligned
       << ",\"wBadFlip\":" << w.wBadFlip
       << ",\"beamWidthFreeslice\":" << w.beamWidthFreeslice
       << ",\"centerTopK\":" << w.centerTopK << "}";
}

void flushStatusJson(const Config& cfg, TrainerStatus& st) {
    st.ramMB = getProcessRamMB();
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << "{\n"
       << "  \"state\": \"" << jsonEscape(st.state) << "\",\n"
       << "  \"message\": \"" << jsonEscape(st.message) << "\",\n"
       << "  \"threads\": " << st.numThreads << ",\n"
       << "  \"target_hours\": " << st.targetHours << ",\n"
       << "  \"elapsed_sec\": " << st.elapsedSec << ",\n"
       << "  \"eta_sec\": " << st.etaSec << ",\n"
       << "  \"ram_mb\": " << st.ramMB << ",\n"
       << "  \"max_ram_mb\": " << st.maxRamMB << ",\n"
       << "  \"tables_pct\": " << st.tablesPct << ",\n"
       << "  \"tables_built\": " << st.tablesBuilt << ",\n"
       << "  \"epoch\": " << st.epoch << ",\n"
       << "  \"val_count\": " << st.valCount << ",\n"
       << "  \"batch_count\": " << st.batchCount << ",\n"
       << "  \"accepted_updates\": " << st.acceptedUpdates << ",\n"
       << "  \"payload_saved\": " << (st.payloadSaved ? "true" : "false") << ",\n"
       << "  \"checksum\": \"" << std::hex << st.checksum << std::dec << "\",\n"
       << "  \"rle_bytes\": " << st.rleTotalBytes << ",\n"
       << "  \"baseline_4x4\": 177.1,\n"
       << "  \"threshold_4x4\": 75.0,\n"
       << "  \"baseline_5x5\": 456.7,\n"
       << "  \"threshold_5x5\": 120.0,\n";
    os << "  \"best_4x4\": "; writeMetricsJson(os, st.best4); os << ",\n";
    os << "  \"cur_4x4\": ";  writeMetricsJson(os, st.cur4);  os << ",\n";
    os << "  \"best_5x5\": "; writeMetricsJson(os, st.best5); os << ",\n";
    os << "  \"cur_5x5\": ";  writeMetricsJson(os, st.cur5);  os << ",\n";
    os << "  \"weights_4x4\": "; writeWeightsJson(os, st.w4); os << ",\n";
    os << "  \"weights_5x5\": "; writeWeightsJson(os, st.w5); os << ",\n";

    os << "  \"history\": [\n";
    for (size_t i = 0; i < st.history.size(); ++i) {
        const auto& h = st.history[i];
        os << "    {\"epoch\":" << h.epoch
           << ",\"t\":" << h.elapsedSec
           << ",\"m4\":" << h.mean4 << ",\"p4\":" << h.p95_4 << ",\"ms4\":" << h.ms4
           << ",\"m5\":" << h.mean5 << ",\"p5\":" << h.p95_5 << ",\"ms5\":" << h.ms5
           << ",\"acc\":" << (h.accepted ? "true" : "false") << "}";
        if (i + 1 < st.history.size()) os << ",";
        os << "\n";
    }
    os << "  ],\n  \"logs\": [\n";
    for (size_t i = 0; i < st.logs.size(); ++i) {
        os << "    \"" << jsonEscape(st.logs[i]) << "\"";
        if (i + 1 < st.logs.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n}\n";
    atomicWriteFile(cfg.statusPath, os.str());
}

void pushLog(TrainerStatus& st, const std::string& line) {
    if (st.logs.size() >= 60) st.logs.erase(st.logs.begin());
    st.logs.push_back(line);
    std::cout << line << std::endl;
}

void pushHistory(TrainerStatus& st, const HistoryPoint& pt) {
    if (st.history.size() >= 400) st.history.erase(st.history.begin());
    st.history.push_back(pt);
}

Config parseArgs(int argc, char** argv) {
    Config c;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--hours" && i + 1 < argc) c.hours = std::stod(argv[++i]);
        else if (a == "--threads" && i + 1 < argc) c.threads = std::stoi(argv[++i]);
        else if (a == "--val" && i + 1 < argc) c.valCount = std::stoi(argv[++i]);
        else if (a == "--batch" && i + 1 < argc) c.batchCount = std::stoi(argv[++i]);
        else if (a == "--max-ram-mb" && i + 1 < argc) c.maxRamMB = std::stod(argv[++i]);
        else if (a == "--out" && i + 1 < argc) c.outHeader = argv[++i];
        else if (a == "--status" && i + 1 < argc) c.statusPath = argv[++i];
        else if (a == "--stop-file" && i + 1 < argc) c.stopFlag = argv[++i];
        else if (a == "--phase-a-only") c.phaseAOnly = true;
    }
    if (c.threads <= 0) {
        unsigned hc = std::thread::hardware_concurrency();
        c.threads = clampI(hc ? static_cast<int>(hc) : 4, 1, 16);
    }
    return c;
}

} // namespace

int main(int argc, char** argv) {
    Config cfg = parseArgs(argc, argv);
    std::error_code ec;
    fs::remove(cfg.stopFlag, ec);

    TrainerStatus st;
    st.numThreads = cfg.threads;
    st.targetHours = cfg.hours;
    st.maxRamMB = cfg.maxRamMB;
    st.valCount = cfg.valCount;
    st.batchCount = cfg.batchCount;

    auto tTrainStart = std::chrono::steady_clock::now();
    auto elapsedSecNow = [&]() -> double {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - tTrainStart).count();
    };

    // ─── PHASE A: Build & RLE-Compress All 9 Exact BFS Tables ──────────────────
    st.state = "bfs_tables";
    st.message = "Initializing 3x3 & building 9 center BFS pruning tables...";
    pushLog(st, "[Phase A] Initializing 3x3 Min2Phase + StageMoveMaps...");
    flushStatusJson(cfg, st);

    Solver3x3_Min2Phase::initTables();
    auto& ctx = engineCtx();
    const auto& cat = catalog();
    ctx.map4.init(cat.c4Perm);
    ctx.map5T.init(cat.t5Perm);
    ctx.map5X.init(cat.x5Perm);

    TableBank buildBank;
    buildBank.w4x4 = defaultWeights4x4();
    buildBank.w5x5 = defaultWeights5x5();

    auto lastJsonFlush = std::chrono::steady_clock::now();
    auto runTableGroup = [&](const char* label, int baseGroupIdx,
                             const uint8_t perm[36][24], const StageMoveMaps& maps,
                             std::vector<uint8_t>& ud, std::vector<uint8_t>& s2, std::vector<uint8_t>& s3) {
        int lastStageCode = -1;
        auto cb = [&](const char* stage, int depth, uint32_t vis, uint32_t total) {
            int sub = (std::strcmp(stage, "UD") == 0) ? 0 : ((std::strcmp(stage, "S2") == 0) ? 1 : 2);
            int tblIdx = baseGroupIdx * 3 + sub;
            double frac = total > 0 ? std::min(1.0, static_cast<double>(vis) / total) : 1.0;
            if (depth == 99) {
                frac = 1.0;
                st.tablesBuilt = std::max(st.tablesBuilt, tblIdx + 1);
            }
            st.tablesPct = ((tblIdx + frac) / 9.0) * 100.0;
            st.elapsedSec = elapsedSecNow();
            st.message = std::string("BFS ") + label + " " + stage + " depth=" + std::to_string(depth)
                       + " (" + std::to_string(vis) + "/" + std::to_string(total) + ")";
            auto now = std::chrono::steady_clock::now();
            if (depth == 99 || sub != lastStageCode ||
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastJsonFlush).count() >= 500) {
                lastJsonFlush = now;
                lastStageCode = sub;
                flushStatusJson(cfg, st);
            }
        };
        EngineContext::buildCenterTablesBFS(perm, maps, ud, s2, s3, cb);
    };

    runTableGroup("4x4 Centers", 0, cat.c4Perm, ctx.map4, buildBank.c4_ud, buildBank.c4_s2, buildBank.c4_s3);
    runTableGroup("5x5 T-Centers", 1, cat.t5Perm, ctx.map5T, buildBank.t5_ud, buildBank.t5_s2, buildBank.t5_s3);
    runTableGroup("5x5 X-Centers", 2, cat.x5Perm, ctx.map5X, buildBank.x5_ud, buildBank.x5_s2, buildBank.x5_s3);
    buildBank.ready = true;

    CompressedTables cTables;
    if (!buildAndVerifyCompressedTables(buildBank, cTables)) {
        st.state = "error";
        st.message = "RLE compression round-trip verification failed.";
        pushLog(st, "[ERROR] RLE verification failed!");
        flushStatusJson(cfg, st);
        return 1;
    }

    size_t rleSum = 0;
    for (const auto& e : cTables.entries) rleSum += e.rle.size();
    st.rleTotalBytes = rleSum;
    st.checksum = cTables.checksum;
    st.tablesPct = 100.0;
    st.tablesBuilt = 9;

    // Copy freshly built bank into EngineContext and mark initialized before saving/evaluating
    ctx.bank = std::move(buildBank);
    ctx.ensureInitialized();

    st.w4 = ctx.bank.w4x4;
    st.w5 = ctx.bank.w5x5;
    if (!emitTrainedPayloadHeader(cfg.outHeader, cTables, st.w4, st.w5)) {
        st.state = "error";
        st.message = "Failed writing CoreCpp/Common/TrainedPayload.hpp";
        pushLog(st, "[ERROR] Cannot write " + cfg.outHeader.string());
        flushStatusJson(cfg, st);
        return 1;
    }
    st.payloadSaved = true;
    pushLog(st, "[Phase A] 9 BFS tables verified & saved (" + std::to_string(rleSum / 1024) + " KB RLE, CRC="
                + [&]() { std::ostringstream ss; ss << std::hex << st.checksum; return ss.str(); }() + ").");
    flushStatusJson(cfg, st);

    if (cfg.phaseAOnly || cfg.hours <= 0.0) {
        st.state = "completed";
        st.message = "Phase A tables generated and saved.";
        st.elapsedSec = elapsedSecNow();
        flushStatusJson(cfg, st);
        return 0;
    }

    // ─── PHASE B: Real WCA-Scramble Evolutionary / Coordinate Optimization ─────
    const double totalBudgetSec = cfg.hours * 3600.0;
    std::mt19937 valRng(20261114u);
    auto val4 = generateScrambles4x4(cfg.valCount, valRng, 40);
    auto val5 = generateScrambles5x5(cfg.valCount, valRng, 60);

    st.state = "validating";
    st.message = "Evaluating baseline weights on fixed " + std::to_string(cfg.valCount) + "-scramble WCA validation set...";
    flushStatusJson(cfg, st);

    if (fs::exists(cfg.stopFlag, ec)) {
        st.elapsedSec = elapsedSecNow();
        st.etaSec = 0.0;
        st.state = "stopped";
        st.message = "Training stopped cleanly after Phase A save.";
        pushLog(st, "[Stop Signal] stop.flag detected after Phase A; saved payload and exiting cleanly.");
        flushStatusJson(cfg, st);
        return 0;
    }

    EvalMetrics base4 = evaluateStatesParallel(val4, cfg.threads, [&](const State4x4& s) {
        return solve4x4WithWeights(s, st.w4, false);
    });
    base4.loss = score4x4Objective(base4);
    EvalMetrics base5 = evaluateStatesParallel(val5, cfg.threads, [&](const State5x5& s) {
        return solve5x5WithWeights(s, st.w5, false);
    });
    base5.loss = score5x5Objective(base5);

    st.best4 = st.cur4 = base4;
    st.best5 = st.cur5 = base5;
    st.elapsedSec = elapsedSecNow();
    st.etaSec = std::max(0.0, totalBudgetSec - st.elapsedSec);
    pushHistory(st, {0, st.elapsedSec, base4.lenMean, (double)base4.lenP95, base4.msP50,
                     base5.lenMean, (double)base5.lenP95, base5.msP50, true});

    {
        std::ostringstream ls;
        ls << std::fixed << std::setprecision(2)
           << "[Epoch 0 Baseline] 4x4: " << base4.solved << "/" << base4.count
           << " len=" << base4.lenMean << " (p95=" << base4.lenP95 << ") " << base4.msP50 << "ms"
           << " | 5x5: " << base5.solved << "/" << base5.count
           << " len=" << base5.lenMean << " (p95=" << base5.lenP95 << ") " << base5.msP50 << "ms";
        pushLog(st, ls.str());
    }
    flushStatusJson(cfg, st);

    std::mt19937 trainRng(777123u);
    bool stoppedEarly = false;

    for (int epoch = 1; ; ++epoch) {
        st.elapsedSec = elapsedSecNow();
        if (st.elapsedSec >= totalBudgetSec) break;
        if (fs::exists(cfg.stopFlag, ec)) {
            stoppedEarly = true;
            pushLog(st, "[Stop Signal] stop.flag detected. Saving best checkpoint and exiting cleanly.");
            break;
        }
        double curRam = getProcessRamMB();
        if (curRam > cfg.maxRamMB) {
            pushLog(st, "[RAM Guard] Process RAM reached " + std::to_string((int)curRam) + " MB. Stopping safely.");
            stoppedEarly = true;
            break;
        }

        double progressFrac = std::min(1.0, st.elapsedSec / totalBudgetSec);
        double temp = std::max(0.25, 1.0 - 0.70 * progressFrac);

        auto batch4 = generateScrambles4x4(cfg.batchCount, trainRng, 40);
        auto batch5 = generateScrambles5x5(cfg.batchCount, trainRng, 60);

        HeuristicWeights candW4 = perturbWeights(st.w4, trainRng, false, temp);
        HeuristicWeights candW5 = perturbWeights(st.w5, trainRng, true, temp);

        st.epoch = epoch;
        st.state = "training";
        st.etaSec = std::max(0.0, totalBudgetSec - st.elapsedSec);
        st.message = "Epoch " + std::to_string(epoch) + ": optimizing on " + std::to_string(cfg.batchCount)
                   + " WCA scrambles (" + std::to_string(cfg.threads) + " threads)...";
        flushStatusJson(cfg, st);

        // Evaluate incumbent vs candidate on SAME batch (common random numbers -> low variance!)
        EvalMetrics incB4 = evaluateStatesParallel(batch4, cfg.threads, [&](const State4x4& s) {
            return solve4x4WithWeights(s, st.w4, false);
        });
        EvalMetrics tryB4 = evaluateStatesParallel(batch4, cfg.threads, [&](const State4x4& s) {
            return solve4x4WithWeights(s, candW4, false);
        });
        incB4.loss = score4x4Objective(incB4);
        tryB4.loss = score4x4Objective(tryB4);

        EvalMetrics incB5 = evaluateStatesParallel(batch5, cfg.threads, [&](const State5x5& s) {
            return solve5x5WithWeights(s, st.w5, false);
        });
        EvalMetrics tryB5 = evaluateStatesParallel(batch5, cfg.threads, [&](const State5x5& s) {
            return solve5x5WithWeights(s, candW5, false);
        });
        incB5.loss = score5x5Objective(incB5);
        tryB5.loss = score5x5Objective(tryB5);

        bool candBeat4 = (tryB4.successRate == 1.0 && tryB4.loss < incB4.loss - 0.05);
        bool candBeat5 = (tryB5.successRate == 1.0 && tryB5.loss < incB5.loss - 0.05);

        st.cur4 = tryB4;
        st.cur5 = tryB5;
        bool promotedAny = false;

        if (candBeat4 || candBeat5) {
            st.state = "validating";
            st.message = "Epoch " + std::to_string(epoch) + ": validating promising weights on full "
                       + std::to_string(cfg.valCount) + "-cube held-out set...";
            flushStatusJson(cfg, st);

            if (candBeat4) {
                EvalMetrics v4 = evaluateStatesParallel(val4, cfg.threads, [&](const State4x4& s) {
                    return solve4x4WithWeights(s, candW4, false);
                });
                v4.loss = score4x4Objective(v4);
                st.cur4 = v4;
                if (v4.successRate == 1.0 && v4.loss < st.best4.loss - 0.02) {
                    st.best4 = v4;
                    st.w4 = candW4;
                    promotedAny = true;
                }
            }
            if (candBeat5) {
                EvalMetrics v5 = evaluateStatesParallel(val5, cfg.threads, [&](const State5x5& s) {
                    return solve5x5WithWeights(s, candW5, false);
                });
                v5.loss = score5x5Objective(v5);
                st.cur5 = v5;
                if (v5.successRate == 1.0 && v5.loss < st.best5.loss - 0.02) {
                    st.best5 = v5;
                    st.w5 = candW5;
                    promotedAny = true;
                }
            }
            if (promotedAny) {
                st.acceptedUpdates++;
                if (emitTrainedPayloadHeader(cfg.outHeader, cTables, st.w4, st.w5)) {
                    st.payloadSaved = true;
                }
            }
        }

        st.elapsedSec = elapsedSecNow();
        st.etaSec = std::max(0.0, totalBudgetSec - st.elapsedSec);
        pushHistory(st, {epoch, st.elapsedSec,
                         st.best4.lenMean, (double)st.best4.lenP95, st.best4.msP50,
                         st.best5.lenMean, (double)st.best5.lenP95, st.best5.msP50, promotedAny});

        std::ostringstream ls;
        ls << std::fixed << std::setprecision(2)
           << "[Epoch " << epoch << (promotedAny ? " *SAVED*" : "") << "] "
           << "Best 4x4=" << st.best4.lenMean << "m (" << st.best4.msP50 << "ms) | "
           << "Best 5x5=" << st.best5.lenMean << "m (" << st.best5.msP50 << "ms) | "
           << "Batch=" << tryB4.lenMean << "/" << tryB5.lenMean;
        pushLog(st, ls.str());
        flushStatusJson(cfg, st);
    }

    // Final atomic save of verified best checkpoint
    st.payloadSaved = emitTrainedPayloadHeader(cfg.outHeader, cTables, st.w4, st.w5);
    st.elapsedSec = elapsedSecNow();
    st.etaSec = 0.0;
    st.state = stoppedEarly ? "stopped" : "completed";
    st.message = stoppedEarly
        ? "Training stopped cleanly and best checkpoint saved."
        : "Training completed and best checkpoint saved to CoreCpp/Common/TrainedPayload.hpp.";
    pushLog(st, std::string("[Done] ") + st.message + " Accepted improvements: " + std::to_string(st.acceptedUpdates));
    flushStatusJson(cfg, st);
    return st.payloadSaved ? 0 : 1;
}
