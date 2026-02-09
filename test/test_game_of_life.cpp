#include <iostream>
#include <sstream>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <unistd.h>
#include "game_of_life.h"
#include "renderer.h"

namespace fs = std::filesystem;

// Test framework - always-on checks with proper error messages
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        std::cerr << "    FAILED: " << message << "\n"; \
        std::cerr << "      at " << __FILE__ << ":" << __LINE__ << "\n"; \
        return false; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    tests_run++; \
    if (test_func()) { \
        tests_passed++; \
        std::cout << "  PASS: " << #test_func << "\n"; \
    } else { \
        tests_failed++; \
        std::cout << "  FAIL: " << #test_func << "\n"; \
    } \
} while(0)

// ============ Parsing Tests ============

bool test_parse() {
    std::string input = R"(#Life 1.06
0 1
1 2
2 0
2 1
2 2
)";
    GameOfLife game = GameOfLife::parse(input);
    TEST_ASSERT(game.count() == 5, "Expected 5 cells");

    CellSet expected = {{0, 1}, {1, 2}, {2, 0}, {2, 1}, {2, 2}};
    TEST_ASSERT(game.cells() == expected, "Cell positions don't match");
    return true;
}

bool test_parse_stream_directly() {
    std::istringstream stream(R"(#Life 1.06
0 0
1 1
2 2
)");
    GameOfLife game = GameOfLife::parse(stream);
    TEST_ASSERT(game.count() == 3, "Expected 3 cells");

    CellSet expected = {{0, 0}, {1, 1}, {2, 2}};
    TEST_ASSERT(game.cells() == expected, "Cell positions don't match");
    return true;
}

bool test_file_extension() {
    TEST_ASSERT(has_valid_life_extension("test.life"), ".life should be valid");
    TEST_ASSERT(has_valid_life_extension("test.lif"), ".lif should be valid");
    TEST_ASSERT(has_valid_life_extension("/path/to/file.life"), "path with .life should be valid");
    TEST_ASSERT(has_valid_life_extension("my.pattern.life"), "multiple dots with .life should be valid");
    TEST_ASSERT(!has_valid_life_extension("test.txt"), ".txt should be invalid");
    TEST_ASSERT(!has_valid_life_extension("test.life.txt"), ".life.txt should be invalid");
    TEST_ASSERT(!has_valid_life_extension("testlife"), "no extension should be invalid");
    TEST_ASSERT(!has_valid_life_extension("test"), "no extension should be invalid");
    return true;
}

bool test_missing_header() {
    std::string input = R"(0 1
1 2
)";
    bool threw = false;
    try {
        (void)GameOfLife::parse(input);
    } catch (const std::runtime_error& e) {
        threw = true;
        TEST_ASSERT(std::string(e.what()).find("header") != std::string::npos,
                    "Exception should mention 'header'");
    }
    TEST_ASSERT(threw, "Should throw exception for missing header");
    return true;
}

bool test_invalid_header() {
    std::string input = R"(#Life 1.05
0 1
)";
    bool threw = false;
    try {
        (void)GameOfLife::parse(input);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    TEST_ASSERT(threw, "Should throw exception for wrong version");
    return true;
}

bool test_malformed_coordinates() {
    std::string input = R"(#Life 1.06
0 1
invalid line
)";
    bool threw = false;
    try {
        (void)GameOfLife::parse(input);
    } catch (const std::runtime_error& e) {
        threw = true;
        TEST_ASSERT(std::string(e.what()).find("malformed") != std::string::npos,
                    "Exception should mention 'malformed'");
    }
    TEST_ASSERT(threw, "Should throw exception for malformed line");
    return true;
}

bool test_trailing_garbage() {
    std::string input = R"(#Life 1.06
0 1 extra_stuff
)";
    bool threw = false;
    try {
        (void)GameOfLife::parse(input);
    } catch (const std::runtime_error& e) {
        threw = true;
        TEST_ASSERT(std::string(e.what()).find("unexpected") != std::string::npos,
                    "Exception should mention 'unexpected'");
    }
    TEST_ASSERT(threw, "Should throw exception for trailing garbage");
    return true;
}

