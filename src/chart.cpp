#include "tinychart/chart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tinychart {
namespace {

constexpr std::array<Color, 4> palette{{
    {0.15, 0.38, 0.78}, {0.80, 0.27, 0.24}, {0.12, 0.52, 0.42}, {0.53, 0.29, 0.73}
}};

Color seriesColor(const ChartSpec& spec) {
    return palette[(spec.series.size() + spec.scatterSeries.size()) % palette.size()];
}

bool positive(double value) {
    return std::isfinite(value) && value > 0;
}

void validateDomain(Domain domain) {
    if (!std::isfinite(domain.min) || !std::isfinite(domain.max) || domain.min >= domain.max) {
        throw std::invalid_argument("Axis domain must have finite, increasing bounds");
    }
}

bool validSample(Sample sample) {
    return std::isfinite(sample.x) && std::isfinite(sample.y);
}

struct DataBounds {
    Domain x{INFINITY, -INFINITY};
    Domain y{INFINITY, -INFINITY};

    void include(const std::vector<Sample>& samples) {
        for (const auto sample : samples) {
            if (validSample(sample)) {
                x = {std::min(x.min, sample.x), std::max(x.max, sample.x)};
                y = {std::min(y.min, sample.y), std::max(y.max, sample.y)};
            }
        }
    }
};

// Keep degenerate data centered whenever the numeric range allows it.
Domain padded(Domain domain) {
    if (domain.min > domain.max) {
        return {0, 1};
    }
    if (domain.min == domain.max) {
        const double value = domain.min;
        const double pad = std::max(1.0, std::abs(value) * 0.05);
        const double limit = std::numeric_limits<double>::max();
        return {std::max(-limit, value - pad), std::min(limit, value + pad)};
    }
    return domain;
}

double fraction(Domain domain, double value) {
    const double span = domain.max - domain.min;
    const double offset = value - domain.min;
    if (std::isfinite(span) && std::isfinite(offset)) {
        return offset / span;
    }
    return (value / 2 - domain.min / 2) / (domain.max / 2 - domain.min / 2);
}

std::vector<double> tickValues(Domain domain, int desired) {
    const double rough = (domain.max - domain.min) / desired;
    if (!positive(rough)) {
        return {domain.min, domain.max};
    }
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    if (!positive(magnitude)) {
        return {domain.min, domain.max};
    }
    const double unit = rough / magnitude;
    double factor = 10;
    for (const double candidate : {1.0, 2.0, 2.5, 5.0}) {
        if (unit <= candidate) {
            factor = candidate;
            break;
        }
    }
    const double step = factor * magnitude;
    const double first = std::ceil(domain.min / step);
    std::vector<double> ticks;
    for (int index = 0; index <= desired + 1; ++index) {
        const double value = (first + index) * step;
        if (!std::isfinite(value) || value > domain.max) {
            break;
        }
        if (value >= domain.min && (ticks.empty() || value > ticks.back())) {
            ticks.push_back(value == 0 ? 0 : value);
        }
    }
    if (ticks.size() < 2) {
        return {domain.min, domain.max};
    }
    return ticks;
}

std::string formatTick(double value, int precision) {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text.precision(precision);
    text << value;
    return text.str();
}

AxisLayout makeAxis(LinearScale scale, int density, const TextStyle& style, TextShaper& text) {
    AxisLayout axis{scale, {}};
    const auto values = tickValues(scale.domain, density);
    std::vector<std::string> labels;
    for (const int precision : {6, 10, 14, std::numeric_limits<double>::max_digits10}) {
        labels.clear();
        for (const double value : values) {
            labels.push_back(formatTick(value, precision));
        }
        if (std::adjacent_find(labels.begin(), labels.end()) == labels.end()) {
            break;
        }
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        axis.ticks.push_back({values[i], {}, text.shape(labels[i], style)});
    }
    return axis;
}

void removeOverlappingLabels(AxisLayout& axis, bool horizontal) {
    constexpr double gap = 4;
    double previousEnd = -std::numeric_limits<double>::infinity();
    std::vector<Tick> visible;
    visible.reserve(axis.ticks.size());
    for (auto& tick : axis.ticks) {
        const auto metrics = tick.label->metrics();
        // Values increase left to right on x and bottom to top on y.
        const double start = horizontal ? tick.baseline.x : -tick.baseline.y - metrics.descent;
        const double end = horizontal ? tick.baseline.x + metrics.advance
                                      : -tick.baseline.y + metrics.ascent;
        if (start >= previousEnd + gap) {
            previousEnd = end;
            visible.push_back(std::move(tick));
        }
    }
    axis.ticks = std::move(visible);
}

ChartLayout makeLayout(const ChartSpec& spec, Size size, TextShaper& text) {
    if (size.width < 80 || size.height < 64) {
        return {};
    }

    DataBounds bounds;
    for (const auto& series : spec.series) {
        bounds.include(series.samples);
    }
    for (const auto& series : spec.scatterSeries) {
        bounds.include(series.samples);
    }
    const Domain xDomain = spec.x.domain.value_or(padded(bounds.x));
    const Domain yDomain = spec.y.domain.value_or(padded(bounds.y));

    constexpr double padding = 10;
    constexpr double labelGap = 8;
    auto y = makeAxis({yDomain, 0, 1},
                      static_cast<int>(std::clamp(size.height / 55, 2.0, 10.0)),
                      spec.y.labels, text);
    double yWidth = 0;
    double yPadding = 0;
    for (const auto& tick : y.ticks) {
        const auto metrics = tick.label->metrics();
        yWidth = std::max(yWidth, metrics.advance);
        yPadding = std::max(yPadding, (metrics.ascent + metrics.descent) / 2);
    }

    const double left = padding + yWidth + labelGap;
    auto x = makeAxis({xDomain, 0, 1},
                      static_cast<int>(std::clamp((size.width - left) / 85, 2.0, 12.0)),
                      spec.x.labels, text);
    double xHeight = 0;
    double xPadding = 0;
    for (const auto& tick : x.ticks) {
        const auto metrics = tick.label->metrics();
        xHeight = std::max(xHeight, metrics.ascent + metrics.descent);
        xPadding = std::max(xPadding, metrics.advance / 2);
    }

    const double plotLeft = std::max(left, padding + xPadding);
    const double top = padding + yPadding;
    const double right = size.width - padding - xPadding;
    const double bottom = size.height - padding - std::max(yPadding, labelGap + xHeight);
    if (right - plotLeft < 24 || bottom - top < 24) {
        return {};
    }

    x.scale.start = plotLeft;
    x.scale.end = right;
    y.scale.start = bottom;
    y.scale.end = top;
    for (auto& tick : x.ticks) {
        const auto metrics = tick.label->metrics();
        tick.baseline = {x.scale.map(tick.value) - metrics.advance / 2,
                         bottom + labelGap + metrics.ascent};
    }
    for (auto& tick : y.ticks) {
        const auto metrics = tick.label->metrics();
        tick.baseline = {plotLeft - labelGap - metrics.advance,
                         y.scale.map(tick.value) + (metrics.ascent - metrics.descent) / 2};
    }
    removeOverlappingLabels(x, true);
    removeOverlappingLabels(y, false);
    return {{plotLeft, top, right - plotLeft, bottom - top}, std::move(x), std::move(y)};
}

void addRule(Layer& layer, Point a, Point b, StrokeStyle style) {
    layer.commands.emplace_back(StrokePolyline{{a, b}, style});
}

enum ClipEdge : unsigned { Left = 1, Right = 2, Below = 4, Above = 8 };

struct ClipBounds {
    Domain x, y;

