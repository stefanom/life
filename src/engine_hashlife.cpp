#include "engine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

// =============================================================================
// HashLife engine: memoized quadtree with hash-consing
//
// Uses spatial clustering to handle widely separated cell groups efficiently.
// Each cluster is stepped independently via a quadtree-based algorithm.
//
// slow_step(): Advance exactly 1 generation using the 9-subquadrant
//   decomposition with center extraction (not recursive stepping) to
//   avoid the standard HashLife 2^(k-2) step doubling.
// =============================================================================

namespace {

struct QuadNode {
    int level;          // 0 = single cell
    int64_t population; // number of alive cells
    QuadNode* nw;
    QuadNode* ne;
    QuadNode* sw;
    QuadNode* se;
    QuadNode* step1_result; // memoized 1-gen advance result
};

struct QuadNodeHash {
    size_t operator()(const QuadNode& n) const noexcept {
        auto h = std::hash<const void*>{};
        size_t seed = h(n.nw);
        seed ^= h(n.ne) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(n.sw) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h(n.se) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct QuadNodeEqual {
    bool operator()(const QuadNode& a, const QuadNode& b) const noexcept {
        return a.nw == b.nw && a.ne == b.ne && a.sw == b.sw && a.se == b.se;
    }
};

class NodePool {
public:
    NodePool() {
        dead_cell_ = alloc_raw(0, 0, nullptr, nullptr, nullptr, nullptr);
        alive_cell_ = alloc_raw(0, 1, nullptr, nullptr, nullptr, nullptr);
    }

    QuadNode* dead_cell() { return dead_cell_; }
    QuadNode* alive_cell() { return alive_cell_; }

    QuadNode* leaf(bool alive) {
        return alive ? alive_cell_ : dead_cell_;
    }

    QuadNode* make(QuadNode* nw, QuadNode* ne, QuadNode* sw, QuadNode* se) {
        assert(nw->level == ne->level && ne->level == sw->level && sw->level == se->level);

        QuadNode key;
        key.nw = nw;
        key.ne = ne;
        key.sw = sw;
        key.se = se;

        auto it = canon_.find(key);
        if (it != canon_.end()) {
            return it->second;
        }

        int level = nw->level + 1;
        int64_t pop = nw->population + ne->population + sw->population + se->population;
        QuadNode* node = alloc_raw(level, pop, nw, ne, sw, se);
        canon_[key] = node;
        return node;
    }

    QuadNode* empty_node(int level) {
        if (level == 0) return dead_cell_;
        if (level < static_cast<int>(empty_cache_.size()) && empty_cache_[level]) {
            return empty_cache_[level];
        }
        if (level >= static_cast<int>(empty_cache_.size())) {
            empty_cache_.resize(level + 1, nullptr);
        }
        QuadNode* sub = empty_node(level - 1);
        QuadNode* node = make(sub, sub, sub, sub);
        empty_cache_[level] = node;
        return node;
    }

    void clear() {
        for (auto* chunk : arena_) {
            delete[] chunk;
        }
        arena_.clear();
        arena_pos_ = 0;
        arena_cap_ = 0;
        current_chunk_ = nullptr;
        canon_.clear();
        empty_cache_.clear();

        dead_cell_ = alloc_raw(0, 0, nullptr, nullptr, nullptr, nullptr);
        alive_cell_ = alloc_raw(0, 1, nullptr, nullptr, nullptr, nullptr);
    }

private:
    QuadNode* alloc_raw(int level, int64_t pop,
                        QuadNode* nw, QuadNode* ne, QuadNode* sw, QuadNode* se) {
        if (arena_pos_ >= arena_cap_) {
            constexpr size_t CHUNK_SIZE = 65536;
            current_chunk_ = new QuadNode[CHUNK_SIZE];
            arena_.push_back(current_chunk_);
            arena_pos_ = 0;
            arena_cap_ = CHUNK_SIZE;
        }
        QuadNode* node = &current_chunk_[arena_pos_++];
        node->level = level;
        node->population = pop;
        node->nw = nw;
        node->ne = ne;
        node->sw = sw;
        node->se = se;
        node->step1_result = nullptr;
        return node;
    }

    QuadNode* dead_cell_ = nullptr;
    QuadNode* alive_cell_ = nullptr;
    std::vector<QuadNode*> arena_;
    QuadNode* current_chunk_ = nullptr;
    size_t arena_pos_ = 0;
    size_t arena_cap_ = 0;
    std::unordered_map<QuadNode, QuadNode*, QuadNodeHash, QuadNodeEqual> canon_;
    std::vector<QuadNode*> empty_cache_;
};

// Sorted cell list for efficient range queries during tree construction
struct SortedCells {
    std::vector<Cell> cells;

    void build(const CellSet& cs) {
        cells.assign(cs.begin(), cs.end());
        std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
    }

    // Check if any cell falls within [x, x+size) x [y, y+size)
    bool has_cell_in(int64_t x, int64_t y, int64_t size) const {
        // Binary search for first cell with cell.x >= x
        auto it = std::lower_bound(cells.begin(), cells.end(), Cell{x, 0},
            [](const Cell& a, const Cell& b) { return a.x < b.x; });

        int64_t x_end = x + size;
        int64_t y_end = y + size;
        while (it != cells.end() && it->x < x_end) {
            if (it->y >= y && it->y < y_end) return true;
            ++it;
        }
        return false;
    }

    bool contains(int64_t x, int64_t y) const {
        auto it = std::lower_bound(cells.begin(), cells.end(), Cell{x, y},
            [](const Cell& a, const Cell& b) {
                return a.x < b.x || (a.x == b.x && a.y < b.y);
            });
        return it != cells.end() && it->x == x && it->y == y;
    }
};

} // anonymous namespace

class HashLifeEngine : public SimulationEngine {
public:
    void tick(CellSet& cells) override {
        pool_.clear();

        if (cells.empty()) return;

        // Cluster cells into groups that are close enough to interact.
        // Cells more than CHUNK_SIZE apart are guaranteed independent.
        constexpr int64_t CHUNK_SIZE = 64;

        struct ChunkKey {
            int64_t cx, cy;
            bool operator==(const ChunkKey& o) const { return cx == o.cx && cy == o.cy; }
        };
        struct ChunkHash {
            size_t operator()(const ChunkKey& k) const noexcept {
                size_t h = std::hash<int64_t>{}(k.cx);
                h ^= std::hash<int64_t>{}(k.cy) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
                return h;
            }
        };

        std::unordered_map<ChunkKey, std::vector<Cell>, ChunkHash> chunks;
        for (const auto& cell : cells) {
            int64_t cx = (cell.x >= 0) ? cell.x / CHUNK_SIZE : (cell.x - CHUNK_SIZE + 1) / CHUNK_SIZE;
            int64_t cy = (cell.y >= 0) ? cell.y / CHUNK_SIZE : (cell.y - CHUNK_SIZE + 1) / CHUNK_SIZE;
            chunks[{cx, cy}].push_back(cell);
        }

        // Union-Find to merge adjacent chunks
        std::unordered_map<ChunkKey, ChunkKey, ChunkHash> parent;
        for (const auto& [key, _] : chunks) {
            parent[key] = key;
        }

        std::function<ChunkKey(ChunkKey)> find = [&](ChunkKey k) -> ChunkKey {
            if (!(parent[k] == k)) parent[k] = find(parent[k]);
            return parent[k];
        };

        auto unite = [&](ChunkKey a, ChunkKey b) {
            a = find(a);
            b = find(b);
            if (!(a == b)) parent[a] = b;
        };

        for (const auto& [key, _] : chunks) {
            for (int64_t dx = -1; dx <= 1; dx++) {
                for (int64_t dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    ChunkKey neighbor{key.cx + dx, key.cy + dy};
                    if (chunks.count(neighbor)) {
                        unite(key, neighbor);
                    }
                }
            }
        }

        std::unordered_map<ChunkKey, CellSet, ChunkHash> clusters;
        for (const auto& [key, chunk_cells] : chunks) {
            ChunkKey root = find(key);
            for (const auto& cell : chunk_cells) {
                clusters[root].insert(cell);
            }
        }

        cells.clear();
        for (auto& [_, cluster_cells] : clusters) {
            step_cluster(cluster_cells);
            for (const auto& cell : cluster_cells) {
                cells.insert(cell);
            }
        }
    }

