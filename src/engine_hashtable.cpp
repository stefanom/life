#include "engine.h"

class HashtableEngine : public SimulationEngine {
public:
    void tick(CellSet& cells) override {
        neighbor_count_buffer_.clear();

        for (const auto& cell : cells) {
            if (GameOfLife::would_overflow(cell.x, cell.y)) {
                continue;
            }

            ++neighbor_count_buffer_[{cell.x - 1, cell.y - 1}];
            ++neighbor_count_buffer_[{cell.x,     cell.y - 1}];
            ++neighbor_count_buffer_[{cell.x + 1, cell.y - 1}];
            ++neighbor_count_buffer_[{cell.x - 1, cell.y}];
            ++neighbor_count_buffer_[{cell.x + 1, cell.y}];
            ++neighbor_count_buffer_[{cell.x - 1, cell.y + 1}];
            ++neighbor_count_buffer_[{cell.x,     cell.y + 1}];
            ++neighbor_count_buffer_[{cell.x + 1, cell.y + 1}];
        }

        new_cells_buffer_.clear();
        new_cells_buffer_.reserve(cells.size());

        for (const auto& [cell, count] : neighbor_count_buffer_) {
            if (count == 3 || (count == 2 && cells.find(cell) != cells.end())) {
                new_cells_buffer_.insert(cell);
            }
        }

        std::swap(cells, new_cells_buffer_);
    }

    [[nodiscard]] std::unique_ptr<SimulationEngine> clone() const override {
        return std::make_unique<HashtableEngine>();
    }

    [[nodiscard]] EngineType type() const noexcept override {
        return EngineType::Hashtable;
    }

private:
    CellCountMap neighbor_count_buffer_;
    CellSet new_cells_buffer_;
};

std::unique_ptr<SimulationEngine> create_hashtable_engine() {
    return std::make_unique<HashtableEngine>();
}
