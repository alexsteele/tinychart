#include "tinychart/chart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <locale>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tinychart {
namespace {

struct Palette {
    Color background, grid, axis, title, caption, muted, crosshair, tooltip, tooltipBorder;
    std::array<Color, 4> series;
};

constexpr Palette light{
    .background = {1, 1, 1}, .grid = {0.89, 0.91, 0.94}, .axis = {0.56, 0.61, 0.68},
    .title = {0.12, 0.16, 0.22}, .caption = {0.20, 0.24, 0.30}, .muted = TextStyle{}.color,
    .crosshair = {0.20, 0.24, 0.30, 0.48}, .tooltip = {0.98, 0.99, 1},
    .tooltipBorder = {0.73, 0.77, 0.83},
    .series = {{{0.15, 0.38, 0.78}, {0.80, 0.27, 0.24}, {0.12, 0.52, 0.42}, {0.53, 0.29, 0.73}}}
};

constexpr Palette marketDark{
    .background = {0.06, 0.09, 0.13}, .grid = {0.14, 0.20, 0.27}, .axis = {0.32, 0.39, 0.49},
    .title = {0.92, 0.95, 0.99}, .caption = {0.76, 0.82, 0.90}, .muted = {0.61, 0.69, 0.80},
    .crosshair = {0.62, 0.73, 0.87, 0.64}, .tooltip = {0.10, 0.15, 0.21},
    .tooltipBorder = {0.32, 0.43, 0.55},
    .series = {{{0.36, 0.63, 1}, {1, 0.68, 0.35}, {0.28, 0.86, 0.72}, {0.77, 0.63, 1}}}
};

const Palette& paletteFor(Theme theme) {
    return theme == Theme::MarketDark ? marketDark : light;
}

Color seriesColor(const ChartSpec& spec) {
    const auto& colors = paletteFor(spec.theme).series;
    return colors[(spec.series.size() + spec.scatterSeries.size() + spec.barSeries.size()) % colors.size()];
}

TextStyle tickStyle(TextStyle style, const Palette& palette) {
    if (style.color == TextStyle{}.color) {
        style.color = palette.muted;
    }
    return style;
}

bool positive(double value) {
    return std::isfinite(value) && value > 0;
}

void validateDomain(Domain domain) {
    if (!std::isfinite(domain.min) || !std::isfinite(domain.max) || domain.min >= domain.max) {
        throw std::invalid_argument("Axis domain must have finite, increasing bounds");
    }
}

bool validSample(Point sample) {
    return std::isfinite(sample.x) && std::isfinite(sample.y);
}

bool contains(Rect rect, Point point) {
    return point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

struct DataBounds {
    Domain x{INFINITY, -INFINITY};
    Domain y{INFINITY, -INFINITY};

    void include(const std::vector<Point>& samples) {
        for (const auto sample : samples) {
            if (validSample(sample)) {
                x = {std::min(x.min, sample.x), std::max(x.max, sample.x)};
                y = {std::min(y.min, sample.y), std::max(y.max, sample.y)};
            }
        }
    }

    void includeBars(const std::vector<double>& values) {
        if (values.empty()) {
            return;
        }
        x.min = std::min(x.min, -0.5);
        x.max = std::max(x.max, static_cast<double>(values.size()) - 0.5);
        for (const double value : values) {
            if (std::isfinite(value)) {
                y.min = std::min({y.min, value, 0.0});
                y.max = std::max({y.max, value, 0.0});
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

std::vector<double> tickValues(Domain domain, int desired, bool wholeNumbers) {
    const double interval = (domain.max - domain.min) / desired;
    if (!positive(interval)) {
        return {domain.min, domain.max};
    }
    const double rough = wholeNumbers ? std::max(1.0, interval) : interval;
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    if (!positive(magnitude)) {
        return {domain.min, domain.max};
    }
    const double unit = rough / magnitude;
    double factor = 10;
    for (const double candidate : {1.0, 2.0, 2.5, 5.0}) {
        if (wholeNumbers && magnitude == 1 && candidate == 2.5) {
            continue;
        }
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
    // A one-slot bar chart only needs its center tick.
    if (wholeNumbers && ticks.empty()) {
        return tickValues(domain, desired, false);
    }
    if (!wholeNumbers && ticks.size() < 2) {
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

AxisLayout makeAxis(LinearScale scale, int density, const TextStyle& style, TextShaper& text,
                    bool wholeNumbers = false) {
    AxisLayout axis{.scale = scale};
    const auto values = tickValues(scale.domain, density, wholeNumbers);
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

constexpr double padding = 10;
constexpr double labelGap = 8;

struct Label {
    std::shared_ptr<const ShapedLine> line;
    TextMetrics metrics{};

    explicit operator bool() const { return static_cast<bool>(line); }
    double height() const { return metrics.ascent + metrics.descent; }
};

Label fitLabel(std::string_view value, TextStyle style, TextShaper& text, double width) {
    if (value.empty() || width <= 0) {
        return {};
    }
    const auto measure = [&](std::string_view label) {
        auto line = text.shape(label, style);
        return Label{line, line->metrics()};
    };
    auto label = measure(value);
    if (label.metrics.advance <= width) {
        return label;
    }

    auto best = measure("…");
    if (best.metrics.advance > width) {
        return {};
    }
    std::vector<std::size_t> cuts;
    for (std::size_t i = 1; i < value.size(); ++i) {
        if ((static_cast<unsigned char>(value[i]) & 0xc0) != 0x80) {
            cuts.push_back(i);
        }
    }
    std::size_t first = 0;
    std::size_t last = cuts.size();
    while (first < last) {
        const auto middle = std::midpoint(first, last);
        auto candidate = measure(std::string(value.substr(0, cuts[middle])) + "…");
        if (candidate.metrics.advance <= width) {
            best = std::move(candidate);
            first = middle + 1;
        } else {
            last = middle;
        }
    }
    return best;
}

constexpr double swatchWidth = 18;
constexpr double swatchGap = 6;
constexpr double legendItemGap = 18;
constexpr double legendRowGap = 5;

struct LegendItem {
    Label label;
    std::variant<StrokeStyle, MarkerStyle, Color> swatch;

    double width() const { return swatchWidth + swatchGap + label.metrics.advance; }
};

struct LegendRow {
    std::vector<LegendItem> items;
    double width = 0;
    double height = 0;
};

std::vector<LegendRow> makeLegend(const ChartSpec& spec, TextShaper& text, double width,
                                  TextStyle style) {
    std::vector<LegendRow> rows;
    const auto append = [&](std::string_view name, auto swatch) {
        auto label = fitLabel(name, style, text, width - swatchWidth - swatchGap);
        if (!label) {
            return;
        }
        LegendItem item{std::move(label), swatch};
        if (rows.empty() || rows.back().width + legendItemGap + item.width() > width) {
            rows.emplace_back();
        }
        auto& row = rows.back();
        row.width += (row.items.empty() ? 0 : legendItemGap) + item.width();
        row.height = std::max({row.height, item.label.height(), 8.0});
        row.items.push_back(std::move(item));
    };
    for (const auto& line : spec.series) {
        append(line.name, StrokeStyle{line.stroke.color, std::min(line.stroke.width, 3.0)});
    }
    for (const auto& scatter : spec.scatterSeries) {
        append(scatter.name, MarkerStyle{scatter.marker.color, std::min(scatter.marker.radius, 4.0)});
    }
    for (const auto& bars : spec.barSeries) {
        append(bars.name, bars.color);
    }
    return rows;
}

void recordLegend(Layer& layer, const std::vector<LegendRow>& rows, double width, double top,
                  Color color) {
    for (const auto& row : rows) {
        double left = (width - row.width) / 2;
        const double centerY = top + row.height / 2;
        for (const auto& item : row.items) {
            if (const auto* stroke = std::get_if<StrokeStyle>(&item.swatch)) {
                layer.commands.emplace_back(StrokePolyline{
                    {{left, centerY}, {left + swatchWidth, centerY}}, *stroke});
            } else if (const auto* marker = std::get_if<MarkerStyle>(&item.swatch)) {
                layer.commands.emplace_back(FillCircles{
                    {{left + swatchWidth / 2, centerY}}, *marker});
            } else {
                layer.commands.emplace_back(FillRect{
                    {left + swatchWidth / 2 - 4, centerY - 4, 8, 8}, std::get<Color>(item.swatch)});
            }
            const auto metrics = item.label.metrics;
            const Point baseline{left + swatchWidth + swatchGap,
                                 centerY + (metrics.ascent - metrics.descent) / 2};
            layer.commands.emplace_back(DrawText{baseline, item.label.line, color});
            left += item.width() + legendItemGap;
        }
        top += row.height + legendRowGap;
    }
}

ChartLayout makeLayout(const ChartSpec& spec, Size size, TextShaper& text, Layer& annotations,
                       bool decorate = true) {
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
    for (const auto& series : spec.barSeries) {
        bounds.includeBars(series.values);
    }
    const Domain xDomain = spec.x.domain.value_or(padded(bounds.x));
    const Domain yDomain = spec.y.domain.value_or(padded(bounds.y));

    const auto& palette = paletteFor(spec.theme);
    const TextStyle titleStyle{20, palette.title};
    const TextStyle captionStyle{13, palette.caption};
    const TextStyle legendStyle{12, palette.muted};
    const auto title = decorate ? fitLabel(spec.title, titleStyle, text, size.width - 2 * padding)
                                : Label{};
    const auto yCaption = decorate ? fitLabel(spec.y.caption, captionStyle, text, size.height)
                                   : Label{};
    const double above = title ? title.height() + labelGap : 0;
    const double beside = yCaption ? yCaption.height() + labelGap : 0;
    auto y = makeAxis({yDomain, 0, 1},
                      static_cast<int>(std::clamp((size.height - above) / 55, 2.0, 10.0)),
                      tickStyle(spec.y.labels, palette), text);
    double yWidth = 0;
    double yPadding = 0;
    for (const auto& tick : y.ticks) {
        const auto metrics = tick.label->metrics();
        yWidth = std::max(yWidth, metrics.advance);
        yPadding = std::max(yPadding, (metrics.ascent + metrics.descent) / 2);
    }

    const double left = padding + beside + yWidth + labelGap;
    auto x = makeAxis({xDomain, 0, 1},
                      static_cast<int>(std::clamp((size.width - left) / 85, 2.0, 12.0)),
                      tickStyle(spec.x.labels, palette), text, !spec.barSeries.empty());
    double xHeight = 0;
    double xPadding = 0;
    for (const auto& tick : x.ticks) {
        const auto metrics = tick.label->metrics();
        xHeight = std::max(xHeight, metrics.ascent + metrics.descent);
        xPadding = std::max(xPadding, metrics.advance / 2);
    }

    const double plotLeft = std::max(left, padding + xPadding);
    const double top = padding + above + yPadding;
    const double right = size.width - padding - xPadding;
    const auto xCaption = decorate ? fitLabel(spec.x.caption, captionStyle, text, right - plotLeft)
                                   : Label{};
    const double below = labelGap + xHeight + (xCaption ? labelGap + xCaption.height() : 0);
    double bottom = size.height - padding - std::max(yPadding, below);
    if (right - plotLeft < 24 || bottom - top < 24) {
        if (title || xCaption || yCaption) {
            return makeLayout(spec, size, text, annotations, false);
        }
        return {};
    }

    if (decorate) {
        const auto legend = makeLegend(spec, text, size.width - 2 * padding, legendStyle);
        double legendSpace = 0;
        for (const auto& row : legend) {
            legendSpace += row.height + legendRowGap;
        }
        if (!legend.empty()) {
            const double legendHeight = legendSpace - legendRowGap;
            if (bottom - top >= legendHeight + labelGap + 24) {
                bottom -= legendHeight + labelGap;
                recordLegend(annotations, legend, size.width, size.height - padding - legendHeight,
                             legendStyle.color);
            }
        }
    }

    if (title) {
        const Point baseline{(size.width - title.metrics.advance) / 2, padding + title.metrics.ascent};
        annotations.commands.emplace_back(DrawText{baseline, title.line, titleStyle.color});
    }
    if (xCaption) {
        const Point baseline{(plotLeft + right - xCaption.metrics.advance) / 2,
                             bottom + 2 * labelGap + xHeight + xCaption.metrics.ascent};
        annotations.commands.emplace_back(DrawText{baseline, xCaption.line, captionStyle.color});
    }
    if (yCaption) {
        const auto caption = yCaption.metrics.advance <= bottom - top
            ? yCaption : fitLabel(spec.y.caption, captionStyle, text, bottom - top);
        if (caption) {
            const Point baseline{plotLeft - 2 * labelGap - yWidth - caption.metrics.descent,
                                 (top + bottom + caption.metrics.advance) / 2};
            annotations.commands.emplace_back(DrawText{
                baseline, caption.line, captionStyle.color, -std::numbers::pi / 2});
        }
    }

    x.scale.start = plotLeft;
    x.scale.end = right;
    y.scale.start = bottom;
    y.scale.end = top;
    x.caption = spec.x.caption;
    y.caption = spec.y.caption;
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

    unsigned outside(Point point) const {
        if (point.x < x.min) {
            return Left | verticalEdges(point);
        }
        if (point.x > x.max) {
            return Right | verticalEdges(point);
        }
        return verticalEdges(point);
    }

private:
    unsigned verticalEdges(Point point) const {
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
bool clipSegment(const ClipBounds& bounds, Point& a, Point& b) {
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

    const auto append = [&](Point a, Point b) {
        if (!clipSegment(bounds, a, b)) {
            flush();
            return;
        }

        const Point first{x.scale.map(a.x), y.scale.map(a.y)};
        const Point last{x.scale.map(b.x), y.scale.map(b.y)};
        if (line.points.empty() || line.points.back().x != first.x || line.points.back().y != first.y) {
            flush();
            line.points.push_back(first);
        }
        line.points.push_back(last);
    };

    for (std::size_t i = 1; i < series.samples.size(); ++i) {
        const auto a = series.samples[i - 1];
        const auto b = series.samples[i];
        if (!validSample(a) || !validSample(b)) {
            flush();
            continue;
        }

        if (series.interpolation == Interpolation::StepAfter && a.x != b.x && a.y != b.y) {
            const Point corner{b.x, a.y};
            append(a, corner);
            append(corner, b);
        } else {
            append(a, b);
        }
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

std::optional<Rect> visibleBar(Point sample, const LinearScale& x, const LinearScale& y) {
    constexpr double halfWidth = 0.375;
    const double left = std::max(sample.x - halfWidth, x.domain.min);
    const double right = std::min(sample.x + halfWidth, x.domain.max);
    const double low = std::max(std::min(0.0, sample.y), y.domain.min);
    const double high = std::min(std::max(0.0, sample.y), y.domain.max);
    if (left >= right || low >= high) {
        return std::nullopt;
    }
    const double x0 = x.map(left);
    const double y0 = y.map(high);
    const double width = x.map(right) - x0;
    const double height = y.map(low) - y0;
    if (!positive(width) || !positive(height)) {
        return std::nullopt;
    }
    return Rect{x0, y0, width, height};
}

void recordTooltip(Layer& layer, const ChartLayout& layout, const PointHit& hit,
                   const Palette& palette, TextShaper& text) {
    constexpr double inset = 4;
    constexpr double pad = 9;
    constexpr double gap = 3;
    constexpr double offset = 12;
    const auto plot = layout.plot;
    const double textWidth = std::min(240.0, plot.width - 2 * inset) - 2 * pad;
    const TextStyle headingStyle{12, palette.title};
    const TextStyle valueStyle{11, palette.caption};
    const auto heading = fitLabel(hit.label, headingStyle, text, textWidth);
    const auto valueLabel = [&](const AxisLayout& axis, std::string_view axisName, double value) {
        const auto caption = axis.caption.empty() ? axisName : std::string_view(axis.caption);
        return fitLabel(std::string(caption) + "  " + formatTick(value, 7), valueStyle, text, textWidth);
    };
    const auto xLabel = valueLabel(*layout.x, "x", hit.data.x);
    const auto yLabel = valueLabel(*layout.y, "y", hit.data.y);
    if (!heading || !xLabel || !yLabel) {
        return;
    }

    const double width = std::max({heading.metrics.advance, xLabel.metrics.advance,
                                   yLabel.metrics.advance}) + 2 * pad;
    const double height = heading.height() + xLabel.height() + yLabel.height() + 2 * gap + 2 * pad;
    if (height > plot.height - 2 * inset) {
        return;
    }

    const double right = plot.x + plot.width - inset;
    const double bottom = plot.y + plot.height - inset;
    const auto [x, y] = hit.position;
    const double left = std::clamp(x + offset + width <= right ? x + offset : x - offset - width,
                                   plot.x + inset, right - width);
    const double top = std::clamp(y - offset - height >= plot.y + inset ? y - offset - height : y + offset,
                                  plot.y + inset, bottom - height);
    layer.commands.emplace_back(FillRect{{left, top, width, height}, palette.tooltipBorder});
    layer.commands.emplace_back(FillRect{{left + 1, top + 1, width - 2, height - 2}, palette.tooltip});
    layer.commands.emplace_back(DrawText{{left + pad, top + pad + heading.metrics.ascent},
                                         heading.line, headingStyle.color});
    const double valueTop = top + pad + heading.height() + gap;
    layer.commands.emplace_back(DrawText{{left + pad, valueTop + xLabel.metrics.ascent},
                                         xLabel.line, valueStyle.color});
    layer.commands.emplace_back(DrawText{{left + pad, valueTop + xLabel.height() + gap + yLabel.metrics.ascent},
                                         yLabel.line, valueStyle.color});
}

}  // namespace

Chart::Chart(ChartSpec spec) : spec_(std::move(spec)) {}

Chart& Chart::addLine(std::vector<Point> points) {
    return addLine(LineOptions{.points = std::move(points)});
}

Chart& Chart::addLine(std::string label, std::vector<Point> points) {
    return addLine(LineOptions{.label = std::move(label), .points = std::move(points)});
}

Chart& Chart::addLine(LineOptions line) {
    const StrokeStyle fallback{seriesColor(spec_)};
    spec_.series.push_back({std::move(line.points), line.stroke.value_or(fallback),
                            std::move(line.label), line.interpolation});
    return *this;
}

Chart& Chart::addScatter(std::vector<Point> points) {
    return addScatter(ScatterOptions{.points = std::move(points)});
}

Chart& Chart::addScatter(ScatterOptions scatter) {
    const MarkerStyle fallback{seriesColor(spec_)};
    spec_.scatterSeries.push_back({std::move(scatter.points), scatter.marker.value_or(fallback),
                                   std::move(scatter.label)});
    return *this;
}

Chart& Chart::addBars(std::vector<double> values) {
    return addBars({}, std::move(values));
}

Chart& Chart::addBars(std::string label, std::vector<double> values) {
    const auto color = seriesColor(spec_);
    spec_.barSeries.push_back({std::move(values), color, std::move(label)});
    return *this;
}

Chart& Chart::setTitle(std::string title) {
    spec_.title = std::move(title);
    return *this;
}

Chart& Chart::setXLabel(std::string caption) {
    spec_.x.caption = std::move(caption);
    return *this;
}

Chart& Chart::setYLabel(std::string caption) {
    spec_.y.caption = std::move(caption);
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

Chart makeChart(ChartOptions options) {
    ChartSpec spec;
    spec.title = std::move(options.title);
    spec.x.caption = std::move(options.xLabel);
    spec.y.caption = std::move(options.yLabel);
    spec.theme = options.theme;
    return Chart(std::move(spec));
}

Chart lineChart(std::vector<Point> points) {
    return lineChart(LineOptions{.points = std::move(points)});
}

Chart lineChart(LineOptions line) {
    Chart chart(ChartSpec{});
    chart.addLine(std::move(line));
    return chart;
}

Chart scatterPlot(std::vector<Point> points) {
    return scatterPlot(ScatterOptions{.points = std::move(points)});
}

Chart scatterPlot(ScatterOptions scatter) {
    Chart chart(ChartSpec{});
    chart.addScatter(std::move(scatter));
    return chart;
}

Chart barChart(std::vector<double> values) {
    Chart chart(ChartSpec{});
    chart.addBars(std::move(values));
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

Frame::Frame(Size viewport, ChartLayout layout, std::vector<Layer> layers)
    : viewport(viewport), layout(std::move(layout)), layers(std::move(layers)) {}

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

    const auto& palette = paletteFor(spec.theme);
    Layer axes;
    Frame frame{viewport, makeLayout(spec, viewport, text, axes), {}};
    frame.theme_ = spec.theme;
    frame.layers.push_back({{}, {FillRect{{0, 0, viewport.width, viewport.height}, palette.background}}});
    if (!frame.layout.x || !frame.layout.y) {
        return frame;
    }

    const auto& plot = frame.layout.plot;
    const auto& x = *frame.layout.x;
    const auto& y = *frame.layout.y;
    const double right = plot.x + plot.width;
    const double bottom = plot.y + plot.height;
    const StrokeStyle gridStroke{palette.grid, 1};
    const StrokeStyle axisStroke{palette.axis, 1};
    const auto xLabelColor = tickStyle(spec.x.labels, palette).color;
    const auto yLabelColor = tickStyle(spec.y.labels, palette).color;
    Layer grid{plot, {}};
    Layer series{plot, {}};
    for (const auto& tick : x.ticks) {
        const double position = x.scale.map(tick.value);
        addRule(grid, {position, plot.y}, {position, bottom}, gridStroke);
        axes.commands.emplace_back(DrawText{tick.baseline, tick.label, xLabelColor});
    }
    for (const auto& tick : y.ticks) {
        const double position = y.scale.map(tick.value);
        addRule(grid, {plot.x, position}, {right, position}, gridStroke);
        axes.commands.emplace_back(DrawText{tick.baseline, tick.label, yLabelColor});
    }
    if (!spec.barSeries.empty() && y.scale.domain.min < 0 && y.scale.domain.max > 0) {
        const double zero = y.scale.map(0);
        addRule(grid, {plot.x, zero}, {right, zero}, axisStroke);
    }
    for (const auto& bars : spec.barSeries) {
        Frame::HitSeries hits{bars.name.empty() ? "Bar series" : bars.name, bars.color, {}, {}};
        for (std::size_t i = 0; i < bars.values.size(); ++i) {
            const Point sample{static_cast<double>(i), bars.values[i]};
            if (!validSample(sample)) {
                continue;
            }
            const auto rect = visibleBar(sample, x.scale, y.scale);
            if (!rect) {
                continue;
            }
            series.commands.emplace_back(FillRect{*rect, bars.color});
            const Point position{x.scale.map(std::clamp(sample.x, x.scale.domain.min, x.scale.domain.max)),
                                 y.scale.map(std::clamp(sample.y, y.scale.domain.min, y.scale.domain.max))};
            hits.bars.push_back({{sample, position}, *rect});
        }
        if (!hits.bars.empty()) {
            frame.hits_.push_back(std::move(hits));
        }
    }
    const auto recordHits = [&](const std::vector<Point>& samples, std::string_view label, Color color,
                                bool requiresSegment) {
        Frame::HitSeries hits{std::string(label), color, {}, {}};
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto sample = samples[i];
            if (!validSample(sample)) {
                continue;
            }
            if (requiresSegment) {
                const bool hasPrevious = i > 0 && validSample(samples[i - 1]);
                const bool hasNext = i + 1 < samples.size() && validSample(samples[i + 1]);
                if (!hasPrevious && !hasNext) {
                    continue;
                }
            }
            const Point position{x.scale.map(sample.x), y.scale.map(sample.y)};
            if (contains(plot, position)) {
                hits.samples.push_back({sample, position});
            }
        }
        if (!hits.samples.empty()) {
            frame.hits_.push_back(std::move(hits));
        }
    };
    for (const auto& line : spec.series) {
        recordLine(series, line, x, y);
        recordHits(line.samples, line.name.empty() ? std::string_view("Line series") : line.name,
                   line.stroke.color, true);
    }
    for (const auto& scatter : spec.scatterSeries) {
        recordScatter(series, scatter, x, y);
        recordHits(scatter.samples, scatter.name.empty() ? std::string_view("Scatter series") : scatter.name,
                   scatter.marker.color, false);
    }
    addRule(axes, {plot.x, plot.y}, {plot.x, bottom}, axisStroke);
    addRule(axes, {plot.x, bottom}, {right, bottom}, axisStroke);
    frame.layers.push_back(std::move(grid));
    frame.layers.push_back(std::move(series));
    frame.layers.push_back(std::move(axes));
    return frame;
}

std::optional<PointHit> Frame::nearestPoint(Point cursor, double radius) const {
    if (!layout.x || !layout.y || !contains(layout.plot, cursor) ||
        !std::isfinite(radius) || radius < 0) {
        return std::nullopt;
    }

    const HitSeries* nearestSeries = nullptr;
    const HitSample* nearestSample = nullptr;
    const auto consider = [&](const HitSeries& series, const HitSample& sample, bool insideBar) {
        const double distance = insideBar ? 0 : std::hypot(sample.position.x - cursor.x,
                                                           sample.position.y - cursor.y);
        if (distance <= radius) {
            nearestSeries = &series;
            nearestSample = &sample;
            radius = distance;
        }
    };
    for (const auto& series : hits_) {
        for (const auto& sample : series.samples) {
            consider(series, sample, false);
        }
        for (const auto& bar : series.bars) {
            consider(series, bar.sample, contains(bar.bounds, cursor));
        }
    }
    if (!nearestSample) {
        return std::nullopt;
    }
    return PointHit{nearestSample->data, nearestSample->position, nearestSeries->color, nearestSeries->label};
}

Layer Frame::hoverOverlay(Point cursor, TextShaper& text) const {
    const auto plot = layout.plot;
    if (!layout.x || !layout.y || !contains(plot, cursor)) {
        return {};
    }

    const auto& palette = paletteFor(theme_);
    const auto hit = nearestPoint(cursor, 12);
    const auto point = hit ? hit->position : cursor;
    Layer overlay{plot, {}};
    const StrokeStyle crosshair{palette.crosshair, 1};
    addRule(overlay, {point.x, plot.y}, {point.x, plot.y + plot.height}, crosshair);
    addRule(overlay, {plot.x, point.y}, {plot.x + plot.width, point.y}, crosshair);
    if (hit) {
        overlay.commands.emplace_back(FillCircles{{point}, {palette.background, 5}});
        overlay.commands.emplace_back(FillCircles{{point}, {hit->color, 3.5}});
        recordTooltip(overlay, layout, *hit, palette, text);
    }
    return overlay;
}

}  // namespace tinychart
