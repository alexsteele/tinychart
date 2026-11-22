#include "tinychart/macos.hpp"

#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tinychart {
namespace {

template<auto release>
struct Releaser {
    template<class T>
    void operator()(T object) const { release(object); }
};

template<class Ref, auto release = CFRelease>
using Owned = std::unique_ptr<std::remove_pointer_t<Ref>, Releaser<release>>;

class GraphicsState {
public:
    explicit GraphicsState(CGContextRef context) : context_(context) {
        CGContextSaveGState(context_);
    }

    ~GraphicsState() { CGContextRestoreGState(context_); }

    GraphicsState(const GraphicsState&) = delete;
    GraphicsState& operator=(const GraphicsState&) = delete;

private:
    CGContextRef context_;
};

class CurrentPath {
public:
    explicit CurrentPath(CGContextRef context)
        : context_(context), path_(CGContextCopyPath(context)) {
        CGContextBeginPath(context_);
    }

    ~CurrentPath() {
        CGContextBeginPath(context_);
        if (path_) {
            CGContextAddPath(context_, path_.get());
        }
    }

    CurrentPath(const CurrentPath&) = delete;
    CurrentPath& operator=(const CurrentPath&) = delete;

private:
    CGContextRef context_;
    Owned<CGPathRef, CGPathRelease> path_;
};

class MacShapedLine final : public ShapedLine {
public:
    explicit MacShapedLine(Owned<CTLineRef> line) : line_(std::move(line)) {}

    TextMetrics metrics() const override {
        TextMetrics metrics{};
        metrics.advance = CTLineGetTypographicBounds(
            line_.get(), &metrics.ascent, &metrics.descent, nullptr);
        return metrics;
    }

    CTLineRef line() const { return line_.get(); }

private:
    Owned<CTLineRef> line_;
};

CGRect nativeRect(Rect rect) {
    return CGRectMake(rect.x, rect.y, rect.width, rect.height);
}

void fillColor(CGContextRef context, Color color) {
    CGContextSetRGBFillColor(context, color.r, color.g, color.b, color.a);
}

void draw(CGContextRef context, const FillRect& command) {
    fillColor(context, command.color);
    CGContextFillRect(context, nativeRect(command.rect));
}

void draw(CGContextRef context, const FillCircles& command) {
    fillColor(context, command.marker.color);
    const auto radius = command.marker.radius;
    const auto diameter = radius * 2;
    for (const auto center : command.centers) {
        CGContextFillEllipseInRect(context, CGRectMake(
            center.x - radius, center.y - radius, diameter, diameter));
    }
}

void draw(CGContextRef context, const StrokePolyline& command) {
    if (command.points.size() < 2) {
        return;
    }
    const auto& color = command.stroke.color;
    CGContextSetRGBStrokeColor(context, color.r, color.g, color.b, color.a);
    CGContextSetLineWidth(context, command.stroke.width);
    CGContextSetLineCap(context, kCGLineCapRound);
    CGContextSetLineJoin(context, kCGLineJoinRound);
    CGContextBeginPath(context);
    CGContextMoveToPoint(context, command.points.front().x, command.points.front().y);
    for (std::size_t i = 1; i < command.points.size(); ++i) {
        CGContextAddLineToPoint(context, command.points[i].x, command.points[i].y);
    }
    CGContextStrokePath(context);
}

void draw(CGContextRef context, const DrawText& command) {
    const auto* line = dynamic_cast<const MacShapedLine*>(command.line.get());
    if (!line) {
        throw std::invalid_argument("Core Graphics playback requires MacTextShaper");
    }

    GraphicsState state(context);
    CGContextTranslateCTM(context, command.baseline.x, command.baseline.y);
    if (command.rotation != 0) {
        CGContextRotateCTM(context, command.rotation);
    }
    CGContextScaleCTM(context, 1, -1);
    const auto textMatrix = CGContextGetTextMatrix(context);
    const auto textPosition = CGContextGetTextPosition(context);
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    CGContextSetTextPosition(context, 0, 0);
    fillColor(context, command.color);
    CTLineDraw(line->line(), context);
    CGContextSetTextMatrix(context, textMatrix);
    CGContextSetTextPosition(context, textPosition.x, textPosition.y);
}

void drawLayer(CGContextRef context, const Layer& layer) {
    GraphicsState state(context);
    if (layer.clip) {
        CGContextClipToRect(context, nativeRect(*layer.clip));
    }
    for (const auto& command : layer.commands) {
        std::visit([context](const auto& item) { draw(context, item); }, command);
    }
}

std::size_t bitmapDimension(double points, double scale) {
    const double pixels = std::ceil(points * scale);
    constexpr auto limit = std::numeric_limits<std::size_t>::max() / 4;
    if (!std::isfinite(points) || points <= 0 || !std::isfinite(pixels) ||
        pixels < 1 || pixels >= static_cast<double>(limit)) {
        throw std::invalid_argument("PNG pixel dimensions must be finite and positive");
    }
    return static_cast<std::size_t>(pixels);
}

}  // namespace

