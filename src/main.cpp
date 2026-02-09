#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <chrono>
#include <filesystem>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>
#include "game_of_life.h"
#include "engine.h"
#include "renderer.h"

namespace fs = std::filesystem;

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  -f, --file FILE    Read from FILE (.life or .lif extension required)\n"
              << "  -n, --iterations N Run N iterations (default: 10)\n"
              << "  --engine ENGINE    Simulation engine: hashtable (default), sorted, hashlife\n"
              << "  --stats            Print performance stats to stderr\n"
              << "  -h, --help         Show this help message\n"
              << "\n"
              << "PNG Output:\n"
              << "  --png DIR          Save each frame as PNG to DIR\n"
              << "  --cell-size N      Pixels per cell (default: 4)\n"
              << "  --padding N        Cells of padding around pattern (default: 10)\n"
              << "  --grid             Show grid lines between cells\n"
              << "\n"
              << "Video Output (requires ffmpeg):\n"
              << "  --video FILE       Generate video file (MP4, WebM, or GIF)\n"
              << "  --fps N            Frames per second (default: 30)\n"
              << "  --keep-frames      Keep PNG frames after video generation\n"
              << "\n"
              << "If no file is specified, reads from stdin.\n";
}

// Strict integer parsing - rejects trailing garbage, overflow, negative values
bool parse_positive_int(const char* str, int& result) {
    if (str == nullptr || *str == '\0') return false;

    char* end;
    errno = 0;
    long val = std::strtol(str, &end, 10);

    // Check for trailing garbage
    if (*end != '\0') return false;

    // Check for overflow
    if (errno == ERANGE || val < 0 || val > INT_MAX) return false;

    result = static_cast<int>(val);
    return true;
}

// Overflow-safe subtraction that clamps to INT64_MIN/MAX
int64_t safe_sub(int64_t a, int64_t b) {
    if (b > 0 && a < std::numeric_limits<int64_t>::min() + b) {
        return std::numeric_limits<int64_t>::min();
    }
    if (b < 0 && a > std::numeric_limits<int64_t>::max() + b) {
        return std::numeric_limits<int64_t>::max();
    }
    return a - b;
}

// Overflow-safe addition that clamps to INT64_MIN/MAX
int64_t safe_add(int64_t a, int64_t b) {
    if (b > 0 && a > std::numeric_limits<int64_t>::max() - b) {
        return std::numeric_limits<int64_t>::max();
    }
    if (b < 0 && a < std::numeric_limits<int64_t>::min() - b) {
        return std::numeric_limits<int64_t>::min();
    }
    return a + b;
}

std::string get_temp_dir() {
    // Use mkdtemp for secure temp directory creation
    // Must use char array, not std::string, because mkdtemp modifies in-place
    // and we need to copy the result before the array goes out of scope
    char tmpl[] = "/tmp/life_frames_XXXXXX";
    char* result = mkdtemp(tmpl);
    if (!result) {
        throw std::runtime_error("Failed to create temp directory");
    }
    return std::string(result);
}

void remove_directory_contents(const std::string& dir) {
    // Remove all PNG files in directory using std::filesystem
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".png") {
            fs::remove(entry.path(), ec);
        }
    }
}

void remove_directory(const std::string& dir) {
    std::error_code ec;
    fs::remove_all(dir, ec);
}

std::string get_file_extension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    // Convert to lowercase
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