    unsigned outside(Sample point) const {
        if (point.x < x.min) {
            return Left | verticalEdges(point);
        }
        if (point.x > x.max) {
            return Right | verticalEdges(point);
        }
        return verticalEdges(point);
    }

private:
    unsigned verticalEdges(Sample point) const {
        if (point.y < y.min) {
            return Below;
        }
        if (point.y > y.max) {
            return Above;
        }
        return 0;
    }
};

Domain paddedDomain(const LinearScale& scale, double padding) {
    const auto [start, end] = std::minmax(scale.start, scale.end);
    const auto [min, max] = std::minmax({
        scale.invert(start - padding), scale.invert(end + padding)
    });
    constexpr auto limit = std::numeric_limits<double>::max();
    return {std::clamp(min, -limit, limit), std::clamp(max, -limit, limit)};
}

double interpolateAt(double coordinate, double a, double b, double valueA, double valueB) {
    const double t = fraction({a, b}, coordinate);
    if (t == 0.5 && std::signbit(a) != std::signbit(b)) {
        // Near the midpoint, t can round to 0.5 even when the offset is
        // visible. Interpolate from the midpoint using that offset directly.
        const double halfSpan = b / 2 - a / 2;
        if (halfSpan != 0) {
            const double offset = coordinate - std::midpoint(a, b);
            const auto [min, max] = std::minmax(valueA, valueB);
            const double midpoint = std::midpoint(valueA, valueB);
            const double value = std::fma(offset / halfSpan, valueB / 2 - valueA / 2, midpoint);
            return std::clamp(value, min, max);
        }
    }
    return std::lerp(valueA, valueB, t);
}

// Clip each segment in data coordinates before mapping it to drawing points.
bool clipSegment(const ClipBounds& bounds, Sample& a, Sample& b) {
    for (;;) {
        const auto aEdges = bounds.outside(a);
        const auto bEdges = bounds.outside(b);
        if ((aEdges | bEdges) == 0) {
            return true;
        }
        if (aEdges & bEdges) {
            return false;
        }

        const auto edge = aEdges ? aEdges : bEdges;
        auto& point = aEdges ? a : b;
        if (edge & (Left | Right)) {
            const double x = edge & Left ? bounds.x.min : bounds.x.max;
            point = {x, interpolateAt(x, a.x, b.x, a.y, b.y)};
        } else {
            const double y = edge & Below ? bounds.y.min : bounds.y.max;
            point = {interpolateAt(y, a.y, b.y, a.x, b.x), y};
        }
    }
}

void recordLine(Layer& layer, const LineSeries& series, const AxisLayout& x, const AxisLayout& y) {
    // Keep artificial segment endpoints outside the painted plot, including
    // the round caps and joins. The layer clip still defines the visible edge.
    const double padding = series.stroke.width / 2 + 1;
    const ClipBounds bounds{paddedDomain(x.scale, padding), paddedDomain(y.scale, padding)};
    StrokePolyline line{{}, series.stroke};
    const auto flush = [&] {
        if (line.points.size() >= 2) {
            layer.commands.emplace_back(std::move(line));
        }
        line.points.clear();
    };

    for (std::size_t i = 1; i < series.samples.size(); ++i) {
        auto a = series.samples[i - 1];
        auto b = series.samples[i];
        if (!validSample(a) || !validSample(b) || !clipSegment(bounds, a, b)) {
            flush();
            continue;
        }

        const Point first{x.scale.map(a.x), y.scale.map(a.y)};
        const Point last{x.scale.map(b.x), y.scale.map(b.y)};
        if (line.points.empty() || line.points.back().x != first.x || line.points.back().y != first.y) {
            flush();
            line.points.push_back(first);
        }
        line.points.push_back(last);
    }
    flush();
}

void recordScatter(Layer& layer, const ScatterSeries& series, const AxisLayout& x, const AxisLayout& y) {
    const auto radius = series.marker.radius;
    const ClipBounds bounds{paddedDomain(x.scale, radius), paddedDomain(y.scale, radius)};
    FillCircles circles{{}, series.marker};
    for (const auto sample : series.samples) {
        if (validSample(sample) && bounds.outside(sample) == 0) {
            circles.centers.push_back({x.scale.map(sample.x), y.scale.map(sample.y)});
        }
    }
    if (!circles.centers.empty()) {
        layer.commands.emplace_back(std::move(circles));
    }
}

}  // namespace

