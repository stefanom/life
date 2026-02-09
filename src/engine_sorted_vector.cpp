#include "engine.h"
#include <algorithm>
#include <vector>

class SortedVectorEngine : public SimulationEngine {
public:
    void tick(CellSet& cells) override {
        // 1. Copy live cells to sorted vector
        sorted_alive_.clear();
        sorted_alive_.reserve(cells.size());
        sorted_alive_.assign(cells.begin(), cells.end());
        std::sort(sorted_alive_.begin(), sorted_alive_.end(), cell_less);

        // 2. Emit 8 neighbor coords per cell into candidates
        candidates_.clear();
        candidates_.reserve(sorted_alive_.size() * 8);
        for (const auto& cell : sorted_alive_) {
            if (GameOfLife::would_overflow(cell.x, cell.y)) {
                continue;
            }
            candidates_.push_back({cell.x - 1, cell.y - 1});
            candidates_.push_back({cell.x,     cell.y - 1});
            candidates_.push_back({cell.x + 1, cell.y - 1});
            candidates_.push_back({cell.x - 1, cell.y});
            candidates_.push_back({cell.x + 1, cell.y});
            candidates_.push_back({cell.x - 1, cell.y + 1});
            candidates_.push_back({cell.x,     cell.y + 1});
            candidates_.push_back({cell.x + 1, cell.y + 1});
        }

        // 3. Sort candidates
        std::sort(candidates_.begin(), candidates_.end(), cell_less);

        // 4. Walk sorted candidates counting runs → neighbor count
        // 5. Apply rules
        cells.clear();
        if (candidates_.empty()) return;

        size_t i = 0;
        while (i < candidates_.size()) {
            Cell current = candidates_[i];
            int count = 1;
            while (i + count < candidates_.size() &&
                   candidates_[i + count].x == current.x &&
                   candidates_[i + count].y == current.y) {
                ++count;
            }

            // count==3 → alive; count==2 → alive if currently alive (binary search)
            if (count == 3) {
                cells.insert(current);
            } else if (count == 2) {
                if (std::binary_search(sorted_alive_.begin(), sorted_alive_.end(),
                                       current, cell_less)) {
                    cells.insert(current);
                }
            }

            i += count;
        }
    }

    [[nodiscard]] std::unique_ptr<SimulationEngine> clone() const override {
        return std::make_unique<SortedVectorEngine>();
    }

    [[nodiscard]] EngineType type() const noexcept override {
        return EngineType::Sorted;
    }

private:
    std::vector<Cell> sorted_alive_;
    std::vector<Cell> candidates_;

    static bool cell_less(const Cell& a, const Cell& b) noexcept {
        if (a.x != b.x) return a.x < b.x;
        return a.y < b.y;
    }
};

std::unique_ptr<SimulationEngine> create_sorted_vector_engine() {
    return std::make_unique<SortedVectorEngine>();
}