bool test_empty_with_header() {
    std::string input = "#Life 1.06\n";
    GameOfLife game = GameOfLife::parse(input);
    TEST_ASSERT(game.count() == 0, "Empty file should have 0 cells");
    return true;
}

bool test_parse_large_integers() {
    std::string input = R"(#Life 1.06
-2000000000000 -2000000000000
9223372036854775806 -9223372036854775807
)";
    GameOfLife game = GameOfLife::parse(input);
    TEST_ASSERT(game.count() == 2, "Should have 2 cells");

    bool found_large = false;
    bool found_near_max = false;
    for (const auto& cell : game.cells()) {
        if (cell.x == -2000000000000LL && cell.y == -2000000000000LL) {
            found_large = true;
        }
        if (cell.x == 9223372036854775806LL && cell.y == -9223372036854775807LL) {
            found_near_max = true;
        }
    }
    TEST_ASSERT(found_large, "Large negative coordinate not found");
    TEST_ASSERT(found_near_max, "Near-max coordinate not found");
    return true;
}

// ============ Pattern Tests ============

bool test_blinker() {
    CellSet blinker_h = {{0, 0}, {1, 0}, {2, 0}};
    CellSet blinker_v = {{1, -1}, {1, 0}, {1, 1}};

    GameOfLife game(blinker_h);
    game.tick();
    TEST_ASSERT(game.cells() == blinker_v, "Blinker should rotate to vertical");

    game.tick();
    TEST_ASSERT(game.cells() == blinker_h, "Blinker should rotate back to horizontal");
    return true;
}

bool test_block() {
    CellSet block = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    GameOfLife game(block);
    game.tick();
    TEST_ASSERT(game.cells() == block, "Block should be stable after 1 tick");

    game.run(10);
    TEST_ASSERT(game.cells() == block, "Block should be stable after 11 ticks");
    return true;
}

bool test_glider() {
    CellSet glider = {{0, 1}, {1, 2}, {2, 0}, {2, 1}, {2, 2}};

    GameOfLife game(glider);
    game.run(4);
    CellSet expected = {{1, 2}, {2, 3}, {3, 1}, {3, 2}, {3, 3}};
    TEST_ASSERT(game.cells() == expected, "Glider should move diagonally after 4 ticks");
    return true;
}

// ============ Edge Case Tests ============

bool test_empty() {
    GameOfLife game;
    game.run(10);
    TEST_ASSERT(game.count() == 0, "Empty board should stay empty");
    return true;
}

bool test_single_cell_dies() {
    CellSet single = {{0, 0}};
    GameOfLife game(single);
    game.tick();
    TEST_ASSERT(game.count() == 0, "Single cell should die");
    return true;
}

bool test_two_cells_die() {
    CellSet two = {{0, 0}, {1, 0}};
    GameOfLife game(two);
    game.tick();
    TEST_ASSERT(game.count() == 0, "Two adjacent cells should die");
    return true;
}

bool test_l_shape_to_block() {
    CellSet l_shape = {{0, 0}, {1, 0}, {0, 1}};
    CellSet block = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

    GameOfLife game(l_shape);
    game.tick();
    TEST_ASSERT(game.cells() == block, "L-shape should become block");
    return true;
}

bool test_overcrowding() {
    CellSet crowded = {
        {0, 0}, {1, 0}, {2, 0},
        {0, 1}, {1, 1}, {2, 1},
        {0, 2}, {1, 2}, {2, 2}
    };

    GameOfLife game(crowded);
    game.tick();

    bool center_alive = game.cells().count({1, 1}) > 0;
    TEST_ASSERT(!center_alive, "Center cell should die from overcrowding");
    return true;
}

