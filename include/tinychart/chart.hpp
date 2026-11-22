#pragma once

// tinychart lays out line and scatter plots in logical points.
// For an AppKit view or a PNG on macOS, include <tinychart/macos.hpp>:
//
//   auto chart = tinychart::lineChart({{0, 12}, {1, 18}, {2, 15}});
//   tinychart::MacTextShaper text;
//   auto frame = tinychart::buildFrame(chart.spec(), {800, 480}, text);
//   tinychart::writePng(frame, "chart.png");
//
// Use drawFrame with a Core Graphics context to display the same frame.

#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace tinychart {

// Series use data coordinates. Layout and drawing use logical points.
struct Point { double x, y; };
struct Size { double width, height; };
struct Rect { double x, y, width, height; };
struct Color { double r, g, b, a = 1; };
struct Domain { double min, max; };

struct StrokeStyle {
    Color color;
    double width = 2;
};

struct MarkerStyle {
    Color color{0.15, 0.38, 0.78};
    double radius = 3;  // Logical points.
};

struct TextStyle {
    double size = 12;
    Color color{0.25, 0.29, 0.35};
};

struct AxisSpec {
    std::optional<Domain> domain;
    TextStyle labels;
};

struct LineSeries {
    std::vector<Point> samples;
    StrokeStyle stroke{{0.15, 0.38, 0.78}};
};

struct ScatterSeries {
    std::vector<Point> samples;
    MarkerStyle marker;
};

struct ChartSpec {
    AxisSpec x, y;
    std::vector<LineSeries> series;
    std::vector<ScatterSeries> scatterSeries{};
};

class Chart {
public:
    explicit Chart(ChartSpec spec);

    Chart& addLine(std::vector<Point> samples, std::optional<StrokeStyle> stroke = {});
    Chart& addScatter(std::vector<Point> samples, std::optional<MarkerStyle> marker = {});
    Chart& setXDomain(Domain domain);
    Chart& setYDomain(Domain domain);
    const ChartSpec& spec() const noexcept { return spec_; }

private:
    ChartSpec spec_;
};

Chart lineChart(std::vector<Point> samples, std::optional<StrokeStyle> stroke = {});
Chart scatterPlot(std::vector<Point> samples, std::optional<MarkerStyle> marker = {});

struct LinearScale {
    Domain domain;
    double start, end;

    double map(double value) const;
    double invert(double position) const;
};

struct TextMetrics {
    double advance, ascent, descent;
};

class ShapedLine {
public:
    virtual ~ShapedLine() = default;
    virtual TextMetrics metrics() const = 0;
};

class TextShaper {
public:
    virtual ~TextShaper() = default;
    virtual std::shared_ptr<const ShapedLine> shape(
        std::string_view text, const TextStyle& style) = 0;
};

struct Tick {
    double value;
    Point baseline;
    std::shared_ptr<const ShapedLine> label;
};

struct AxisLayout {
    LinearScale scale;
    std::vector<Tick> ticks;
};

struct ChartLayout {
    Rect plot;
    std::optional<AxisLayout> x, y;
};

struct FillRect { Rect rect; Color color; };
struct StrokePolyline { std::vector<Point> points; StrokeStyle stroke; };
struct DrawText {
    Point baseline;
    std::shared_ptr<const ShapedLine> line;
    Color color;
};

struct FillCircles { std::vector<Point> centers; MarkerStyle marker; };

using PaintCommand = std::variant<FillRect, StrokePolyline, DrawText, FillCircles>;

struct Layer {
    std::optional<Rect> clip;
    std::vector<PaintCommand> commands;
};

struct Frame {
    Size viewport;
    ChartLayout layout;
    std::vector<Layer> layers;
};

// Throws std::invalid_argument for invalid dimensions, domains, or styles.
Frame buildFrame(const ChartSpec& spec, Size viewport, TextShaper& text);

}  // namespace tinychart
