#pragma once

// tinychart is a small chart library that renders to MacOS AppKit or PNG.
//
//   auto chart = tinychart::lineChart({{0, 12}, {1, 18}, {2, 15}});
//   tinychart::MacTextShaper text;
//   auto frame = tinychart::buildFrame(chart.spec(), {800, 480}, text);
//   tinychart::writePng(frame, "chart.png");
//
// Use drawFrame with a Core Graphics context to display the same frame.

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tinychart {

// Series use data coordinates. Layout and drawing use logical points.
struct Point { double x, y; };
struct Size { double width, height; };
struct Rect { double x, y, width, height; };
struct Color {
    double r, g, b, a = 1;
    bool operator==(const Color&) const = default;
};
struct Domain { double min, max; };

enum class Theme { Light, MarketDark };

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
    std::string caption{};
};

enum class Interpolation {
    Linear,
    StepAfter,  // Hold the previous y value until the next x.
};

struct LineSeries {
    std::vector<Point> samples;
    StrokeStyle stroke{{0.15, 0.38, 0.78}};
    std::string name;
    Interpolation interpolation = Interpolation::Linear;
};

struct ScatterSeries {
    std::vector<Point> samples;
    MarkerStyle marker;
    std::string name;
};

struct ChartSpec {
    AxisSpec x, y;
    std::vector<LineSeries> series;
    std::vector<ScatterSeries> scatterSeries{};
    std::string title{};
    Theme theme = Theme::Light;
};

struct LineOptions {
    std::string label;
    std::vector<Point> points;
    std::optional<StrokeStyle> stroke;
    Interpolation interpolation = Interpolation::Linear;
};

struct ScatterOptions {
    std::string label;
    std::vector<Point> points;
    std::optional<MarkerStyle> marker;
};

struct ChartOptions {
    std::string title;
    std::string xLabel;
    std::string yLabel;
    Theme theme = Theme::Light;
};

struct PointHit {
    Point data;
    Point position;
    Color color;
    std::string label;
};

class Chart {
public:
    explicit Chart(ChartSpec spec);

    Chart& addLine(std::vector<Point> points);
    Chart& addLine(std::initializer_list<Point> points) {
        return addLine(std::vector<Point>(points));
    }
    Chart& addLine(std::string label, std::vector<Point> points);
    Chart& addLine(LineOptions line);
    Chart& addScatter(std::vector<Point> points);
    Chart& addScatter(std::initializer_list<Point> points) {
        return addScatter(std::vector<Point>(points));
    }
    Chart& addScatter(ScatterOptions scatter);
    Chart& setTitle(std::string title);
    Chart& setXLabel(std::string caption);
    Chart& setYLabel(std::string caption);
    Chart& setXDomain(Domain domain);
    Chart& setYDomain(Domain domain);
    const ChartSpec& spec() const noexcept { return spec_; }

private:
    ChartSpec spec_;
};

Chart makeChart(ChartOptions options);
Chart lineChart(std::vector<Point> points);
inline Chart lineChart(std::initializer_list<Point> points) {
    return lineChart(std::vector<Point>(points));
}
Chart lineChart(LineOptions line);
Chart scatterPlot(std::vector<Point> points);
inline Chart scatterPlot(std::initializer_list<Point> points) {
    return scatterPlot(std::vector<Point>(points));
}
Chart scatterPlot(ScatterOptions scatter);

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
    std::string caption;
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
    double rotation = 0;  // Clockwise radians in chart coordinates.
};

struct FillCircles { std::vector<Point> centers; MarkerStyle marker; };

using PaintCommand = std::variant<FillRect, StrokePolyline, DrawText, FillCircles>;

struct Layer {
    std::optional<Rect> clip;
    std::vector<PaintCommand> commands;
};

struct Frame {
    Frame() = default;
    Frame(Size viewport, ChartLayout layout, std::vector<Layer> layers);

    Size viewport{};
    ChartLayout layout{};
    std::vector<Layer> layers;

    // Cursor and radius use logical points in this frame.
    std::optional<PointHit> nearestPoint(Point cursor, double radius) const;

    // Draws a crosshair and an optional tooltip. Empty when the cursor is outside the plot.
    Layer hoverOverlay(Point cursor, TextShaper& text) const;

private:
    struct HitSample { Point data, position; };
    struct HitSeries {
        std::string label;
        Color color;
        std::vector<HitSample> samples;
    };

    Theme theme_ = Theme::Light;
    std::vector<HitSeries> hits_;

    friend Frame buildFrame(const ChartSpec& spec, Size viewport, TextShaper& text);
};

// Throws std::invalid_argument for invalid dimensions, domains, or styles.
Frame buildFrame(const ChartSpec& spec, Size viewport, TextShaper& text);

}  // namespace tinychart