bool test_negative_iterations() {
    GameOfLife game;
    bool threw = false;
    try {
        game.run(-1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    TEST_ASSERT(threw, "Negative iterations should throw");
    return true;
}

// ============ Large Coordinate Tests ============

bool test_large_coordinates_separate() {
    CellSet cells = {
        {0, 0}, {1, 0}, {2, 0},
        {1000000000000LL, 0}, {1000000000001LL, 0}, {1000000000002LL, 0}
    };

    GameOfLife game(cells);
    game.tick();

    TEST_ASSERT(game.count() == 6, "Should have 6 cells");
    TEST_ASSERT(game.cells().count({1, -1}) > 0, "Near-origin blinker cell missing");
    TEST_ASSERT(game.cells().count({1, 0}) > 0, "Near-origin blinker cell missing");
    TEST_ASSERT(game.cells().count({1, 1}) > 0, "Near-origin blinker cell missing");
    TEST_ASSERT(game.cells().count({1000000000001LL, -1}) > 0, "Far blinker cell missing");
    TEST_ASSERT(game.cells().count({1000000000001LL, 0}) > 0, "Far blinker cell missing");
    TEST_ASSERT(game.cells().count({1000000000001LL, 1}) > 0, "Far blinker cell missing");
    return true;
}

bool test_boundary_cells_no_crash() {
    constexpr int64_t max_val = std::numeric_limits<int64_t>::max();
    constexpr int64_t min_val = std::numeric_limits<int64_t>::min();

    CellSet cells = {
        {max_val, 0},
        {min_val, 0},
        {0, max_val},
        {0, min_val}
    };

    GameOfLife game(cells);
    game.tick();

    TEST_ASSERT(game.count() == 0, "Boundary cells should die (no computable neighbors)");
    return true;
}

bool test_overflow_check() {
    constexpr int64_t max_val = std::numeric_limits<int64_t>::max();
    constexpr int64_t min_val = std::numeric_limits<int64_t>::min();

    TEST_ASSERT(GameOfLife::would_overflow(max_val, 0), "MAX x should overflow");
    TEST_ASSERT(GameOfLife::would_overflow(min_val, 0), "MIN x should overflow");
    TEST_ASSERT(GameOfLife::would_overflow(0, max_val), "MAX y should overflow");
    TEST_ASSERT(GameOfLife::would_overflow(0, min_val), "MIN y should overflow");
    TEST_ASSERT(!GameOfLife::would_overflow(0, 0), "Origin should not overflow");
    TEST_ASSERT(!GameOfLife::would_overflow(max_val - 1, min_val + 1), "One-off limits should not overflow");
    return true;
}

// ============ Hash Quality Tests ============

bool test_hash_collision_grid() {
    // Test hash distribution with a grid pattern (common in Life)
    std::unordered_set<size_t> hashes;
    CellHash hasher;

    // 100x100 grid
    for (int64_t x = 0; x < 100; x++) {
        for (int64_t y = 0; y < 100; y++) {
            hashes.insert(hasher({x, y}));
        }
    }

    // Expect very few collisions (< 1% for a good hash)
    size_t expected_cells = 10000;
    size_t collision_count = expected_cells - hashes.size();
    double collision_rate = static_cast<double>(collision_count) / expected_cells;

    TEST_ASSERT(collision_rate < 0.01, "Hash collision rate should be < 1% for grid pattern");
    return true;
}

bool test_hash_collision_diagonal() {
    // Test hash distribution with a diagonal pattern
    std::unordered_set<size_t> hashes;
    CellHash hasher;

    for (int64_t i = 0; i < 10000; i++) {
        hashes.insert(hasher({i, i}));
        hashes.insert(hasher({i, -i}));
    }

    size_t expected_cells = 20000;
    size_t collision_count = expected_cells - hashes.size();
    double collision_rate = static_cast<double>(collision_count) / expected_cells;

    TEST_ASSERT(collision_rate < 0.01, "Hash collision rate should be < 1% for diagonal pattern");
    return true;
}

bool test_hash_stress() {
    // Comprehensive stress test for hash collisions
    // Tests that the hash function works correctly for large datasets
    // and that collisions don't corrupt the simulation

    CellHash hasher;
    std::mt19937_64 rng(12345);

    // Test 1: Large random coordinate range (stress the full int64_t space)
    {
        std::unordered_set<size_t> hashes;
        std::uniform_int_distribution<int64_t> dist(
            std::numeric_limits<int64_t>::min() + 1,
            std::numeric_limits<int64_t>::max() - 1
        );

        constexpr size_t num_cells = 100000;
        for (size_t i = 0; i < num_cells; i++) {
            hashes.insert(hasher({dist(rng), dist(rng)}));
        }

        size_t collision_count = num_cells - hashes.size();
        double collision_rate = static_cast<double>(collision_count) / num_cells;
        TEST_ASSERT(collision_rate < 0.01,
            "Hash collision rate should be < 1% for random coordinates");
    }

    // Test 2: Clustered coordinates (common in real simulations)
    {
        std::unordered_set<size_t> hashes;
        constexpr size_t num_cells = 50000;

        // Multiple clusters at different locations
        std::vector<std::pair<int64_t, int64_t>> cluster_centers = {
            {0, 0},
            {1000000, 1000000},
            {-999999, 500000},
            {INT64_MAX/2, INT64_MIN/2},
            {-1, -1}
        };

        size_t cells_per_cluster = num_cells / cluster_centers.size();
        std::uniform_int_distribution<int64_t> offset(-500, 500);

        for (const auto& [cx, cy] : cluster_centers) {
            for (size_t i = 0; i < cells_per_cluster; i++) {
                int64_t x = cx + offset(rng);
                int64_t y = cy + offset(rng);
                hashes.insert(hasher({x, y}));
            }
        }

        size_t collision_count = num_cells - hashes.size();
        double collision_rate = static_cast<double>(collision_count) / num_cells;
        TEST_ASSERT(collision_rate < 0.02,
            "Hash collision rate should be < 2% for clustered coordinates");
    }

    // Test 3: Verify no cell loss due to hash collisions
    // This is the critical test - collisions in the hash don't matter
    // as long as the hash table resolves them correctly
    {
        CellSet cells;
        constexpr size_t num_cells = 10000;
        std::uniform_int_distribution<int64_t> dist(-10000, 10000);

        std::vector<Cell> inserted_cells;
        for (size_t i = 0; i < num_cells; i++) {
            Cell c{dist(rng), dist(rng)};
            auto [it, inserted] = cells.insert(c);
            if (inserted) {
                inserted_cells.push_back(c);
            }
        }

        // Verify all inserted cells are retrievable
        for (const auto& c : inserted_cells) {
            TEST_ASSERT(cells.count(c) == 1,
                "All inserted cells must be retrievable from hash set");
        }
    }

    // Test 4: Simulation correctness with large cell set
    // Run simulation and verify no cells are mysteriously lost or duplicated
    {
        CellSet cells;
        // Create a stable pattern repeated many times
        // (2x2 blocks are stable in Game of Life)
        for (int64_t bx = 0; bx < 100; bx++) {
            for (int64_t by = 0; by < 100; by++) {
                int64_t base_x = bx * 10;
                int64_t base_y = by * 10;
                cells.insert({base_x, base_y});
                cells.insert({base_x + 1, base_y});
                cells.insert({base_x, base_y + 1});
                cells.insert({base_x + 1, base_y + 1});
            }
        }

        size_t initial_count = cells.size();
        TEST_ASSERT(initial_count == 40000, "Should have 40000 cells (100x100 blocks x 4 cells)");

        GameOfLife game(cells);
        game.tick();

        // Blocks are stable - count should remain the same
        TEST_ASSERT(game.count() == initial_count,
            "Stable block pattern should maintain cell count after tick");

        // Verify structure is preserved (spot check some blocks)
        TEST_ASSERT(game.cells().count({0, 0}) == 1, "Block cell should survive");
        TEST_ASSERT(game.cells().count({1, 1}) == 1, "Block cell should survive");
        TEST_ASSERT(game.cells().count({990, 990}) == 1, "Far block cell should survive");
        TEST_ASSERT(game.cells().count({991, 991}) == 1, "Far block cell should survive");
    }

    // Test 5: Pathological patterns that might cause hash clustering
    {
        std::unordered_set<size_t> hashes;
        CellHash hasher_local;

        // Pattern: cells along powers of 2 (could stress some hash functions)
        for (int i = 0; i < 62; i++) {
            int64_t val = 1LL << i;
            hashes.insert(hasher_local({val, 0}));
            hashes.insert(hasher_local({0, val}));
            hashes.insert(hasher_local({val, val}));
            hashes.insert(hasher_local({-val, val}));
        }

        // Should have minimal collisions even with pathological input
        size_t expected = 62 * 4;
        size_t collision_count = expected - hashes.size();
        TEST_ASSERT(collision_count < expected / 10,
            "Powers of 2 pattern should have < 10% collisions");
    }

    return true;
}

// ============ Integration Tests ============

bool test_format() {
    CellSet cells = {{2, 1}, {0, 0}, {1, 0}};
    GameOfLife game(cells);

    std::string output = game.format();

    TEST_ASSERT(output.find("#Life 1.06") != std::string::npos, "Output should have header");
    TEST_ASSERT(output.find("0 0") != std::string::npos, "Output should contain 0 0");
    TEST_ASSERT(output.find("1 0") != std::string::npos, "Output should contain 1 0");
    TEST_ASSERT(output.find("2 1") != std::string::npos, "Output should contain 2 1");
    return true;
}

bool test_write_stream() {
    CellSet cells = {{1, 2}, {0, 0}};
    GameOfLife game(cells);

    std::ostringstream out;
    game.write(out);

    std::string output = out.str();
    TEST_ASSERT(output.find("#Life 1.06") != std::string::npos, "Stream output should have header");
    TEST_ASSERT(output.find("0 0") != std::string::npos, "Output should contain 0 0");
    TEST_ASSERT(output.find("1 2") != std::string::npos, "Output should contain 1 2");
    return true;
}

bool test_write_sorted() {
    CellSet cells = {{2, 2}, {0, 0}, {1, 1}};
    GameOfLife game(cells);

    std::ostringstream out;
    game.write(out, true);  // explicitly sorted

    std::string output = out.str();
    TEST_ASSERT(output.find("#Life 1.06") != std::string::npos, "Sorted output should have header");

    // Verify sorted order: 0 0 should come before 1 1 which should come before 2 2
    size_t pos_00 = output.find("0 0");
    size_t pos_11 = output.find("1 1");
    size_t pos_22 = output.find("2 2");
    TEST_ASSERT(pos_00 < pos_11 && pos_11 < pos_22, "Output should be sorted when requested");
    return true;
}

bool test_sample_input() {
    std::string input = R"(#Life 1.06
0 1
1 2
2 0
2 1
2 2
-2000000000000 -2000000000000
-2000000000001 -2000000000001
-2000000000000 -2000000000001
)";

    GameOfLife game = GameOfLife::parse(input);
    game.run(10);

    TEST_ASSERT(game.count() == 9, "Should have 9 cells (5 glider + 4 block)");

    TEST_ASSERT(game.cells().count({-2000000000001LL, -2000000000001LL}) > 0, "Block cell missing");
    TEST_ASSERT(game.cells().count({-2000000000001LL, -2000000000000LL}) > 0, "Block cell missing");
    TEST_ASSERT(game.cells().count({-2000000000000LL, -2000000000001LL}) > 0, "Block cell missing");
    TEST_ASSERT(game.cells().count({-2000000000000LL, -2000000000000LL}) > 0, "Block cell missing");
    return true;
}

bool test_move_constructor() {
    CellSet cells = {{0, 0}, {1, 1}};
    CellSet cells_copy = cells;

    GameOfLife game(std::move(cells));
    TEST_ASSERT(game.count() == 2, "Move constructor should work");
    TEST_ASSERT(game.cells() == cells_copy, "Cells should match");
    return true;
}

bool test_randomized_consistency() {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int64_t> dist(-5, 5);

    CellSet cells;
    for (int i = 0; i < 30; i++) {
        cells.insert({dist(rng), dist(rng)});
    }

    GameOfLife game(cells);

    // Reference tick using a straightforward implementation
    std::unordered_map<Cell, int, CellHash> neighbor_count;
    for (const auto& cell : cells) {
        if (GameOfLife::would_overflow(cell.x, cell.y)) {
            continue;
        }
        const auto neighbors = GameOfLife::get_neighbors(cell.x, cell.y);
        for (const auto& neighbor : neighbors) {
            ++neighbor_count[neighbor];
        }
    }

    CellSet expected;
    for (const auto& [cell, count] : neighbor_count) {
        bool alive = cells.count(cell) > 0;
        if (count == 3 || (count == 2 && alive)) {
            expected.insert(cell);
        }
    }

    game.tick();
    TEST_ASSERT(game.cells() == expected, "Randomized tick should match reference");
    return true;
}

// ============ Renderer Tests ============

bool test_bounding_box_empty() {
    GameOfLife game;
    int64_t min_x, max_x, min_y, max_y;
    bool result = get_bounding_box(game, min_x, max_x, min_y, max_y);
    TEST_ASSERT(!result, "Empty game should return false for bounding box");
    return true;
}

bool test_bounding_box_single_cell() {
    CellSet cells = {{5, 10}};
    GameOfLife game(cells);
    int64_t min_x, max_x, min_y, max_y;
    bool result = get_bounding_box(game, min_x, max_x, min_y, max_y);
    TEST_ASSERT(result, "Single cell should return true");
    TEST_ASSERT(min_x == 5 && max_x == 5, "X bounds should be 5");
    TEST_ASSERT(min_y == 10 && max_y == 10, "Y bounds should be 10");
    return true;
}

bool test_bounding_box_multiple_cells() {
    CellSet cells = {{-10, -20}, {30, 40}, {0, 0}};
    GameOfLife game(cells);
    int64_t min_x, max_x, min_y, max_y;
    bool result = get_bounding_box(game, min_x, max_x, min_y, max_y);
    TEST_ASSERT(result, "Multiple cells should return true");
    TEST_ASSERT(min_x == -10, "min_x should be -10");
    TEST_ASSERT(max_x == 30, "max_x should be 30");
    TEST_ASSERT(min_y == -20, "min_y should be -20");
    TEST_ASSERT(max_y == 40, "max_y should be 40");
    return true;
}

bool test_render_frame_creates_file() {
    // Create a simple pattern
    CellSet cells = {{0, 0}, {1, 0}, {2, 0}};
    GameOfLife game(cells);

    // Use a temp directory for test output
    std::string test_dir = "/tmp/life_test_render_" + std::to_string(getpid());
    std::error_code ec;
    fs::create_directory(test_dir, ec);

    RenderConfig config;
    config.output_dir = test_dir;
    config.cell_size = 4;
    config.padding = 2;

    bool result = render_frame(game, config, 0);
    TEST_ASSERT(result, "render_frame should succeed");

    // Check that file was created
    std::string expected_file = test_dir + "/frame_00000.png";
    TEST_ASSERT(fs::exists(expected_file), "PNG file should exist");

    // Cleanup
    fs::remove_all(test_dir, ec);
    return true;
}

bool test_render_frame_fixed_viewport() {
    CellSet cells = {{0, 0}, {1, 1}};
    GameOfLife game(cells);

    std::string test_dir = "/tmp/life_test_viewport_" + std::to_string(getpid());
    std::error_code ec;
    fs::create_directory(test_dir, ec);

    RenderConfig config;
    config.output_dir = test_dir;
    config.cell_size = 4;

    bool result = render_frame_fixed_viewport(game, config, 0, -5, 5, -5, 5);
    TEST_ASSERT(result, "render_frame_fixed_viewport should succeed");

    std::string expected_file = test_dir + "/frame_00000.png";
    TEST_ASSERT(fs::exists(expected_file), "PNG file should exist");

    // Cleanup
    fs::remove_all(test_dir, ec);
    return true;
}

bool test_render_empty_game() {
    GameOfLife game;

    std::string test_dir = "/tmp/life_test_empty_" + std::to_string(getpid());
    std::error_code ec;
    fs::create_directory(test_dir, ec);

    RenderConfig config;
    config.output_dir = test_dir;

    // render_frame with empty game should render a default small image
    bool result = render_frame(game, config, 0);
    TEST_ASSERT(result, "render_frame should succeed even with empty game");

    // Cleanup
    fs::remove_all(test_dir, ec);
    return true;
}

bool test_render_rejects_huge_viewport() {
    CellSet cells = {{0, 0}};
    GameOfLife game(cells);

    std::string test_dir = "/tmp/life_test_huge_" + std::to_string(getpid());
    std::error_code ec;
    fs::create_directory(test_dir, ec);

    RenderConfig config;
    config.output_dir = test_dir;

    // Try to render a viewport that exceeds max_cells_dimension
    bool result = render_frame_fixed_viewport(game, config, 0,
        0, config.max_cells_dimension + 1, 0, 10);
    TEST_ASSERT(!result, "Should reject viewport exceeding max_cells_dimension");

    // Cleanup
    fs::remove_all(test_dir, ec);
    return true;
}

// ============ Benchmarks ============
// Run with: ./test_game_of_life --benchmark

void run_benchmarks() {
    std::cout << "\n=== Hash Table Performance Benchmark ===\n\n";

    // Create a reproducible random pattern
    std::mt19937_64 rng(99999);
    std::uniform_int_distribution<int64_t> dist(-1000, 1000);

    CellSet base_cells;
    for (int i = 0; i < 5000; i++) {
        base_cells.insert({dist(rng), dist(rng)});
    }
    std::cout << "Test pattern: " << base_cells.size() << " unique cells\n\n";

    constexpr int iterations = 50;

    // Benchmark 1: Default settings (current implementation)
    {
        GameOfLife game(base_cells);
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            game.tick();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Default settings:     " << duration.count() / 1000.0 << " ms for "
                  << iterations << " ticks\n";
        std::cout << "  Final cell count:   " << game.count() << "\n";
    }

    // Benchmark 2: Lower load factor (0.5 instead of 1.0)
    {
        CellSet tuned_cells;
        tuned_cells.max_load_factor(0.5f);
        tuned_cells.reserve(base_cells.size());
        for (const auto& cell : base_cells) {
            tuned_cells.insert(cell);
        }

        GameOfLife game(std::move(tuned_cells));
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            game.tick();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Load factor 0.5:      " << duration.count() / 1000.0 << " ms for "
                  << iterations << " ticks\n";
        std::cout << "  Final cell count:   " << game.count() << "\n";
    }

    // Benchmark 3: Even lower load factor (0.25)
    {
        CellSet tuned_cells;
        tuned_cells.max_load_factor(0.25f);
        tuned_cells.reserve(base_cells.size());
        for (const auto& cell : base_cells) {
            tuned_cells.insert(cell);
        }

        GameOfLife game(std::move(tuned_cells));
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < iterations; i++) {
            game.tick();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        std::cout << "Load factor 0.25:     " << duration.count() / 1000.0 << " ms for "
                  << iterations << " ticks\n";
        std::cout << "  Final cell count:   " << game.count() << "\n";
    }

    // Measure collision rates at different load factors
    // Note: ankerl::unordered_dense is a flat hash map, so bucket_size() is not available
#if !USE_FAST_HASH
    std::cout << "\n--- Collision Analysis ---\n";
    for (float lf : {1.0f, 0.5f, 0.25f}) {
        CellSet cells;
        cells.max_load_factor(lf);
        cells.reserve(base_cells.size());
        for (const auto& cell : base_cells) {
            cells.insert(cell);
        }

        size_t total_chain_length = 0;
        size_t max_chain = 0;
        size_t non_empty_buckets = 0;

        for (size_t i = 0; i < cells.bucket_count(); i++) {
            size_t bucket_size = cells.bucket_size(i);
            if (bucket_size > 0) {
                non_empty_buckets++;
                total_chain_length += bucket_size;
                max_chain = std::max(max_chain, bucket_size);
            }
        }

        double avg_chain = non_empty_buckets > 0
            ? static_cast<double>(total_chain_length) / non_empty_buckets
            : 0.0;

        std::cout << "Load factor " << lf << ":\n";
        std::cout << "  Buckets: " << cells.bucket_count()
                  << ", Non-empty: " << non_empty_buckets << "\n";
        std::cout << "  Avg chain length: " << avg_chain
                  << ", Max chain: " << max_chain << "\n";
    }
#else
    std::cout << "\n--- Hash Map Info ---\n";
    std::cout << "Using ankerl::unordered_dense (flat hash map with robin-hood probing)\n";
    std::cout << "Collision analysis not applicable - no bucket chains\n";
#endif

    std::cout << "\n=== Benchmark Complete ===\n";
}

int main(int argc, char* argv[]) {
    // Check for --benchmark flag
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--benchmark") == 0) {
            run_benchmarks();
            return 0;
        }
    }

    // Run normal tests
    std::cout << "Running Game of Life tests...\n\n";

    std::cout << "Parsing tests:\n";
    RUN_TEST(test_parse);
    RUN_TEST(test_parse_stream_directly);
    RUN_TEST(test_file_extension);
    RUN_TEST(test_missing_header);
    RUN_TEST(test_invalid_header);
    RUN_TEST(test_malformed_coordinates);
    RUN_TEST(test_trailing_garbage);
    RUN_TEST(test_empty_with_header);
    RUN_TEST(test_parse_large_integers);

    std::cout << "\nPattern tests:\n";
    RUN_TEST(test_blinker);
    RUN_TEST(test_block);
    RUN_TEST(test_glider);

    std::cout << "\nEdge case tests:\n";
    RUN_TEST(test_empty);
    RUN_TEST(test_single_cell_dies);
    RUN_TEST(test_two_cells_die);
    RUN_TEST(test_l_shape_to_block);
    RUN_TEST(test_overcrowding);
    RUN_TEST(test_negative_iterations);

    std::cout << "\nBoundary/overflow tests:\n";
    RUN_TEST(test_large_coordinates_separate);
    RUN_TEST(test_boundary_cells_no_crash);
    RUN_TEST(test_overflow_check);

    std::cout << "\nHash quality tests:\n";
    RUN_TEST(test_hash_collision_grid);
    RUN_TEST(test_hash_collision_diagonal);
    RUN_TEST(test_hash_stress);

    std::cout << "\nIntegration tests:\n";
    RUN_TEST(test_format);
    RUN_TEST(test_write_stream);
    RUN_TEST(test_write_sorted);
    RUN_TEST(test_sample_input);
    RUN_TEST(test_move_constructor);
    RUN_TEST(test_randomized_consistency);

    std::cout << "\nRenderer tests:\n";
    RUN_TEST(test_bounding_box_empty);
    RUN_TEST(test_bounding_box_single_cell);
    RUN_TEST(test_bounding_box_multiple_cells);
    RUN_TEST(test_render_frame_creates_file);
    RUN_TEST(test_render_frame_fixed_viewport);
    RUN_TEST(test_render_empty_game);
    RUN_TEST(test_render_rejects_huge_viewport);

    std::cout << "\n";
    std::cout << "================================\n";
    std::cout << "Tests run: " << tests_run << "\n";
    std::cout << "Passed:    " << tests_passed << "\n";
    std::cout << "Failed:    " << tests_failed << "\n";
    std::cout << "================================\n";

    if (tests_failed > 0) {
        std::cout << "SOME TESTS FAILED\n";
        return 1;
    }

    std::cout << "ALL TESTS PASSED\n";
    return 0;
}