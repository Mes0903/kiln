#include "progress_bar.hpp"
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <sys/ioctl.h>

namespace dmake {

ProgressBar::ProgressBar(int total_tasks, bool is_tty)
    : is_tty_(is_tty)
    , total_(total_tasks)
    , start_time_(std::chrono::steady_clock::now())
    , last_draw_time_(start_time_)
{
}

void ProgressBar::erase() {
    if (!is_tty_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!bar_visible_) return;

    int term_width = get_terminal_width();

    // If previous render was wider than current terminal, it wrapped.
    // Clean up the extra lines.
    if (last_rendered_width_ > term_width && term_width > 0) {
        int extra_lines = (last_rendered_width_ - 1) / term_width;
        for (int i = 0; i < extra_lines; i++) {
            std::cout << "\x1B[A\x1B[2K";  // cursor up, erase line
        }
    }

    std::cout << "\r\x1B[K" << std::flush;
    bar_visible_ = false;
    last_rendered_width_ = 0;
}

void ProgressBar::request_redraw() {
    if (!is_tty_) return;
    // Force next redraw() to actually draw regardless of throttle
    std::lock_guard<std::mutex> lock(mutex_);
    last_draw_time_ = std::chrono::steady_clock::time_point{};
}

void ProgressBar::redraw() {
    if (!is_tty_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    // Always update terminal title (cheap, no visual flicker)
    update_title_locked();

    // Don't show progress bar for fast builds
    if (now - start_time_ < initial_delay_) return;

    // Throttle redraws
    if (now - last_draw_time_ < redraw_interval_) return;

    draw_locked();
    last_draw_time_ = now;
}

void ProgressBar::task_started(std::string_view filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_tasks_.emplace_back(filename);
}

void ProgressBar::task_finished(std::string_view filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find(active_tasks_.begin(), active_tasks_.end(), filename);
    if (it != active_tasks_.end()) {
        active_tasks_.erase(it);
    }
}

int ProgressBar::mark_completed() {
    return completed_.fetch_add(1, std::memory_order_relaxed) + 1;
}

void ProgressBar::bump_total(int additional) {
    total_.fetch_add(additional, std::memory_order_relaxed);
}

void ProgressBar::finish() {
    if (!is_tty_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!bar_visible_) return;

    int term_width = get_terminal_width();
    if (last_rendered_width_ > term_width && term_width > 0) {
        int extra_lines = (last_rendered_width_ - 1) / term_width;
        for (int i = 0; i < extra_lines; i++) {
            std::cout << "\x1B[A\x1B[2K";
        }
    }

    std::cout << "\r\x1B[K" << std::flush;
    // Reset terminal title
    std::cout << "\x1B]0;\x07" << std::flush;
    bar_visible_ = false;
    last_rendered_width_ = 0;
}

int ProgressBar::get_terminal_width() const {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return ws.ws_col;
    }
    return 80;
}

void ProgressBar::draw_locked() {
    int term_width = get_terminal_width();
    std::string line = build_progress_line(term_width);

    // Truncate to terminal width to prevent wrapping
    if (static_cast<int>(line.size()) >= term_width) {
        line.resize(term_width - 1);
    }

    // Clean up any previous wrapped content
    if (bar_visible_ && last_rendered_width_ > term_width && term_width > 0) {
        int extra_lines = (last_rendered_width_ - 1) / term_width;
        for (int i = 0; i < extra_lines; i++) {
            std::cout << "\x1B[A\x1B[2K";
        }
    }

    std::cout << "\r\x1B[K" << line << std::flush;
    last_rendered_width_ = static_cast<int>(line.size());
    bar_visible_ = true;
}

void ProgressBar::update_title_locked() {
    int done = completed_.load(std::memory_order_relaxed);
    int tot = total_.load(std::memory_order_relaxed);
    std::cout << "\x1B]0;dmake [" << done << "/" << tot << "]\x07" << std::flush;
}

std::string ProgressBar::build_progress_line(int width) const {
    int done = completed_.load(std::memory_order_relaxed);
    int tot = total_.load(std::memory_order_relaxed);

    // Format the ratio string: "3/42"
    std::string ratio = std::to_string(done) + "/" + std::to_string(tot);

    // Build active tasks suffix
    int num_active = static_cast<int>(active_tasks_.size());
    std::string active_suffix;
    if (num_active > 0) {
        // We'll build this after we know our width budget
    }

    // Ultra-narrow: just the ratio
    if (width < 40) {
        return ratio;
    }

    std::string label = "    Building ";  // 13 chars, right-aligned to 12 + space

    // Narrow: no visual bar
    if (width < 60) {
        // "    Building 3/42 (32 active)"
        std::string line = label + ratio;
        if (num_active > 0) {
            line += " (" + std::to_string(num_active) + " active)";
        }
        return line;
    }

    // Normal/wide: full bar + active task names
    constexpr int bar_inner_width = 20;
    // "    Building [====>               ] 3/42"
    //  13 chars    + 1 '[' + 20 bar + 1 '] ' + ratio

    float progress = tot > 0 ? static_cast<float>(done) / static_cast<float>(tot) : 0.0f;
    int filled = static_cast<int>(progress * bar_inner_width);
    if (filled > bar_inner_width) filled = bar_inner_width;

    std::string bar(bar_inner_width, ' ');
    for (int i = 0; i < filled; i++) bar[i] = '=';
    if (filled < bar_inner_width) bar[filled] = '>';

    std::string line = label + "[" + bar + "] " + ratio;

    // Append active task names within remaining width budget
    if (num_active > 0) {
        // Reserve space for possible "(+N more)" suffix
        std::string more_suffix;
        int names_shown = 0;

        // Try to fit task names
        std::string names_part = ": ";
        int budget = width - static_cast<int>(line.size()) - 2;  // -2 for ": "

        for (int i = 0; i < num_active && budget > 0; i++) {
            std::string name = active_tasks_[i];
            int remaining = num_active - (i + 1);

            // Calculate space needed for "(+N more)" if we stop after this name
            std::string remaining_str;
            if (remaining > 0) {
                remaining_str = " (+" + std::to_string(remaining) + " more)";
            }

            std::string separator = (i > 0) ? ", " : "";
            int needed = static_cast<int>(separator.size() + name.size() + remaining_str.size());

            if (needed > budget) {
                // Can't fit this name; show remaining count including this one
                if (names_shown == 0) {
                    names_part = " (" + std::to_string(num_active) + " active)";
                } else {
                    names_part += " (+" + std::to_string(num_active - names_shown) + " more)";
                }
                break;
            }

            names_part += separator + name;
            budget -= static_cast<int>(separator.size() + name.size());
            names_shown++;

            if (i == num_active - 1) {
                // All names fit, no suffix needed
            }
        }

        line += names_part;
    }

    return line;
}

} // namespace dmake