Chart::Chart(ChartSpec spec) : spec_(std::move(spec)) {}

Chart& Chart::addLine(std::vector<Sample> samples, std::optional<StrokeStyle> stroke) {
    const StrokeStyle fallback{seriesColor(spec_)};
    spec_.series.push_back({std::move(samples), stroke.value_or(fallback)});
    return *this;
}

Chart& Chart::addScatter(std::vector<Sample> samples, std::optional<MarkerStyle> marker) {
    const MarkerStyle fallback{seriesColor(spec_)};
    spec_.scatterSeries.push_back({std::move(samples), marker.value_or(fallback)});
    return *this;
}

Chart& Chart::setXDomain(Domain domain) {
    validateDomain(domain);
    spec_.x.domain = domain;
    return *this;
}

Chart& Chart::setYDomain(Domain domain) {
    validateDomain(domain);
    spec_.y.domain = domain;
    return *this;
}

Chart lineChart(std::vector<Sample> samples, std::optional<StrokeStyle> stroke) {
    Chart chart(ChartSpec{});
    chart.addLine(std::move(samples), stroke);
    return chart;
}

Chart scatterPlot(std::vector<Sample> samples, std::optional<MarkerStyle> marker) {
    Chart chart(ChartSpec{});
    chart.addScatter(std::move(samples), marker);
    return chart;
}

