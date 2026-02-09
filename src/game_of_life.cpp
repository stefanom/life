#include "game_of_life.h"
#include "engine.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

// --- Constructors ---

GameOfLife::GameOfLife()
    : engine_(create_engine(EngineType::Hashtable)) {}

GameOfLife::GameOfLife(const CellSet& cells)
    : live_cells_(cells), engine_(create_engine(EngineType::Hashtable)) {}

GameOfLife::GameOfLife(CellSet&& cells) noexcept
    : live_cells_(std::move(cells)), engine_(create_engine(EngineType::Hashtable)) {}

GameOfLife::GameOfLife(const CellSet& cells, EngineType engine)
    : live_cells_(cells), engine_(create_engine(engine)) {}

GameOfLife::GameOfLife(CellSet&& cells, EngineType engine)
    : live_cells_(std::move(cells)), engine_(create_engine(engine)) {}

GameOfLife::~GameOfLife() = default;

// --- Copy ---

GameOfLife::GameOfLife(const GameOfLife& other)
    : live_cells_(other.live_cells_),
      engine_(other.engine_ ? other.engine_->clone() : create_engine(EngineType::Hashtable)) {}

GameOfLife& GameOfLife::operator=(const GameOfLife& other) {
    if (this != &other) {
        live_cells_ = other.live_cells_;
        engine_ = other.engine_ ? other.engine_->clone() : create_engine(EngineType::Hashtable);
    }
    return *this;
}

// --- Move ---

GameOfLife::GameOfLife(GameOfLife&& other) noexcept
    : live_cells_(std::move(other.live_cells_)),
      engine_(std::move(other.engine_)) {}

GameOfLife& GameOfLife::operator=(GameOfLife&& other) noexcept {
    if (this != &other) {
        live_cells_ = std::move(other.live_cells_);
        engine_ = std::move(other.engine_);
    }
    return *this;
}

// --- Parsing ---

CellSet GameOfLife::parse_cells(std::istream& input) {
    CellSet cells;
    std::string line;
    bool header_found = false;

    while (std::getline(input, line)) {
        // Trim trailing whitespace in-place
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end == std::string::npos) {
            continue; // Empty line
        }
        line.resize(end + 1);

        // First non-empty line must be the header
        if (!header_found) {
            if (line != "#Life 1.06") {
                throw std::runtime_error(
                    "Invalid Life 1.06 file: missing or invalid header (expected '#Life 1.06')");
            }
            header_found = true;
            continue;
        }

        // Parse "x y" coordinate pair with std::from_chars (no heap allocation)
        const char* ptr = line.data();
        const char* const line_end = ptr + line.size();

        // Skip leading whitespace
        while (ptr < line_end && (*ptr == ' ' || *ptr == '\t')) ++ptr;

        int64_t x, y;
        auto [p1, ec1] = std::from_chars(ptr, line_end, x);
        if (ec1 != std::errc{}) {
            throw std::runtime_error(
                "Invalid Life 1.06 file: malformed coordinate line '" + line + "'");
        }

        // Skip whitespace between x and y
        ptr = p1;
        while (ptr < line_end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
        if (ptr == p1) {
            // No whitespace separator found
            throw std::runtime_error(
                "Invalid Life 1.06 file: malformed coordinate line '" + line + "'");
        }

        auto [p2, ec2] = std::from_chars(ptr, line_end, y);
        if (ec2 != std::errc{}) {
            throw std::runtime_error(
                "Invalid Life 1.06 file: malformed coordinate line '" + line + "'");
        }

        // Check for trailing garbage (skip whitespace, then must be at end)
        ptr = p2;
        while (ptr < line_end && (*ptr == ' ' || *ptr == '\t')) ++ptr;
        if (ptr != line_end) {
            throw std::runtime_error(
                "Invalid Life 1.06 file: unexpected content after coordinates '" + line + "'");
        }

        cells.insert({x, y});
    }

    if (!header_found) {
        throw std::runtime_error("Invalid Life 1.06 file: empty or missing header");
    }

    return cells;
}

GameOfLife GameOfLife::parse(const std::string& input) {
    std::istringstream stream(input);
    return parse(stream);
}

GameOfLife GameOfLife::parse(const std::string& input, EngineType engine) {
    std::istringstream stream(input);
    return parse(stream, engine);
}

GameOfLife GameOfLife::parse(std::istream& input) {
    CellSet cells = parse_cells(input);
    return GameOfLife(std::move(cells));
}

GameOfLife GameOfLife::parse(std::istream& input, EngineType engine) {
    CellSet cells = parse_cells(input);
    return GameOfLife(std::move(cells), engine);
}

// --- Simulation ---

void GameOfLife::tick() {
    engine_->tick(live_cells_);
}

void GameOfLife::run(int iterations) {
    if (iterations < 0) {
        throw std::invalid_argument("Iterations must be non-negative");
    }
    for (int i = 0; i < iterations; i++) {
        tick();
    }
}

// --- Output ---

void GameOfLife::write(std::ostream& out, bool sorted) const {
    // Use std::to_chars into a buffer for fast integer formatting,
    // then flush with a single write() call per cell.
    // This avoids iostream's locale handling and virtual dispatch overhead.
    constexpr size_t kBufSize = 8192;
    char buf[kBufSize];
    char* pos = buf;
    char* const end = buf + kBufSize;

    // Reserve space for the longest possible line: two int64_t + space + newline
    // max int64_t is 20 digits, so worst case is 20 + 1 + 20 + 1 = 42 bytes
    constexpr size_t kMaxLineLen = 44;

    auto flush = [&]() {
        out.write(buf, pos - buf);
        pos = buf;
    };

    auto write_cell = [&](const Cell& cell) {
        if (static_cast<size_t>(end - pos) < kMaxLineLen) {
            flush();
        }
        auto [p1, ec1] = std::to_chars(pos, end, cell.x);
        *p1++ = ' ';
        auto [p2, ec2] = std::to_chars(p1, end, cell.y);
        *p2++ = '\n';
        pos = p2;
    };

    // Write header
    constexpr char kHeader[] = "#Life 1.06\n";
    constexpr size_t kHeaderLen = sizeof(kHeader) - 1;
    std::memcpy(pos, kHeader, kHeaderLen);
    pos += kHeaderLen;

    if (sorted) {
        std::vector<Cell> sorted_cells;
        sorted_cells.reserve(live_cells_.size());
        sorted_cells.assign(live_cells_.begin(), live_cells_.end());
        std::sort(sorted_cells.begin(), sorted_cells.end(),
            [](const Cell& a, const Cell& b) {
                if (a.x != b.x) return a.x < b.x;
                return a.y < b.y;
            });
        for (const auto& cell : sorted_cells) {
            write_cell(cell);
        }
    } else {
        for (const auto& cell : live_cells_) {
            write_cell(cell);
        }
    }

    if (pos > buf) {
        flush();
    }
}

std::string GameOfLife::format() const {
    std::ostringstream out;
    write(out);
    return out.str();
}