    [[nodiscard]] std::unique_ptr<SimulationEngine> clone() const override {
        return std::make_unique<HashLifeEngine>();
    }

    [[nodiscard]] EngineType type() const noexcept override {
        return EngineType::Hashlife;
    }

private:
    NodePool pool_;

    void step_cluster(CellSet& cells) {
        if (cells.empty()) return;

        // Find bounding box
        int64_t min_x = std::numeric_limits<int64_t>::max();
        int64_t max_x = std::numeric_limits<int64_t>::min();
        int64_t min_y = std::numeric_limits<int64_t>::max();
        int64_t max_y = std::numeric_limits<int64_t>::min();

        for (const auto& cell : cells) {
            min_x = std::min(min_x, cell.x);
            max_x = std::max(max_x, cell.x);
            min_y = std::min(min_y, cell.y);
            max_y = std::max(max_y, cell.y);
        }

        int64_t range_x = max_x - min_x + 1;
        int64_t range_y = max_y - min_y + 1;
        int64_t range = std::max(range_x, range_y);

        int level = 1;
        while ((int64_t(1) << level) < range) {
            ++level;
        }

        int64_t size = int64_t(1) << level;
        int64_t ox = min_x - (size - range_x) / 2;
        int64_t oy = min_y - (size - range_y) / 2;

        // Sort cells for efficient range queries during tree construction
        sorted_.build(cells);

        QuadNode* root = build_recursive(ox, oy, level);

        // Expand for border safety
        while (root->level < 2) {
            root = expand(root, ox, oy);
        }
        root = expand(root, ox, oy);
        root = expand(root, ox, oy);

        QuadNode* result = slow_step(root);

        int64_t quarter = int64_t(1) << (root->level - 2);
        int64_t rx = ox + quarter;
        int64_t ry = oy + quarter;

        cells.clear();
        flatten(result, rx, ry, cells);
    }

