#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <unistd.h>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <cstring>
#include <fstream>
#include "mprpcapplication.h"
#include "user.pb.h"
#include "friend.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"

// ─── Configuration overridable from command line ───
static int g_warmup = 50;
static int g_duration_sec = 5;
static std::vector<int> g_concurrency = {1, 2, 4, 8, 16};
static std::string g_method = "login";
static bool g_quiet = false;
static bool g_show_blocked = false;   // -b: show blocked count
static int g_algo = -1;               // -a: algorithm override (0-4), -1 = use config
static int g_max_qps = -1;            // -t: max_qps override, -1 = use config
static std::string g_csv_file;        // -o <file>: write CSV output
static std::string g_direct_addr;     // -p ip:port: direct connect bypassing ZK
static bool g_provider_dist = false;  // --provider-dist: show per-provider distribution

// ─── Per-thread result ───
struct ThreadResult {
    int64_t calls = 0;
    int64_t errors = 0;
    int64_t blocked = 0;
    std::vector<double> latencies_us;
};

struct RoundResult {
    int concurrency = 0;
    std::string label;
    double duration_sec = 0;
    int64_t total_calls = 0;
    int64_t total_errors = 0;
    int64_t total_blocked = 0;
    std::vector<double> all_us;
};

struct Stats {
    double qps;
    double success_rate;
    double avg_ms, min_ms, max_ms, p50_ms, p95_ms, p99_ms;
};

static Stats compute(const RoundResult& r) {
    Stats s = {};
    if (r.total_calls == 0) return s;
    s.qps = r.total_calls / r.duration_sec;
    s.success_rate = 100.0 * (1.0 - (double)r.total_errors / r.total_calls);

    auto& v = r.all_us;
    if (v.empty()) return s;
    size_t n = v.size();
    s.min_ms = v.front() / 1000.0;
    s.max_ms = v.back() / 1000.0;
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    s.avg_ms = (sum / n) / 1000.0;
    s.p50_ms = v[std::min(n - 1, (size_t)(n * 0.50))] / 1000.0;
    s.p95_ms = v[std::min(n - 1, (size_t)(n * 0.95))] / 1000.0;
    s.p99_ms = v[std::min(n - 1, (size_t)(n * 0.99))] / 1000.0;
    return s;
}

// ─── Single worker thread ───
static ThreadResult worker(int id, int warmup, int duration_sec,
                           const std::string& method,
                           const std::string& direct_addr = "") {
    ThreadResult r;
    MprpcChannel channel;
    if (!direct_addr.empty()) {
        channel.setDirectAddress(direct_addr);
    }
    fixbug::UserServiceRpc_Stub stub(&channel);

    fixbug::LoginRequest login_req;
    login_req.set_name("user_" + std::to_string(id));
    login_req.set_pwd("pwd_" + std::to_string(id));

    fixbug::RegisterRequest reg_req;
    reg_req.set_name("user_" + std::to_string(id));
    reg_req.set_pwd("pwd_" + std::to_string(id));

    // Warmup
    for (int i = 0; i < warmup; i++) {
        MprpcController ctrl;
        auto w_start = std::chrono::steady_clock::now();
        if (method == "register") {
            fixbug::RegisterResponse resp;
            stub.Register(&ctrl, &reg_req, &resp, nullptr);
        } else {
            fixbug::LoginResponse resp;
            stub.Login(&ctrl, &login_req, &resp, nullptr);
        }
        auto w_end = std::chrono::steady_clock::now();
        int64_t w_ms = std::chrono::duration_cast<std::chrono::milliseconds>(w_end - w_start).count();
        if (w_ms > 100) {
            std::cout << "[DEBUG] Warmup call " << i << " took " << w_ms << "ms, failed=" << ctrl.Failed();
            if (ctrl.Failed()) std::cout << " err=" << ctrl.ErrorText();
            std::cout << std::endl;
        }
    }

    r.latencies_us.reserve(500000);

    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count()
            >= duration_sec)
            break;

        auto op_start = std::chrono::steady_clock::now();
        MprpcController ctrl;

        if (method == "register") {
            fixbug::RegisterResponse resp;
            stub.Register(&ctrl, &reg_req, &resp, nullptr);
        } else {
            fixbug::LoginResponse resp;
            stub.Login(&ctrl, &login_req, &resp, nullptr);
        }

        auto op_end = std::chrono::steady_clock::now();
        r.calls++;
        r.latencies_us.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(
                op_end - op_start).count());
        if (ctrl.Failed()) {
            r.errors++;
            if (ctrl.ErrorText().find("rate limit") != std::string::npos)
                r.blocked++;
        }
    }
    return r;
}

static void print_separator(char c = '=') {
    std::cout << std::string(100, c) << std::endl;
}

static void print_header() {
    print_separator();
    std::cout << "  MPRPC Performance Test"
              << "    method=" << g_method
              << "    duration=" << g_duration_sec << "s"
              << "    warmup=" << g_warmup;
    if (g_algo >= 0) std::cout << "    algo=" << g_algo;
    if (g_max_qps > 0) std::cout << "    max_qps=" << g_max_qps;
    if (!g_direct_addr.empty()) std::cout << "    direct=" << g_direct_addr;
    if (g_provider_dist) std::cout << "    provider-dist";
    std::cout << std::endl;
    print_separator();
}

