#pragma once

#include "tinychart/chart.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <filesystem>

namespace tinychart {

class MacTextShaper final : public TextShaper {
public:
    std::shared_ptr<const ShapedLine> shape(
        std::string_view text, const TextStyle& style) override;
};

struct PngOptions {
    Size size{800, 480};
    double scale = 1;
};

// The context uses top-left coordinates in logical points. Use MacTextShaper
// to build the frame. Playback preserves the context's current path.
void drawFrame(CGContextRef context, const Frame& frame, const Layer* overlay = nullptr);

// Throws std::invalid_argument for invalid options and std::runtime_error
// when an image cannot be created or written.
void writePng(const Frame& frame, const std::filesystem::path& path, double scale = 1);
void writePng(const Chart& chart, const std::filesystem::path& path, PngOptions options = {});

}  // namespace tinychart