    SortedCells sorted_;

    QuadNode* expand(QuadNode* node, int64_t& ox, int64_t& oy) {
        int64_t half = int64_t(1) << (node->level - 1);
        QuadNode* e = pool_.empty_node(node->level - 1);
        ox -= half;
        oy -= half;
        return pool_.make(
            pool_.make(e, e, e, node->nw),
            pool_.make(e, e, node->ne, e),
            pool_.make(e, node->sw, e, e),
            pool_.make(node->se, e, e, e)
        );
    }

    // Build quadtree with early exit for empty sub-regions
    QuadNode* build_recursive(int64_t x, int64_t y, int level) {
        int64_t size = int64_t(1) << level;

        // Early exit: if no cells in this region, return canonical empty node
        if (!sorted_.has_cell_in(x, y, size)) {
            return pool_.empty_node(level);
        }

        if (level == 0) {
            return pool_.leaf(sorted_.contains(x, y));
        }

        int64_t half = size >> 1;
        return pool_.make(
            build_recursive(x,        y,        level - 1),
            build_recursive(x + half, y,        level - 1),
            build_recursive(x,        y + half, level - 1),
            build_recursive(x + half, y + half, level - 1)
        );
    }

    // center() returns the center sub-node without any simulation.
    QuadNode* center(QuadNode* node) {
        return pool_.make(node->nw->se, node->ne->sw, node->sw->ne, node->se->nw);
    }