static void print_table_header(bool multi_round) {
    if (multi_round) {
        std::cout << std::left
                  << std::setw(6) << "Thrds"
                  << std::setw(12) << "QPS"
                  << std::setw(10) << "Succ%"
                  << std::setw(10) << "Blk%"
                  << std::setw(10) << "Avg(ms)"
                  << std::setw(10) << "Min(ms)"
                  << std::setw(10) << "Max(ms)"
                  << std::setw(10) << "P50(ms)"
                  << std::setw(10) << "P95(ms)"
                  << std::setw(10) << "P99(ms)"
                  << std::setw(10) << "Total"
                  << std::endl;
        print_separator('-');
    }
}

static void print_stats_row(const std::string& label, const Stats& s,
                            int64_t total, int64_t blocked = 0) {
    double blk_pct = total > 0 ? 100.0 * blocked / total : 0;
    std::cout << std::left
              << std::setw(6) << label
              << std::setw(12) << std::fixed << std::setprecision(0) << s.qps
              << std::setw(10) << std::fixed << std::setprecision(1) << s.success_rate
              << std::setw(10) << std::fixed << std::setprecision(1) << blk_pct
              << std::setw(10) << std::fixed << std::setprecision(2) << s.avg_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.min_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.max_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.p50_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.p95_ms
              << std::setw(10) << std::fixed << std::setprecision(2) << s.p99_ms
              << std::setw(10) << total
              << std::endl;
}

static void print_round(const RoundResult& round) {
    Stats s = compute(round);
    print_stats_row(std::to_string(round.concurrency), s, round.total_calls, round.total_blocked);
}

static void print_footer(const std::vector<RoundResult>& rounds) {
    print_separator('-');
    if (rounds.empty()) return;
    // Best QPS row
    auto best = std::max_element(rounds.begin(), rounds.end(),
        [](const RoundResult& a, const RoundResult& b) {
            return compute(a).qps < compute(b).qps;
        });
    Stats s = compute(*best);
    std::cout << "  Best QPS: " << s.qps << " at concurrency="
              << best->concurrency << std::endl;

    // Latency at best QPS
    std::cout << "  Latency @ best QPS: avg=" << s.avg_ms
              << "ms  p99=" << s.p99_ms << "ms" << std::endl;
    print_separator();
}

static void run_single_round(int concurrency, std::vector<RoundResult>& rounds) {
    double duration_sec = (double)g_duration_sec;

    // Launch workers
    std::vector<std::thread> threads;
    std::vector<ThreadResult> results(concurrency);

    auto test_start = std::chrono::steady_clock::now();

    for (int i = 0; i < concurrency; i++) {
        threads.emplace_back([i, &results]() {
            results[i] = worker(i, g_warmup, g_duration_sec, g_method, g_direct_addr);
        });
    }

    for (auto& t : threads) t.join();

    auto test_end = std::chrono::steady_clock::now();
    duration_sec = std::chrono::duration_cast<std::chrono::milliseconds>(
                       test_end - test_start).count() / 1000.0;

    // Merge
    RoundResult round;
    round.concurrency = concurrency;
    round.duration_sec = duration_sec;

    for (auto& r : results) {
        round.total_calls += r.calls;
        round.total_errors += r.errors;
        round.total_blocked += r.blocked;
        round.all_us.insert(round.all_us.end(),
                            r.latencies_us.begin(), r.latencies_us.end());
    }

    std::sort(round.all_us.begin(), round.all_us.end());
    rounds.push_back(round);

    if (!g_quiet) {
        print_round(round);
    }
}

// ─── Parse command line ───
static void parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
            g_warmup = atoi(argv[++i]);
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            g_duration_sec = atoi(argv[++i]);
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            g_concurrency.clear();
            char* tok = strtok(argv[++i], ",");
            while (tok) { g_concurrency.push_back(atoi(tok)); tok = strtok(nullptr, ","); }
        }
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
            g_method = argv[++i];
        else if (strcmp(argv[i], "-q") == 0)
            g_quiet = true;
        else if (strcmp(argv[i], "-b") == 0)
            g_show_blocked = true;
        else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc)
            g_algo = atoi(argv[++i]);
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            g_max_qps = atoi(argv[++i]);
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            g_csv_file = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc)
            g_direct_addr = argv[++i];
        else if (strcmp(argv[i], "--provider-dist") == 0)
            g_provider_dist = true;
    }
}

