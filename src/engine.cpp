#include "engine.h"
#include <algorithm>
#include <stdexcept>
#include <string>

// Forward declarations of engine constructors
std::unique_ptr<SimulationEngine> create_hashtable_engine();
std::unique_ptr<SimulationEngine> create_sorted_vector_engine();
std::unique_ptr<SimulationEngine> create_hashlife_engine();

std::unique_ptr<SimulationEngine> create_engine(EngineType type) {
    switch (type) {
        case EngineType::Hashtable:
            return create_hashtable_engine();
        case EngineType::Sorted:
            return create_sorted_vector_engine();
        case EngineType::Hashlife:
            return create_hashlife_engine();
    }
    // Unreachable, but satisfy compilers
    return create_hashtable_engine();
}

EngineType parse_engine_type(std::string_view s) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower(s);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "hashtable") return EngineType::Hashtable;
    if (lower == "sorted")    return EngineType::Sorted;
    if (lower == "hashlife")  return EngineType::Hashlife;

    throw std::invalid_argument(
        "Unknown engine type '" + std::string(s) +
        "'. Valid options: hashtable, sorted, hashlife");
}
