#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "renderer.h"

#include <vector>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>

bool get_bounding_box(const GameOfLife& game,
                      int64_t& min_x, int64_t& max_x,
                      int64_t& min_y, int64_t& max_y) {
    const auto& cells = game.cells();
    if (cells.empty()) {
        // Initialize output params to sensible defaults even on failure
        min_x = max_x = 0;
        min_y = max_y = 0;
        return false;
    }

    min_x = std::numeric_limits<int64_t>::max();
    max_x = std::numeric_limits<int64_t>::min();
    min_y = std::numeric_limits<int64_t>::max();
    max_y = std::numeric_limits<int64_t>::min();

    for (const auto& cell : cells) {
        min_x = std::min(min_x, cell.x);
        max_x = std::max(max_x, cell.x);
        min_y = std::min(min_y, cell.y);
        max_y = std::max(max_y, cell.y);
    }

    return true;
}

bool render_frame_fixed_viewport(const GameOfLife& game, const RenderConfig& config,
                                  int frame_number,
                                  int64_t min_x, int64_t max_x,
                                  int64_t min_y, int64_t max_y) {
    // Check for potential overflow in dimension calculation (max_x - min_x + 1)
    // This can overflow if max_x is very large and min_x is very negative
    if (max_x > 0 && min_x < 0 && max_x > std::numeric_limits<int64_t>::max() + min_x) {
        return false;  // Overflow would occur
    }
    if (max_y > 0 && min_y < 0 && max_y > std::numeric_limits<int64_t>::max() + min_y) {
        return false;  // Overflow would occur
    }

    // Calculate viewport dimensions in cells
    int64_t width_cells = max_x - min_x + 1;
    int64_t height_cells = max_y - min_y + 1;

    // Sanity check: if viewport is too large, we can't render it
    if (width_cells > config.max_cells_dimension || height_cells > config.max_cells_dimension ||
        width_cells <= 0 || height_cells <= 0) {
        // Viewport too large or invalid, skip rendering
        return false;
    }

    // Calculate effective cell size
    int eff_cell_size = config.cell_size;

    // Check for overflow in pixel calculation before computing
    // width_cells * height_cells * eff_cell_size * eff_cell_size
    int64_t cell_area = width_cells * height_cells;  // Safe because both are <= max_cells_dimension
    int64_t cell_size_sq = static_cast<int64_t>(eff_cell_size) * eff_cell_size;
    if (cell_area > config.max_pixels / cell_size_sq) {
        // Would overflow or exceed max, need to scale down
    }

    // Scale down if image would be too large
    int64_t total_pixels = cell_area * cell_size_sq;
    while (total_pixels > config.max_pixels && eff_cell_size > 1) {
        eff_cell_size--;
        cell_size_sq = static_cast<int64_t>(eff_cell_size) * eff_cell_size;
        total_pixels = cell_area * cell_size_sq;
    }

    // Calculate image dimensions
    int img_width = static_cast<int>(width_cells * eff_cell_size);
    int img_height = static_cast<int>(height_cells * eff_cell_size);

    // Clamp to max dimensions
    img_width = std::min(img_width, config.max_width);
    img_height = std::min(img_height, config.max_height);

    if (img_width <= 0 || img_height <= 0) {
        return false;
    }

    // Create image buffer (RGBA)
    std::vector<uint32_t> pixels(static_cast<size_t>(img_width) * img_height, config.dead_color);

    // Precompute grid settings to avoid repeated checks in hot loop
    const bool draw_grid = config.show_grid && eff_cell_size > 2;

    // Draw grid lines if enabled
    // Note: This is O(num_lines * pixels_per_line) which can be significant for large images
    if (draw_grid) {
        // Draw vertical lines
        for (int64_t cx = 0; cx <= width_cells; cx++) {
            int x = static_cast<int>(cx * eff_cell_size);
            if (x < img_width) {
                for (int y = 0; y < img_height; y++) {
                    pixels[static_cast<size_t>(y) * img_width + x] = config.grid_color;
                }
            }
        }
        // Draw horizontal lines
        for (int64_t cy = 0; cy <= height_cells; cy++) {
            int y = static_cast<int>(cy * eff_cell_size);
            if (y < img_height) {
                for (int x = 0; x < img_width; x++) {
                    pixels[static_cast<size_t>(y) * img_width + x] = config.grid_color;
                }
            }
        }
    }

    // Draw live cells - separate code paths for grid/no-grid to avoid branch in hot loop
    if (draw_grid) {
        // With grid: skip first row/column of each cell (where grid lines are)
        for (const auto& cell : game.cells()) {
            int64_t rel_x = cell.x - min_x;
            int64_t rel_y = cell.y - min_y;

            if (rel_x < 0 || rel_x >= width_cells || rel_y < 0 || rel_y >= height_cells) {
                continue;
            }

            int px_start_x = static_cast<int>(rel_x * eff_cell_size);
            int px_start_y = static_cast<int>(rel_y * eff_cell_size);

            if (px_start_x >= img_width || px_start_y >= img_height) {
                continue;
            }

            // Fill cell rectangle, starting at offset 1 to skip grid lines
            int max_dy = std::min(eff_cell_size, img_height - px_start_y);
            int max_dx = std::min(eff_cell_size, img_width - px_start_x);
            for (int dy = 1; dy < max_dy; dy++) {
                for (int dx = 1; dx < max_dx; dx++) {
                    pixels[static_cast<size_t>(px_start_y + dy) * img_width + (px_start_x + dx)] = config.alive_color;
                }
            }
        }
    } else {
        // No grid: fill entire cell rectangle
        for (const auto& cell : game.cells()) {
            int64_t rel_x = cell.x - min_x;
            int64_t rel_y = cell.y - min_y;

            if (rel_x < 0 || rel_x >= width_cells || rel_y < 0 || rel_y >= height_cells) {
                continue;
            }

            int px_start_x = static_cast<int>(rel_x * eff_cell_size);
            int px_start_y = static_cast<int>(rel_y * eff_cell_size);

            if (px_start_x >= img_width || px_start_y >= img_height) {
                continue;
            }

            // Fill entire cell rectangle
            int max_dy = std::min(eff_cell_size, img_height - px_start_y);
            int max_dx = std::min(eff_cell_size, img_width - px_start_x);
            for (int dy = 0; dy < max_dy; dy++) {
                for (int dx = 0; dx < max_dx; dx++) {
                    pixels[static_cast<size_t>(px_start_y + dy) * img_width + (px_start_x + dx)] = config.alive_color;
                }
            }
        }
    }

    // Generate filename using std::string (avoids fixed buffer issues)
    char frame_str[16];
    snprintf(frame_str, sizeof(frame_str), "%05d", frame_number);
    std::string filename = config.output_dir + "/frame_" + frame_str + ".png";

    // Write PNG (RGBA = 4 channels)
    int result = stbi_write_png(filename.c_str(), img_width, img_height, 4,
                                 pixels.data(), img_width * 4);

    return result != 0;
}

bool render_frame(const GameOfLife& game, const RenderConfig& config, int frame_number) {
    int64_t min_x, max_x, min_y, max_y;

    if (!get_bounding_box(game, min_x, max_x, min_y, max_y)) {
        // Empty game - render a small empty image
        min_x = min_y = 0;
        max_x = max_y = 10;
    }

    // Add padding
    min_x -= config.padding;
    max_x += config.padding;
    min_y -= config.padding;
    max_y += config.padding;

    return render_frame_fixed_viewport(game, config, frame_number,
                                        min_x, max_x, min_y, max_y);
}