    // slow_step: advance exactly 1 generation.
    QuadNode* slow_step(QuadNode* node) {
        if (node->step1_result) return node->step1_result;

        if (node->population == 0) {
            node->step1_result = pool_.empty_node(node->level - 1);
            return node->step1_result;
        }

        if (node->level == 2) {
            node->step1_result = step_4x4(node);
            return node->step1_result;
        }

        QuadNode* n00 = node->nw;
        QuadNode* n01 = node->ne;
        QuadNode* n10 = node->sw;
        QuadNode* n11 = node->se;

        // 9 overlapping sub-quadrants at level (k-1)
        QuadNode* c01 = pool_.make(n00->ne, n01->nw, n00->se, n01->sw);
        QuadNode* c10 = pool_.make(n00->sw, n00->se, n10->nw, n10->ne);
        QuadNode* c11 = pool_.make(n00->se, n01->sw, n10->ne, n11->nw);
        QuadNode* c12 = pool_.make(n01->sw, n01->se, n11->nw, n11->ne);
        QuadNode* c21 = pool_.make(n10->ne, n11->nw, n10->se, n11->sw);

        // Take centers (no simulation)
        QuadNode* r00 = center(n00);
        QuadNode* r01 = center(c01);
        QuadNode* r02 = center(n01);
        QuadNode* r10 = center(c10);
        QuadNode* r11 = center(c11);
        QuadNode* r12 = center(c12);
        QuadNode* r20 = center(n10);
        QuadNode* r21 = center(c21);
        QuadNode* r22 = center(n11);

        // Assemble and step each quadrant
        node->step1_result = pool_.make(
            slow_step(pool_.make(r00, r01, r10, r11)),
            slow_step(pool_.make(r01, r02, r11, r12)),
            slow_step(pool_.make(r10, r11, r20, r21)),
            slow_step(pool_.make(r11, r12, r21, r22))
        );
        return node->step1_result;
    }

    // Directly compute the center 2x2 of a 4x4 node (level 2)
    QuadNode* step_4x4(QuadNode* node) {
        int g[4][4];
        g[0][0] = static_cast<int>(node->nw->nw->population);
        g[1][0] = static_cast<int>(node->nw->ne->population);
        g[2][0] = static_cast<int>(node->ne->nw->population);
        g[3][0] = static_cast<int>(node->ne->ne->population);
        g[0][1] = static_cast<int>(node->nw->sw->population);
        g[1][1] = static_cast<int>(node->nw->se->population);
        g[2][1] = static_cast<int>(node->ne->sw->population);
        g[3][1] = static_cast<int>(node->ne->se->population);
        g[0][2] = static_cast<int>(node->sw->nw->population);
        g[1][2] = static_cast<int>(node->sw->ne->population);
        g[2][2] = static_cast<int>(node->se->nw->population);
        g[3][2] = static_cast<int>(node->se->ne->population);
        g[0][3] = static_cast<int>(node->sw->sw->population);
        g[1][3] = static_cast<int>(node->sw->se->population);
        g[2][3] = static_cast<int>(node->se->sw->population);
        g[3][3] = static_cast<int>(node->se->se->population);

        auto life_rule = [&](int cx, int cy) -> bool {
            int count = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    count += g[cx + dx][cy + dy];
                }
            }
            bool alive = g[cx][cy] != 0;
            return count == 3 || (count == 2 && alive);
        };

        return pool_.make(
            pool_.leaf(life_rule(1, 1)),
            pool_.leaf(life_rule(2, 1)),
            pool_.leaf(life_rule(1, 2)),
            pool_.leaf(life_rule(2, 2))
        );
    }

    void flatten(QuadNode* node, int64_t x, int64_t y, CellSet& cells) {
        if (node->population == 0) return;

        if (node->level == 0) {
            if (node->population > 0) {
                cells.insert({x, y});
            }
            return;
        }

        int64_t half = int64_t(1) << (node->level - 1);
        flatten(node->nw, x,        y,        cells);
        flatten(node->ne, x + half, y,        cells);
        flatten(node->sw, x,        y + half, cells);
        flatten(node->se, x + half, y + half, cells);
    }
};

std::unique_ptr<SimulationEngine> create_hashlife_engine() {
    return std::make_unique<HashLifeEngine>();
}
