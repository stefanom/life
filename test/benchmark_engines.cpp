// Comparative benchmark for all simulation engines
// Compile: make benchmark-engines

#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>
#include "game_of_life.h"
#include "engine.h"

struct BenchmarkResult {
    std::string engine_name;
    std::string pattern_name;
    size_t cells;
    int iterations;
    double total_ms;
    double per_tick_us;
};

struct PatternSpec {
    std::string name;
    CellSet cells;
    int ticks;
};

const char* engine_name(EngineType t) {
    switch (t) {
        case EngineType::Hashtable: return "hashtable";
        case EngineType::Sorted:    return "sorted";
        case EngineType::Hashlife:  return "hashlife";
    }
    return "unknown";
}

CellSet generate_random_soup(int64_t size, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int64_t> dist(-size/2, size/2);

    CellSet cells;
    int target = static_cast<int>(size * size * 0.3);
    for (int i = 0; i < target; i++) {
        cells.insert({dist(rng), dist(rng)});
    }
    return cells;
}

CellSet generate_block_grid(int side) {
    CellSet cells;
    for (int bx = 0; bx < side; bx++) {
        for (int by = 0; by < side; by++) {
            int64_t x = bx * 4;
            int64_t y = by * 4;
            cells.insert({x, y});
            cells.insert({x + 1, y});
            cells.insert({x, y + 1});
            cells.insert({x + 1, y + 1});
        }
    }
    return cells;
}

CellSet generate_gliders(int count) {
    CellSet cells;
    // Place gliders spaced apart
    for (int i = 0; i < count; i++) {
        int64_t ox = i * 20;
        int64_t oy = i * 20;
        cells.insert({ox + 0, oy + 1});
        cells.insert({ox + 1, oy + 2});
        cells.insert({ox + 2, oy + 0});
        cells.insert({ox + 2, oy + 1});
        cells.insert({ox + 2, oy + 2});
    }
    return cells;
}

BenchmarkResult run_engine_benchmark(EngineType engine, const std::string& pattern_name,
                                      const CellSet& initial_cells, int ticks) {
    GameOfLife game(initial_cells, engine);

    // Warmup: 3 ticks
    GameOfLife warmup(initial_cells, engine);
    for (int i = 0; i < 3; i++) warmup.tick();

    // Timed run
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ticks; i++) {
        game.tick();
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
    double per_tick_us = (total_ms * 1000.0) / ticks;

    return {engine_name(engine), pattern_name, initial_cells.size(), ticks, total_ms, per_tick_us};
}

bool verify_correctness(const std::string& pattern_name, const CellSet& initial_cells, int ticks) {
    // Run all three engines and verify they produce identical results
    std::vector<EngineType> engines = {EngineType::Hashtable, EngineType::Sorted, EngineType::Hashlife};

    // Collect results
    std::vector<CellSet> results;
    for (auto engine : engines) {
        GameOfLife game(initial_cells, engine);
        for (int i = 0; i < ticks; i++) {
            game.tick();
        }
        results.push_back(game.cells());
    }

    // Compare all against hashtable (reference)
    bool all_match = true;
    for (size_t i = 1; i < results.size(); i++) {
        if (results[i] != results[0]) {
            std::cerr << "  MISMATCH: " << pattern_name << " - "
                      << engine_name(engines[i]) << " differs from hashtable after "
                      << ticks << " ticks (hashtable: " << results[0].size()
                      << " cells, " << engine_name(engines[i]) << ": "
                      << results[i].size() << " cells)\n";
            all_match = false;
        }
    }
    return all_match;
}

int main() {
    std::cout << "=== Comparative Engine Benchmark ===\n\n";

    // Define patterns
    std::vector<PatternSpec> patterns;

    // R-pentomino
    patterns.push_back({"R-pentomino", {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}}, 200});

    // Acorn
    patterns.push_back({"Acorn", {{0, 0}, {1, 0}, {1, 2}, {3, 1}, {4, 0}, {5, 0}, {6, 0}}, 500});

    // 10 gliders
    patterns.push_back({"10 gliders", generate_gliders(10), 200});

    // Soup 50x50
    patterns.push_back({"Soup 50x50", generate_random_soup(50, 12345), 100});

    // Soup 100x100
    patterns.push_back({"Soup 100x100", generate_random_soup(100, 12345), 50});

    // Soup 200x200
    patterns.push_back({"Soup 200x200", generate_random_soup(200, 12345), 20});

    // Block grid 50x50
    patterns.push_back({"Block grid 50x50", generate_block_grid(50), 100});

    // === Phase 1: Correctness Check ===
    std::cout << "--- Correctness Check (10 ticks per pattern) ---\n";
    bool all_correct = true;
    for (const auto& p : patterns) {
        bool ok = verify_correctness(p.name, p.cells, 10);
        std::cout << "  " << (ok ? "PASS" : "FAIL") << ": " << p.name
                  << " (" << p.cells.size() << " cells)\n";
        if (!ok) all_correct = false;
    }

    if (!all_correct) {
        std::cerr << "\nCORRECTNESS CHECK FAILED - benchmark results may be unreliable\n\n";
    } else {
        std::cout << "\nAll engines produce identical results.\n\n";
    }

    // === Phase 2: Timed Benchmarks ===
    std::cout << "--- Performance Benchmarks ---\n\n";

    std::vector<EngineType> engines = {EngineType::Hashtable, EngineType::Sorted, EngineType::Hashlife};
    std::vector<BenchmarkResult> results;

    for (const auto& p : patterns) {
        std::cout << p.name << " (" << p.cells.size() << " cells, " << p.ticks << " ticks):\n";
        for (auto engine : engines) {
            auto r = run_engine_benchmark(engine, p.name, p.cells, p.ticks);
            results.push_back(r);
            std::cout << "  " << std::setw(12) << std::left << r.engine_name
                      << std::setw(10) << std::right << std::fixed << std::setprecision(1)
                      << r.total_ms << " ms  ("
                      << std::setw(8) << std::setprecision(1) << r.per_tick_us << " us/tick)\n";
        }
        std::cout << "\n";
    }

    // === Summary Table ===
    std::cout << "=== Summary ===\n";
    std::cout << std::setw(20) << std::left << "Pattern"
              << std::setw(8) << std::right << "Cells"
              << std::setw(7) << "Ticks"
              << std::setw(14) << "hashtable"
              << std::setw(14) << "sorted"
              << std::setw(14) << "hashlife" << "\n";
    std::cout << std::string(77, '-') << "\n";

    for (const auto& p : patterns) {
        std::cout << std::setw(20) << std::left << p.name
                  << std::setw(8) << std::right << p.cells.size()
                  << std::setw(7) << p.ticks;

        for (auto engine : engines) {
            for (const auto& r : results) {
                if (r.pattern_name == p.name && r.engine_name == engine_name(engine)) {
                    std::cout << std::setw(11) << std::fixed << std::setprecision(1)
                              << r.total_ms << " ms";
                    break;
                }
            }
        }
        std::cout << "\n";
    }

    std::cout << "\n=== Benchmark Complete ===\n";
    return all_correct ? 0 : 1;
}
