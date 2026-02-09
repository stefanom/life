#ifndef GAME_OF_LIFE_H
#define GAME_OF_LIFE_H

#include <array>
#include <cstdint>
#include <limits>
#include <istream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

// Use ankerl::unordered_dense for faster hash tables
// Falls back to std::unordered_set/map if not available
#if __has_include("unordered_dense.h")
#include "unordered_dense.h"
#define USE_FAST_HASH 1
#else
#include <unordered_set>
#include <unordered_map>
#define USE_FAST_HASH 0
#endif

/**
 * Represents a cell coordinate in the Game of Life grid.
 * Coordinates can be any value in the int64_t range.
 */
struct Cell {
    int64_t x;
    int64_t y;

    bool operator==(const Cell& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

/**
 * Hash function for Cell coordinates.
 * Uses a golden ratio derived multiplier for good distribution.
 */
struct CellHash {
    static constexpr uint64_t kHashMultiplier = 0x9e3779b97f4a7c15ULL;

    using is_avalanching = void;  // Hint for ankerl::unordered_dense

    size_t operator()(const Cell& cell) const noexcept {
        // Combine x and y using a high-quality mixing function
        uint64_t h = static_cast<uint64_t>(cell.x);
        h ^= static_cast<uint64_t>(cell.y) * kHashMultiplier;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return static_cast<size_t>(h);
    }
};

#if USE_FAST_HASH
using CellSet = ankerl::unordered_dense::set<Cell, CellHash>;
using CellCountMap = ankerl::unordered_dense::map<Cell, int, CellHash>;
#else
using CellSet = std::unordered_set<Cell, CellHash>;
using CellCountMap = std::unordered_map<Cell, int, CellHash>;
#endif

/**
 * Check if filename has valid Life 1.06 extension (.life or .lif).
 * @param filename The filename to check
 * @return true if extension is valid
 */
inline bool has_valid_life_extension(std::string_view filename) noexcept {
    size_t dot_pos = filename.rfind('.');
    if (dot_pos == std::string_view::npos) return false;
    std::string_view ext = filename.substr(dot_pos);
    return ext == ".life" || ext == ".lif";
}

// Forward declaration
class SimulationEngine;
enum class EngineType;

/**
 * Conway's Game of Life simulation.
 *
 * Supports coordinates in the full int64_t range using sparse storage.
 * Cells at INT64_MIN/MAX boundaries are skipped during simulation to avoid overflow.
 *
 * Delegates simulation to a pluggable SimulationEngine (hashtable, sorted, hashlife).
 * Default engine is Hashtable, preserving all existing behavior.
 *
 * Thread safety: Not thread-safe. External synchronization required for concurrent access.
 *
 * Exception safety:
 * - parse(): Strong guarantee (throws on invalid input, no state change)
 * - tick()/run(): Basic guarantee (state may change before exception)
 * - write()/format(): Strong guarantee (output failure doesn't affect state)
 */
class GameOfLife {
public:
    GameOfLife();
    explicit GameOfLife(const CellSet& cells);
    explicit GameOfLife(CellSet&& cells) noexcept;
    GameOfLife(const CellSet& cells, EngineType engine);
    GameOfLife(CellSet&& cells, EngineType engine);

    ~GameOfLife();

    // Copy support (uses engine->clone())
    GameOfLife(const GameOfLife& other);
    GameOfLife& operator=(const GameOfLife& other);

    // Move support
    GameOfLife(GameOfLife&& other) noexcept;
    GameOfLife& operator=(GameOfLife&& other) noexcept;

    /**
     * Parse Life 1.06 format from a string.
     * @param input String containing Life 1.06 formatted data
     * @param engine Engine type to use (default: Hashtable)
     * @return GameOfLife instance with parsed cells
     * @throws std::runtime_error on invalid format
     */
    [[nodiscard]] static GameOfLife parse(const std::string& input);
    [[nodiscard]] static GameOfLife parse(const std::string& input, EngineType engine);

    /**
     * Parse Life 1.06 format from an input stream.
     * @param input Stream containing Life 1.06 formatted data
     * @param engine Engine type to use (default: Hashtable)
     * @return GameOfLife instance with parsed cells
     * @throws std::runtime_error on invalid format
     */
    [[nodiscard]] static GameOfLife parse(std::istream& input);
    [[nodiscard]] static GameOfLife parse(std::istream& input, EngineType engine);

    /**
     * Check if neighbor computation would overflow for this cell.
     * Cells at INT64_MIN or INT64_MAX in either dimension would overflow
     * when computing neighbor coordinates.
     */
    static bool would_overflow(int64_t x, int64_t y) noexcept {
        constexpr int64_t min_val = std::numeric_limits<int64_t>::min();
        constexpr int64_t max_val = std::numeric_limits<int64_t>::max();
        return x == min_val || x == max_val || y == min_val || y == max_val;
    }

    /**
     * Get the 8 neighboring cell coordinates.
     * @warning Caller must ensure would_overflow(x, y) returns false
     */
    static std::array<Cell, 8> get_neighbors(int64_t x, int64_t y) noexcept {
        return {{
            {x-1, y-1}, {x, y-1}, {x+1, y-1},
            {x-1, y},            {x+1, y},
            {x-1, y+1}, {x, y+1}, {x+1, y+1}
        }};
    }

    /**
     * Run one generation of the simulation.
     * Cells at int64_t boundaries are skipped to avoid overflow.
     */
    void tick();

    /**
     * Run multiple generations.
     * @param iterations Number of generations to run (must be >= 0)
     * @throws std::invalid_argument if iterations < 0
     */
    void run(int iterations);

    /**
     * Write current state to output stream in Life 1.06 format.
     * @param out Output stream
     * @param sorted If true, sort cells by (x, y) for deterministic output
     */
    void write(std::ostream& out, bool sorted = false) const;

    /**
     * Format current state as Life 1.06 string.
     * @return String in Life 1.06 format (unsorted)
     */
    std::string format() const;

    /** Get read-only access to live cells */
    const CellSet& cells() const noexcept { return live_cells_; }

    /** Get count of live cells */
    size_t count() const noexcept { return live_cells_.size(); }

private:
    CellSet live_cells_;
    std::unique_ptr<SimulationEngine> engine_;

    static CellSet parse_cells(std::istream& input);
};

#endif // GAME_OF_LIFE_H
