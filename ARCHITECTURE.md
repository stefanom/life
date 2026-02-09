# Conway's Game of Life -- Architecture

## What It Does

This project is a command-line simulator for Conway's Game of Life. It reads an
initial pattern in Life 1.06 format, runs the cellular automaton for a
configurable number of generations, and writes the final state to stdout. It can
also render each frame as a PNG and stitch them into a video via ffmpeg.

### Feature Summary

- **Simulation**: Sparse-grid Game of Life supporting the full `int64_t`
  coordinate range, with three selectable simulation engines.
- **I/O**: Reads/writes Life 1.06 format from files or stdin/stdout.
- **PNG rendering**: Outputs per-frame images with configurable cell size,
  padding, grid lines, and colors.
- **Video generation**: Shells out to ffmpeg (via `fork`/`execvp`) to produce
  MP4, WebM, GIF, or MOV files from rendered frames.

## Project Structure

```
src/main.cpp                CLI, argument parsing, orchestration
src/game_of_life.cpp        GameOfLife class (owns cells, delegates tick to engine)
src/engine.cpp              Engine factory and parse_engine_type()
src/engine_hashtable.cpp    HashtableEngine (default, hash-based neighbor counting)
src/engine_sorted_vector.cpp  SortedVectorEngine (sort-based neighbor counting)
src/engine_hashlife.cpp     HashLifeEngine (memoized quadtree)
src/renderer.cpp            PNG frame rendering (stb_image_write)

include/game_of_life.h      Cell type, hash, CellSet/CellCountMap, GameOfLife class
include/engine.h            SimulationEngine ABC, EngineType enum, factory
include/renderer.h          RenderConfig struct, render API

test/test_game_of_life.cpp  37 unit tests
test/benchmark.cpp          Single-engine performance benchmark
test/benchmark_engines.cpp  Comparative benchmark (correctness + timing, all 3 engines)

third_party/
  unordered_dense.h         ankerl robin-hood hash table (fast CellSet/CellCountMap)
  stb_image_write.h         Single-file PNG encoder
```

## Engine Architecture

The simulation uses a **strategy pattern via composition**. `GameOfLife` keeps
its public API unchanged but delegates `tick()` to an internal
`SimulationEngine` via `unique_ptr`. The default engine is `Hashtable`,
preserving all existing behavior.

```
SimulationEngine (abstract base, virtual tick(CellSet&))
  ├── HashtableEngine      (hash-based neighbor counting)
  ├── SortedVectorEngine   (sort-based neighbor counting)
  └── HashLifeEngine       (memoized quadtree with spatial clustering)

GameOfLife  (unchanged public API, owns unique_ptr<SimulationEngine>)
```

The engine's `tick()` takes `CellSet&` by mutable reference -- reads current
cells, replaces with next generation. `GameOfLife` still owns `live_cells_`
directly, so `cells()`, `write()`, `count()` are unchanged.

### Engine Selection

Engines are selected via the `EngineType` enum and `--engine` CLI flag:

```bash
./game_of_life --engine hashtable   # default
./game_of_life --engine sorted
./game_of_life --engine hashlife
```

`parse_engine_type()` maps a string to the enum. `create_engine()` is the
factory that returns a `unique_ptr<SimulationEngine>`.

## Engine Implementations

### HashtableEngine

The simplest algorithm which uses a hash table:

1. For every live cell, increment a counter on each of its 8 neighbors in a
   `CellCountMap` (hash map from `Cell -> int`).
2. Iterate the count map. A cell is alive in the next generation if its count
   is 3, or its count is 2 and it is currently alive.
3. Swap the new cell set into place.

O(N) per tick where N = number of live cells. Internal buffers
(`neighbor_count_buffer_`, `new_cells_buffer_`) are engine members reused
across ticks.

### SortedVectorEngine

A cache-friendly alternative that avoids hash table overhead:

1. Copy live cells to a sorted `vector<Cell>`.
2. Emit 8 neighbor coordinates per cell into a `candidates` vector (8N entries).
3. Sort `candidates`.
4. Walk sorted candidates counting runs of identical cells to get neighbor counts.
5. count==3 → alive; count==2 → alive if in sorted live cells (binary search).
6. Write results back into `CellSet`.

O(N log N) per tick, but the constant factor is lower than hash map operations
for moderate N due to sequential memory access patterns. Buffers are reused
across ticks.

### HashLifeEngine

A memoized quadtree algorithm with spatial clustering:

- **Spatial clustering**: Cells are grouped into 64-cell chunks. Adjacent
  chunks are merged via union-find into clusters. Each cluster is stepped
  independently, allowing widely separated cell groups (e.g. coordinates
  trillions apart) to be handled efficiently.

- **Quadtree construction**: Each cluster builds a level-based quadtree using
  `build_recursive()` with early exit for empty sub-regions via sorted-cell
  range queries. This creates only the nodes where cells actually exist.

- **`slow_step()`**: Advances exactly 1 generation for a level-k node. At
  level 2 (4×4), computes the center 2×2 directly via Game of Life rules.
  At higher levels, decomposes into 9 overlapping sub-quadrants, extracts
  their centers (no simulation), assembles 4 combined nodes, and recurses.
  This avoids the standard HashLife `2^(k-2)` step doubling.

- **Hash-consing**: Nodes are interned in a hash table keyed by their 4
  children. Identical sub-trees share a single canonical node. An arena
  allocator provides fast allocation; the pool is cleared each tick.

- **Memoization**: `step1_result` on each node caches the 1-generation
  advance, so identical sub-trees (common in regular patterns like block
  grids) are computed once.

## Data Structures

| Type | Underlying | Purpose |
|------|-----------|---------|
| `CellSet` | `ankerl::unordered_dense::set` | Stores live cells |
| `CellCountMap` | `ankerl::unordered_dense::map` | Neighbor counts (hashtable engine) |
| `Cell` | `{int64_t x, y}` | A coordinate pair |
| `CellHash` | MurmurHash3 finalizer | Hash function for Cell |
| `QuadNode` | Struct with level, population, 4 children | HashLife tree node |
| `NodePool` | Arena allocator + hash-cons table | Canonical node storage |

## Copy and Move Semantics

- `SimulationEngine` provides a virtual `clone()` method.
- `GameOfLife` copy constructor/assignment use `engine_->clone()`.
- Move operations transfer the `unique_ptr` directly.

## Compiler Optimizations

`-O3 -march=native -flto` enables aggressive inlining, auto-vectorization,
and link-time optimization across all translation units.

## Future Improvement Opportunities

### SIMD / Bitboard optimization for dense regions

The current engines treat every cell individually. For dense regions, bitboard
or SIMD techniques can process 64 cells simultaneously using bitwise operations.

### Parallelism

The simulation is single-threaded. The neighbor-counting phase is
embarrassingly parallelizable. Partitioning live cells into spatial buckets and
processing them with thread-local state could provide near-linear speedup with
core count.

### Multi-step HashLife

The current HashLife implementation advances exactly 1 generation per call.
The standard HashLife algorithm advances `2^(k-2)` generations in one recursive
call, providing exponential speedups for long-running simulations on patterns
with temporal regularity. This would require a different interface than the
current per-tick CellSet approach.
