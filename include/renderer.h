#ifndef LIFE_RENDERER_H
#define LIFE_RENDERER_H

#include "game_of_life.h"
#include <string>
#include <cstdint>

/**
 * Configuration for rendering Game of Life frames to PNG images.
 */
struct RenderConfig {
    std::string output_dir = ".";   // Directory to save PNG files
    int cell_size = 4;              // Pixels per cell
    int padding = 10;               // Cells of padding around bounding box
    int max_width = 4096;           // Maximum image width
    int max_height = 4096;          // Maximum image height
    uint32_t alive_color = 0xFF00FF00;  // RGBA: green
    uint32_t dead_color = 0xFF000000;   // RGBA: black
    uint32_t grid_color = 0xFF333333;   // RGBA: dark gray (optional grid lines)
    bool show_grid = false;         // Draw grid lines
    int64_t max_pixels = 16 * 1024 * 1024;  // Maximum total pixels (16 megapixels)
    int64_t max_cells_dimension = 10000;    // Max cells in either dimension
};

/**
 * Render Game of Life state to a PNG file.
 *
 * @param game Current game state
 * @param config Rendering configuration
 * @param frame_number Frame number (used for filename)
 * @return true if successful, false on error
 */
[[nodiscard]] bool render_frame(const GameOfLife& game, const RenderConfig& config, int frame_number);

/**
 * Render Game of Life state to a PNG file with fixed viewport.
 * Use this when rendering multiple frames to keep consistent framing.
 *
 * @param game Current game state
 * @param config Rendering configuration
 * @param frame_number Frame number (used for filename)
 * @param min_x Minimum x coordinate of viewport
 * @param max_x Maximum x coordinate of viewport
 * @param min_y Minimum y coordinate of viewport
 * @param max_y Maximum y coordinate of viewport
 * @return true if successful, false on error
 */
[[nodiscard]] bool render_frame_fixed_viewport(const GameOfLife& game, const RenderConfig& config,
                                  int frame_number,
                                  int64_t min_x, int64_t max_x,
                                  int64_t min_y, int64_t max_y);

/**
 * Calculate bounding box of all live cells.
 *
 * @param game Current game state
 * @param min_x Output: minimum x coordinate
 * @param max_x Output: maximum x coordinate
 * @param min_y Output: minimum y coordinate
 * @param max_y Output: maximum y coordinate
 * @return true if cells exist, false if empty
 */
[[nodiscard]] bool get_bounding_box(const GameOfLife& game,
                      int64_t& min_x, int64_t& max_x,
                      int64_t& min_y, int64_t& max_y);

#endif // LIFE_RENDERER_H