bool generate_video(const std::string& frame_dir, const std::string& output_path,
                    int fps, bool show_stats) {
    std::string ext = get_file_extension(output_path);

    // Build ffmpeg arguments directly (no shell interpretation)
    std::vector<std::string> args;
    args.push_back("ffmpeg");
    args.push_back("-y");
    args.push_back("-framerate");
    args.push_back(std::to_string(fps));
    args.push_back("-i");
    args.push_back(frame_dir + "/frame_%05d.png");

    // Add codec-specific arguments
    if (ext == ".mp4") {
        args.push_back("-vf");
        args.push_back("pad=ceil(iw/2)*2:ceil(ih/2)*2");
        args.push_back("-c:v");
        args.push_back("libx264");
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
        args.push_back("-preset");
        args.push_back("fast");
        args.push_back("-crf");
        args.push_back("18");
    } else if (ext == ".webm") {
        args.push_back("-vf");
        args.push_back("pad=ceil(iw/2)*2:ceil(ih/2)*2");
        args.push_back("-c:v");
        args.push_back("libvpx-vp9");
        args.push_back("-crf");
        args.push_back("30");
        args.push_back("-b:v");
        args.push_back("0");
    } else if (ext == ".gif") {
        args.push_back("-vf");
        args.push_back("split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse");
    } else if (ext == ".mov") {
        args.push_back("-vf");
        args.push_back("pad=ceil(iw/2)*2:ceil(ih/2)*2");
        args.push_back("-c:v");
        args.push_back("prores_ks");
        args.push_back("-profile:v");
        args.push_back("3");
        args.push_back("-pix_fmt");
        args.push_back("yuv422p10le");
    } else {
        // Default to MP4
        args.push_back("-vf");
        args.push_back("pad=ceil(iw/2)*2:ceil(ih/2)*2");
        args.push_back("-c:v");
        args.push_back("libx264");
        args.push_back("-pix_fmt");
        args.push_back("yuv420p");
        args.push_back("-preset");
        args.push_back("fast");
        args.push_back("-crf");
        args.push_back("18");
    }

    args.push_back(output_path);

    if (show_stats) {
        std::cerr << "   🎬 Encoding video...\n";
    }

    // Fork and exec ffmpeg directly (no shell interpretation = no injection)
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Error: Failed to fork for ffmpeg\n";
        return false;
    }

    if (pid == 0) {
        // Child process: exec ffmpeg
        // Redirect stdout/stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        // Build argv array for execvp
        std::vector<char*> argv;
        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp("ffmpeg", argv.data());
        // If execvp returns, it failed
        _exit(127);
    }

    // Parent process: wait for child
    int status;
    waitpid(pid, &status, 0);

    bool cmd_success = (WIFEXITED(status) && WEXITSTATUS(status) == 0);

    // Also verify the output file exists and has content
    std::error_code ec;
    bool file_exists = fs::exists(output_path, ec) && fs::file_size(output_path, ec) > 0;

    return cmd_success || file_exists;
}

