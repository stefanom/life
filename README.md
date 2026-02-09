# Game of Life

A C++17 implementation of Conway's Game of Life using the Life 1.06 file format.

## Build

```bash
make
```

## Quick Start

```bash
# Run 10 iterations on a sample file
./game_of_life -f examples/sample_input.life -n 10

# Or use the helper script
./run_life.sh examples/sample_input.life 10
```

## Command-Line Options

```
Usage: ./game_of_life [OPTIONS]

Options:
  -f, --file FILE    Read from FILE (.life or .lif extension required)
  -n, --iterations N Run N iterations (default: 10)
  --engine ENGINE    Simulation engine: hashtable (default), sorted, hashlife
  --stats            Print performance stats to stderr
  -h, --help         Show help message

PNG Output:
  --png DIR          Save each frame as PNG to DIR
  --cell-size N      Pixels per cell (default: 4)
  --padding N        Cells of padding around pattern (default: 10)
  --grid             Show grid lines between cells

Video Output (requires ffmpeg):
  --video FILE       Generate video file (MP4, WebM, or GIF)
  --fps N            Frames per second (default: 30)
  --keep-frames      Keep PNG frames after video generation
```

## Examples

### Basic Simulation

```bash
# Run 100 iterations
./game_of_life -f examples/glider.life -n 100

# Run with performance stats
./game_of_life -f examples/large_test.life -n 50 --stats
```

### Generating PNG Frames

Save each generation as a PNG image:

```bash
# Create frames directory and generate PNGs
mkdir -p output/frames
./game_of_life -f examples/glider.life -n 50 --png output/frames

# Customize cell size (larger cells)
./game_of_life -f examples/glider.life -n 50 --png output/frames --cell-size 10

# Add grid lines between cells
./game_of_life -f examples/glider.life -n 50 --png output/frames --cell-size 8 --grid

# More padding around the pattern
./game_of_life -f examples/glider.life -n 50 --png output/frames --padding 20
```

This creates files: `output/frames/frame_00000.png`, `frame_00001.png`, etc.

### Generating Videos

Create animated videos directly (requires [ffmpeg](https://ffmpeg.org/)):

```bash
# Generate MP4 video
./game_of_life -f examples/glider.life -n 100 --video glider.mp4

# Generate GIF animation
./game_of_life -f examples/glider.life -n 100 --video glider.gif

# Generate WebM video
./game_of_life -f examples/glider.life -n 100 --video glider.webm

# Custom frame rate (slower animation)
./game_of_life -f examples/glider.life -n 100 --video glider.mp4 --fps 10

# Combine with rendering options
./game_of_life -f examples/glider.life -n 100 \
    --video glider.mp4 --fps 15 --cell-size 8 --grid
```

### Keeping Frames with Video

```bash
# Generate video AND keep the PNG frames
./game_of_life -f examples/glider.life -n 100 \
    --png output/frames --video glider.mp4 --keep-frames

# Generate video with frames in custom directory
mkdir -p my_frames
./game_of_life -f examples/glider.life -n 100 \
    --png my_frames --video animation.mp4
```

### Full Example with Stats

```bash
./game_of_life -f examples/large_test.life -n 200 \
    --stats --video simulation.mp4 --fps 30 --cell-size 4
```

Output:
```
🧬 Game of Life Simulation
━━━━━━━━━━━━━━━━━━━━━━━━━━
📥 Input:      2234 cells
🔄 Iterations: 200
🎬 Video:      simulation.mp4 @ 30 fps
━━━━━━━━━━━━━━━━━━━━━━━━━━
   📸 Rendered frame 20/200
   📸 Rendered frame 40/200
   ...
   🎬 Encoding video...
📊 Results
━━━━━━━━━━━━━━━━━━━━━━━━━━
📤 Output:     2156 cells
📈 Change:     -78 cells
🎬 Video:      simulation.mp4
━━━━━━━━━━━━━━━━━━━━━━━━━━
🚀 Speed:      1250 ticks/sec
✅ Done!
```

### Reading from stdin

```bash
# Pipe input from another command
cat examples/glider.life | ./game_of_life -n 10

# Use heredoc for inline patterns
./game_of_life -n 5 << 'EOF'
#Life 1.06
0 0
1 0
2 0
EOF
```

## Test

```bash
make test
```

## Debug / Sanitizers

```bash
make debug      # Debug build with symbols
make san        # Build with AddressSanitizer and UBSan
```

## Benchmarks

```bash
make benchmark          # Run performance benchmarks
make benchmark-engines  # Compare all three engines
```

## Simulation Engines

Three simulation engines are available, selectable via `--engine`:

| Engine | Algorithm | Best for |
|--------|-----------|----------|
| `hashtable` | Hash-based neighbor counting (default) | General purpose, dense patterns |
| `sorted` | Sort-based neighbor counting | No hash overhead, predictable performance |
| `hashlife` | Memoized quadtree with spatial clustering | Large repetitive/stable patterns |

```bash
# Use the sorted-vector engine
./game_of_life --engine sorted -f examples/glider.life -n 100

# Use the HashLife engine
./game_of_life --engine hashlife -f examples/glider.life -n 100
```

## File Format

This implementation uses the [Life 1.06](http://www.conwaylife.com/wiki/Life_1.06) format:

```
#Life 1.06
0 1
1 2
2 0
2 1
2 2
```

- First line must be `#Life 1.06`
- Each subsequent line contains `x y` coordinates of a live cell
- Coordinates can be any 64-bit signed integer

## Notes

- Supports coordinates in the full `int64_t` range
- Cells at `int64_t` boundaries are skipped during neighbor computation to avoid overflow
- Uses sparse storage (hash set) for efficient handling of patterns with large coordinates
- Engine selection uses a strategy pattern; all engines produce identical results
