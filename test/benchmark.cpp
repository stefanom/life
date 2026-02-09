// Standalone benchmark for Game of Life tick performance
// Compile: g++ -std=c++17 -O2 -I include -o benchmark benchmark.cpp src/game_of_life.cpp

#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include "game_of_life.h"

struct BenchmarkResult {
    std::string name;
    size_t cells;
    int iterations;
    double total_ms;
    double per_tick_us;
    double ticks_per_sec;
};

BenchmarkResult run_benchmark(const std::string& name, const CellSet& initial_cells,
                               int iterations) {
    GameOfLife game(initial_cells);

    // Warmup
    GameOfLife warmup(initial_cells);
    for (int i = 0; i < 5; i++) warmup.tick();

    // Actual benchmark
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; i++) {
        game.tick();
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() / 1000.0;
    double per_tick_us = (total_ms * 1000.0) / iterations;
    double ticks_per_sec = iterations / (total_ms / 1000.0);

    return {name, initial_cells.size(), iterations, total_ms, per_tick_us, ticks_per_sec};
}

void print_result(const BenchmarkResult& r) {
    std::cout << "  " << r.name << ":\n";
    std::cout << "    Cells: " << r.cells << ", Iterations: " << r.iterations << "\n";
    std::cout << "    Total: " << r.total_ms << " ms\n";
    std::cout << "    Per tick: " << r.per_tick_us << " µs\n";
    std::cout << "    Speed: " << static_cast<int>(r.ticks_per_sec) << " ticks/sec\n";
    std::cout << "\n";
}

CellSet generate_random_soup(int64_t size, int64_t seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int64_t> dist(-size/2, size/2);

    CellSet cells;
    // Generate approximately size*size*0.3 cells (30% density)
    int target = static_cast<int>(size * size * 0.3);
    for (int i = 0; i < target; i++) {
        cells.insert({dist(rng), dist(rng)});
    }
    return cells;
}

CellSet generate_acorn() {
    // Acorn methuselah - runs for 5206 generations
    return {{0, 0}, {1, 0}, {1, 2}, {3, 1}, {4, 0}, {5, 0}, {6, 0}};
}

CellSet generate_r_pentomino() {
    // R-pentomino - runs for 1103 generations
    return {{1, 0}, {2, 0}, {0, 1}, {1, 1}, {1, 2}};
}

int main() {
    std::cout << "=== Game of Life Performance Benchmark ===\n\n";

    std::vector<BenchmarkResult> results;

    // Small pattern - R-pentomino
    std::cout << "Benchmark 1: R-pentomino (5 cells, expands)\n";
    auto r_pent = generate_r_pentomino();
    results.push_back(run_benchmark("R-pentomino 100 ticks", r_pent, 100));
    print_result(results.back());

    // Medium pattern - Acorn
    std::cout << "Benchmark 2: Acorn (7 cells, long-running)\n";
    auto acorn = generate_acorn();
    results.push_back(run_benchmark("Acorn 200 ticks", acorn, 200));
    print_result(results.back());

    // Random soup - small
    std::cout << "Benchmark 3: Random soup 50x50 (~750 cells)\n";
    auto soup_small = generate_random_soup(50, 12345);
    results.push_back(run_benchmark("Soup 50x50, 100 ticks", soup_small, 100));
    print_result(results.back());

    // Random soup - medium
    std::cout << "Benchmark 4: Random soup 100x100 (~3000 cells)\n";
    auto soup_medium = generate_random_soup(100, 12345);
    results.push_back(run_benchmark("Soup 100x100, 50 ticks", soup_medium, 50));
    print_result(results.back());

    // Random soup - large
    std::cout << "Benchmark 5: Random soup 200x200 (~12000 cells)\n";
    auto soup_large = generate_random_soup(200, 12345);
    results.push_back(run_benchmark("Soup 200x200, 20 ticks", soup_large, 20));
    print_result(results.back());

    // Summary
    std::cout << "=== Summary ===\n";
    std::cout << "| Benchmark | Cells | Ticks | Total (ms) | Per Tick (µs) | Ticks/sec |\n";
    std::cout << "|-----------|-------|-------|------------|---------------|----------|\n";
    for (const auto& r : results) {
        std::cout << "| " << r.name << " | " << r.cells << " | " << r.iterations
                  << " | " << r.total_ms << " | " << r.per_tick_us
                  << " | " << static_cast<int>(r.ticks_per_sec) << " |\n";
    }

    return 0;
}