int main(int argc, char** argv) {
    // Parse test-specific args first (before -i which is consumed by Init)
    std::vector<char*> mprpc_args;
    mprpc_args.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            mprpc_args.push_back(argv[i]);
            mprpc_args.push_back(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "-d") == 0 ||
                   strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "-m") == 0 ||
                   strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "-t") == 0 ||
                   strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "-p") == 0) {
            i++; // skip value
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "-b") == 0 ||
                   strcmp(argv[i], "--provider-dist") == 0) {
            // skip
        }
    }

    parse_args(argc, argv);
    MprpcApplication::Init(mprpc_args.size(), mprpc_args.data());

    // Apply config overrides from command line
    MprpcConfig& cfg = MprpcApplication::GetConfig();
    if (g_algo >= 0) {
        cfg.SetConfig("ratelimitalgorithm", std::to_string(g_algo));
        cfg.SetConfig("ratelimitenableconsumer", "true");
    }
    if (g_max_qps > 0) {
        cfg.SetConfig("ratelimitmaxqps", std::to_string(g_max_qps));
        cfg.SetConfig("ratelimitenableconsumer", "true");
    }

    // Auto-detect concurrency max
    int hw = std::thread::hardware_concurrency();
    if (g_concurrency.back() > hw * 2) {
        g_concurrency.push_back(hw * 2);
    }

    if (!g_quiet) {
        print_header();
    }

    std::vector<RoundResult> rounds;

    // ── Provider distribution mode: benchmark each provider separately ──
    if (g_provider_dist) {
        std::vector<std::string> providers = {"127.0.0.1:8001", "127.0.0.1:8002", "127.0.0.1:8003"};
        std::vector<std::string> labels = {"Provider-1", "Provider-2", "Provider-3"};

        print_separator();
        std::cout << "  Per-Provider Benchmark (direct connect, concurrency=" << g_concurrency.back() << ")" << std::endl;
        print_separator('-');
        std::cout << std::left
                  << std::setw(14) << "Provider"
                  << std::setw(12) << "QPS"
                  << std::setw(10) << "Succ%"
                  << std::setw(10) << "Avg(ms)"
                  << std::setw(10) << "P99(ms)"
                  << std::setw(10) << "Total"
                  << std::endl;
        print_separator('-');

        for (size_t i = 0; i < providers.size(); i++) {
            g_direct_addr = providers[i];
            int c = g_concurrency.back();
            RoundResult round;
            round.concurrency = c;

            std::vector<std::thread> threads;
            std::vector<ThreadResult> results(c);
            auto test_start = std::chrono::steady_clock::now();
            for (int t = 0; t < c; t++) {
                threads.emplace_back([t, &results]() {
                    results[t] = worker(t, g_warmup, g_duration_sec, g_method, g_direct_addr);
                });
            }
            for (auto& th : threads) th.join();
            auto test_end = std::chrono::steady_clock::now();
            round.duration_sec = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     test_end - test_start).count() / 1000.0;
            for (auto& r : results) {
                round.total_calls += r.calls;
                round.total_errors += r.errors;
                round.total_blocked += r.blocked;
                round.all_us.insert(round.all_us.end(),
                                    r.latencies_us.begin(), r.latencies_us.end());
            }
            std::sort(round.all_us.begin(), round.all_us.end());
            Stats s = compute(round);

            std::cout << std::left
                      << std::setw(14) << labels[i]
                      << std::setw(12) << std::fixed << std::setprecision(0) << s.qps
                      << std::setw(10) << std::fixed << std::setprecision(1) << s.success_rate
                      << std::setw(10) << std::fixed << std::setprecision(2) << s.avg_ms
                      << std::setw(10) << std::fixed << std::setprecision(2) << s.p99_ms
                      << std::setw(10) << round.total_calls
                      << std::endl;
        }

        print_separator('-');
        g_direct_addr = "";
        std::cout << std::endl;

        // Now run ZK-balanced test
        std::cout << "  ZK Load Balanced (all providers, concurrency=" << g_concurrency.back() << ")" << std::endl;
        print_separator('-');
    }

    for (size_t ci = 0; ci < g_concurrency.size(); ci++) {
        int c = g_concurrency[ci];
        if (ci == 0 && !g_quiet) print_table_header(g_concurrency.size() > 1);
        run_single_round(c, rounds);
    }

    if (g_concurrency.size() > 1) {
        print_footer(rounds);
    }

    // Write CSV if requested
    if (!g_csv_file.empty()) {
        std::ofstream csv(g_csv_file);
        csv << "concurrency,qps,success_rate,blocked_pct,avg_ms,min_ms,max_ms,p50_ms,p95_ms,p99_ms,total_calls,total_blocked\n";
        for (auto& r : rounds) {
            Stats s = compute(r);
            double blk_pct = r.total_calls > 0 ? 100.0 * r.total_blocked / r.total_calls : 0;
            csv << r.concurrency << ","
                << s.qps << ","
                << s.success_rate << ","
                << blk_pct << ","
                << s.avg_ms << ","
                << s.min_ms << ","
                << s.max_ms << ","
                << s.p50_ms << ","
                << s.p95_ms << ","
                << s.p99_ms << ","
                << r.total_calls << ","
                << r.total_blocked << "\n";
        }
        csv.close();
        if (!g_quiet)
            std::cout << "\n  Results written to " << g_csv_file << std::endl;
    }

    // Logger background thread blocked on condition variable at exit.
    // _exit() bypasses static destructor ordering issues.
    _exit(0);
}
