#ifndef ENGINE_H
#define ENGINE_H

#include "game_of_life.h"
#include <memory>
#include <string>
#include <string_view>

enum class EngineType {
    Hashtable,
    Sorted,
    Hashlife
};

/**
 * Abstract base class for Game of Life simulation engines.
 * Each engine implements a different algorithm for computing the next generation.
 * The tick() method reads from and writes to the provided CellSet in place.
 */
class SimulationEngine {
public:
    virtual ~SimulationEngine() = default;

    /** Advance the cell set by one generation. */
    virtual void tick(CellSet& cells) = 0;

    /** Create a deep copy of this engine (for GameOfLife copy semantics). */
    [[nodiscard]] virtual std::unique_ptr<SimulationEngine> clone() const = 0;

    /** Return the engine type. */
    [[nodiscard]] virtual EngineType type() const noexcept = 0;
};

/**
 * Factory: create a SimulationEngine of the given type.
 */
[[nodiscard]] std::unique_ptr<SimulationEngine> create_engine(EngineType type);

/**
 * Parse a string into an EngineType.
 * Accepts "hashtable", "sorted", "hashlife" (case-insensitive).
 * @throws std::invalid_argument on unrecognized string
 */
[[nodiscard]] EngineType parse_engine_type(std::string_view s);

#endif // ENGINE_H