int main(int argc, char* argv[]) {
    int iterations = 10;
    std::string filepath;
    bool use_stdin = true;
    bool show_stats = false;
    EngineType engine_type = EngineType::Hashtable;

    // PNG options
    bool render_png = false;
    RenderConfig render_config;

    // Video options
    bool generate_video_output = false;
    std::string video_output_path;
    int video_fps = 30;
    bool keep_frames = false;
    bool using_temp_dir = false;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a filename argument\n";
                return 1;
            }
            filepath = argv[++i];
            use_stdin = false;
        } else if (arg == "-n" || arg == "--iterations") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
            if (!parse_positive_int(argv[++i], iterations)) {
                std::cerr << "Error: Invalid iteration count (must be a non-negative integer)\n";
                return 1;
            }
        } else if (arg == "--engine") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires an engine name argument\n";
                return 1;
            }
            try {
                engine_type = parse_engine_type(argv[++i]);
            } catch (const std::invalid_argument& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return 1;
            }
        } else if (arg == "--stats") {
            show_stats = true;
        } else if (arg == "--png") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a directory argument\n";
                return 1;
            }
            render_config.output_dir = argv[++i];
            render_png = true;
        } else if (arg == "--cell-size") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
            if (!parse_positive_int(argv[++i], render_config.cell_size) || render_config.cell_size < 1) {
                std::cerr << "Error: Invalid cell size (must be a positive integer)\n";
                return 1;
            }
        } else if (arg == "--padding") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
            if (!parse_positive_int(argv[++i], render_config.padding)) {
                std::cerr << "Error: Invalid padding (must be a non-negative integer)\n";
                return 1;
            }
        } else if (arg == "--grid") {
            render_config.show_grid = true;
        } else if (arg == "--video") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a filename argument\n";
                return 1;
            }
            video_output_path = argv[++i];
            generate_video_output = true;
        } else if (arg == "--fps") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a number argument\n";
                return 1;
            }
            if (!parse_positive_int(argv[++i], video_fps) || video_fps < 1) {
                std::cerr << "Error: Invalid FPS (must be a positive integer)\n";
                return 1;
            }
        } else if (arg == "--keep-frames") {
            keep_frames = true;
        } else {
            std::cerr << "Error: Unknown argument '" << arg << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate file extension if reading from file
    if (!use_stdin && !has_valid_life_extension(filepath)) {
        std::cerr << "Error: File must have .life or .lif extension\n";
        return 1;
    }

    // If video output is requested but no PNG dir specified, use temp directory
    if (generate_video_output && !render_png) {
        render_config.output_dir = get_temp_dir();
        render_png = true;
        using_temp_dir = true;
    }

    // Validate PNG output directory
    if (render_png && !fs::is_directory(render_config.output_dir)) {
        std::error_code ec;
        if (!fs::create_directory(render_config.output_dir, ec) && ec) {
            std::cerr << "Error: Cannot create PNG output directory '" << render_config.output_dir << "'\n";
            return 1;
        }
    }

    // Read input
    try {
        auto total_start = std::chrono::high_resolution_clock::now();

        // Parse phase
        auto parse_start = std::chrono::high_resolution_clock::now();
        GameOfLife game;
        if (use_stdin) {
            game = GameOfLife::parse(std::cin, engine_type);
        } else {
            std::ifstream file(filepath);
            if (!file) {
                std::cerr << "Error: Cannot open file '" << filepath << "'\n";
                return 1;
            }
            game = GameOfLife::parse(file, engine_type);
        }
        auto parse_end = std::chrono::high_resolution_clock::now();

        size_t initial_cells = game.count();

        // Calculate fixed viewport for PNG rendering (based on initial state + padding for growth)
        int64_t vp_min_x = 0, vp_max_x = 0, vp_min_y = 0, vp_max_y = 0;
        if (render_png) {
            if (get_bounding_box(game, vp_min_x, vp_max_x, vp_min_y, vp_max_y)) {
                // Check if bounding box is too large for rendering
                int64_t width = vp_max_x - vp_min_x + 1;
                int64_t height = vp_max_y - vp_min_y + 1;
                constexpr int64_t MAX_RENDER_CELLS = 10000;

                if (width > MAX_RENDER_CELLS || height > MAX_RENDER_CELLS) {
                    std::cerr << "Warning: Pattern spans " << width << " x " << height
                              << " cells, too large for PNG rendering.\n";
                    std::cerr << "         PNG/video output disabled. Use a smaller pattern.\n";
                    render_png = false;
                    generate_video_output = false;
                } else {
                    // Add extra padding for pattern growth (using overflow-safe arithmetic)
                    int64_t growth_padding = render_config.padding + iterations / 2;
                    vp_min_x = safe_sub(vp_min_x, growth_padding);
                    vp_max_x = safe_add(vp_max_x, growth_padding);
                    vp_min_y = safe_sub(vp_min_y, growth_padding);
                    vp_max_y = safe_add(vp_max_y, growth_padding);
                }
            } else {
                vp_min_x = vp_min_y = -50;
                vp_max_x = vp_max_y = 50;
            }
        }

        // Simulation phase
        if (show_stats) {
            std::cerr << "🧬 Game of Life Simulation\n";
            std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cerr << "📥 Input:      " << initial_cells << " cells\n";
            std::cerr << "🔄 Iterations: " << iterations << "\n";
            if (render_png && !using_temp_dir) {
                std::cerr << "🖼️  PNG:        " << render_config.output_dir << "/\n";
            }
            if (generate_video_output) {
                std::cerr << "🎬 Video:      " << video_output_path << " @ " << video_fps << " fps\n";
            }
            std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
        }

        auto sim_start = std::chrono::high_resolution_clock::now();

        // Render initial state (frame 0)
        if (render_png) {
            if (!render_frame_fixed_viewport(game, render_config, 0,
                                              vp_min_x, vp_max_x, vp_min_y, vp_max_y)) {
                std::cerr << "Warning: Failed to render frame 0\n";
            }
        }

        // Run simulation with optional per-frame rendering
        for (int i = 0; i < iterations; i++) {
            game.tick();

            if (render_png) {
                if (!render_frame_fixed_viewport(game, render_config, i + 1,
                                                  vp_min_x, vp_max_x, vp_min_y, vp_max_y)) {
                    std::cerr << "Warning: Failed to render frame " << (i + 1) << "\n";
                }

                // Progress indicator for long renders
                if (show_stats && iterations >= 10 && (i + 1) % (iterations / 10) == 0) {
                    std::cerr << "   📸 Rendered frame " << (i + 1) << "/" << iterations << "\n";
                }
            }
        }

        auto sim_end = std::chrono::high_resolution_clock::now();

        // Generate video if requested
        bool video_success = false;
        if (generate_video_output && render_png) {
            video_success = generate_video(render_config.output_dir, video_output_path,
                                           video_fps, show_stats);
            if (!video_success) {
                std::cerr << "Warning: Video generation failed. Is ffmpeg installed?\n";
            }
        }

        // Clean up temp frames if using temp directory and not keeping frames
        if (using_temp_dir && !keep_frames) {
            remove_directory(render_config.output_dir);
        } else if (generate_video_output && !keep_frames && !using_temp_dir) {
            // User specified --png and --video but not --keep-frames
            // Keep the frames since they explicitly requested PNG output
        }

        // Output phase
        auto write_start = std::chrono::high_resolution_clock::now();
        game.write(std::cout);
        auto write_end = std::chrono::high_resolution_clock::now();

        auto total_end = std::chrono::high_resolution_clock::now();

        if (show_stats) {
            auto parse_ms = std::chrono::duration_cast<std::chrono::microseconds>(parse_end - parse_start).count() / 1000.0;
            auto sim_ms = std::chrono::duration_cast<std::chrono::microseconds>(sim_end - sim_start).count() / 1000.0;
            auto write_ms = std::chrono::duration_cast<std::chrono::microseconds>(write_end - write_start).count() / 1000.0;
            auto total_ms = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start).count() / 1000.0;

            std::cerr << "📊 Results\n";
            std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cerr << "📤 Output:     " << game.count() << " cells\n";
            std::cerr << "📈 Change:     ";
            int64_t diff = static_cast<int64_t>(game.count()) - static_cast<int64_t>(initial_cells);
            if (diff > 0) {
                std::cerr << "+" << diff << " cells\n";
            } else if (diff < 0) {
                std::cerr << diff << " cells\n";
            } else {
                std::cerr << "no change\n";
            }
            if (render_png && !using_temp_dir) {
                std::cerr << "🖼️  Frames:     " << (iterations + 1) << " PNG files\n";
            }
            if (generate_video_output && video_success) {
                std::cerr << "🎬 Video:      " << video_output_path << "\n";
            }
            std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cerr << "⏱️  Timing\n";
            std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
            std::cerr << "   Parse:      " << parse_ms << " ms\n";
            std::cerr << "   Simulate:   " << sim_ms << " ms";
            if (render_png) {
                std::cerr << " (includes rendering)";
            }
            std::cerr << "\n";
            std::cerr << "   Write:      " << write_ms << " ms\n";
            std::cerr << "   ─────────────────────\n";
            std::cerr << "   Total:      " << total_ms << " ms\n";
            std::cerr << "━━━━━━━━━━━━━━━━━━━━━━━━━━\n";

            if (iterations > 0 && sim_ms > 0) {
                double ticks_per_sec = iterations / (sim_ms / 1000.0);
                std::cerr << "🚀 Speed:      " << static_cast<int>(ticks_per_sec) << " ticks/sec\n";
            }
            std::cerr << "✅ Done!\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