double LinearScale::map(double value) const {
    double t = fraction(domain, value);
    if (!std::isfinite(t)) {
        t = std::copysign(std::numeric_limits<double>::max(), t);
    }
    return std::lerp(start, end, t);
}

double LinearScale::invert(double position) const {
    return std::lerp(domain.min, domain.max, (position - start) / (end - start));
}

Frame buildFrame(const ChartSpec& spec, Size viewport, TextShaper& text) {
    if (!positive(viewport.width) || !positive(viewport.height)) {
        throw std::invalid_argument("Viewport must have finite, positive dimensions");
    }
    for (const auto* axis : {&spec.x, &spec.y}) {
        if (axis->domain) {
            validateDomain(*axis->domain);
        }
        if (!positive(axis->labels.size)) {
            throw std::invalid_argument("Label size must be positive and finite");
        }
    }
    for (const auto& line : spec.series) {
        if (!positive(line.stroke.width)) {
            throw std::invalid_argument("Stroke width must be positive and finite");
        }
    }
    for (const auto& scatter : spec.scatterSeries) {
        const auto radius = scatter.marker.radius;
        if (!positive(radius) || !std::isfinite(radius * 2)) {
            throw std::invalid_argument("Marker radius must be positive with a finite diameter");
        }
    }

    Frame frame{viewport, makeLayout(spec, viewport, text), {}};
    frame.layers.push_back({{}, {FillRect{{0, 0, viewport.width, viewport.height}, {1, 1, 1}}}});
    if (!frame.layout.x || !frame.layout.y) {
        return frame;
    }

    const auto& plot = frame.layout.plot;
    const auto& x = *frame.layout.x;
    const auto& y = *frame.layout.y;
    const double right = plot.x + plot.width;
    const double bottom = plot.y + plot.height;
    const StrokeStyle gridStroke{{0.89, 0.91, 0.94}, 1};
    const StrokeStyle axisStroke{{0.56, 0.61, 0.68}, 1};
    Layer grid{plot, {}};
    Layer series{plot, {}};
    Layer axes;
    for (const auto& tick : x.ticks) {
        const double position = x.scale.map(tick.value);
        addRule(grid, {position, plot.y}, {position, bottom}, gridStroke);
        axes.commands.emplace_back(DrawText{tick.baseline, tick.label, spec.x.labels.color});
    }
    for (const auto& tick : y.ticks) {
        const double position = y.scale.map(tick.value);
        addRule(grid, {plot.x, position}, {right, position}, gridStroke);
        axes.commands.emplace_back(DrawText{tick.baseline, tick.label, spec.y.labels.color});
    }
    for (const auto& line : spec.series) {
        recordLine(series, line, x, y);
    }
    for (const auto& scatter : spec.scatterSeries) {
        recordScatter(series, scatter, x, y);
    }
    addRule(axes, {plot.x, plot.y}, {plot.x, bottom}, axisStroke);
    addRule(axes, {plot.x, bottom}, {right, bottom}, axisStroke);
    frame.layers.push_back(std::move(grid));
    frame.layers.push_back(std::move(series));
    frame.layers.push_back(std::move(axes));
    return frame;
}

}  // namespace tinychart