std::shared_ptr<const ShapedLine> MacTextShaper::shape(
    std::string_view text, const TextStyle& style) {
    Owned<CFStringRef> string(CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
    if (!string) {
        throw std::invalid_argument("Could not create UTF-8 chart label");
    }
    Owned<CTFontRef> font(CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, style.size, nullptr));
    if (!font) {
        throw std::runtime_error("Could not create the system chart font");
    }

    const void* keys[]{kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
    const void* values[]{font.get(), kCFBooleanTrue};
    Owned<CFDictionaryRef> attributes(CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks));
    if (!attributes) {
        throw std::runtime_error("Could not create chart label attributes");
    }
    Owned<CFAttributedStringRef> attributed(CFAttributedStringCreate(
        kCFAllocatorDefault, string.get(), attributes.get()));
    if (!attributed) {
        throw std::runtime_error("Could not create attributed chart label");
    }
    Owned<CTLineRef> line(CTLineCreateWithAttributedString(attributed.get()));
    if (!line) {
        throw std::runtime_error("Could not shape chart label");
    }
    return std::make_shared<MacShapedLine>(std::move(line));
}

void drawFrame(CGContextRef context, const Frame& frame, const Layer* overlay) {
    if (!context) {
        throw std::invalid_argument("Drawing requires a graphics context");
    }
    const CurrentPath path(context);
    for (const auto& layer : frame.layers) {
        drawLayer(context, layer);
    }
    if (overlay) {
        drawLayer(context, *overlay);
    }
}

void writePng(const Frame& frame, const std::filesystem::path& path, double scale) {
    if (!std::isfinite(scale) || scale <= 0 || path.empty()) {
        throw std::invalid_argument("PNG output requires a positive scale and a path");
    }
    const auto width = bitmapDimension(frame.viewport.width, scale);
    const auto height = bitmapDimension(frame.viewport.height, scale);
    const auto stride = width * 4;
    if (height > std::numeric_limits<std::size_t>::max() / stride) {
        throw std::invalid_argument("PNG bitmap is too large");
    }

    Owned<CGColorSpaceRef, CGColorSpaceRelease> colorSpace(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    if (!colorSpace) {
        throw std::runtime_error("Could not create the sRGB color space");
    }
    Owned<CGContextRef, CGContextRelease> context(CGBitmapContextCreate(
        nullptr, width, height, 8, stride, colorSpace.get(), kCGImageAlphaPremultipliedLast));
    if (!context) {
        throw std::runtime_error("Could not allocate PNG bitmap");
    }

    CGContextTranslateCTM(context.get(), 0, static_cast<double>(height));
    CGContextScaleCTM(context.get(), scale, -scale);
    drawFrame(context.get(), frame);

    Owned<CGImageRef, CGImageRelease> image(CGBitmapContextCreateImage(context.get()));
    if (!image) {
        throw std::runtime_error("Could not snapshot PNG bitmap");
    }
    const auto& nativePath = path.native();
    Owned<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(nativePath.data()),
        static_cast<CFIndex>(nativePath.size()), false));
    if (!url) {
        throw std::runtime_error("Could not create PNG output URL");
    }
    Owned<CGImageDestinationRef> destination(CGImageDestinationCreateWithURL(
        url.get(), CFSTR("public.png"), 1, nullptr));
    if (!destination) {
        throw std::runtime_error("Could not open PNG output: " + path.string());
    }
    CGImageDestinationAddImage(destination.get(), image.get(), nullptr);
    if (!CGImageDestinationFinalize(destination.get())) {
        throw std::runtime_error("Could not write PNG output: " + path.string());
    }
}

void writePng(const Chart& chart, const std::filesystem::path& path, PngOptions options) {
    MacTextShaper text;
    writePng(buildFrame(chart.spec(), options.size, text), path, options.scale);
}

}  // namespace tinychart
