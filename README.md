# tinychart

A lightweight C++20 line-chart library for macOS.

![Sample tinychart plot with two lines and scatter markers](examples/sample.png)

## Quick example

Link your program to the CMake target `chart`.

```cpp
#include <tinychart/macos.hpp>

int main() {
    auto plot = tinychart::lineChart({{0, 12}, {1, 18}, {2, 15}, {3, 24}});
    plot.addScatter({{0.8, 16}, {2.2, 18}});
    tinychart::writePng(plot, "chart.png", {.size = {800, 480}, .scale = 2});
}
```

Use `scatterPlot(samples)` for a chart with just points.

## Features

- Line and scatter series on shared axes, with configurable strokes and marker sizes.
- Automatic axis ranges, optional explicit domains, and plot clipping.
- Non-finite samples break lines and are skipped in scatter series.
- PNG export at a chosen scale and a resizable AppKit demo with a hover crosshair.

## How it works

- `lineChart` and `scatterPlot` build a `ChartSpec` from samples and styles.
- `buildFrame` lays out the chart and records drawing commands in a `Frame`.
- `drawFrame` uses Core Graphics to draw the frame.

## Draw in an AppKit view

In an Objective-C++ `NSView`, include `<tinychart/macos.hpp>` and store your chart in an `std::optional<tinychart::Chart>` named `chart_`.

```objective-c++
- (BOOL)isFlipped { return YES; }

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const auto size = self.bounds.size;
    if (!chart_ || size.width <= 0 || size.height <= 0) return;

    tinychart::MacTextShaper text;
    auto frame = tinychart::buildFrame(chart_->spec(), {size.width, size.height}, text);
    tinychart::drawFrame(NSGraphicsContext.currentContext.CGContext, frame);
}
```

Set `needsDisplay` when your data changes. If the view redraws frequently, cache the frame and rebuild it when the data or view size changes, as in the [AppKit demo](examples/chart_demo.mm).

## Build and run

Requires Apple Command Line Tools, CMake 3.24 or later, and Ninja.

```sh
brew install cmake ninja
cmake --preset dev
cmake --build --preset dev
./build/dev/render_chart build/dev/sample.png
./build/dev/render_chart build/dev/sample@2x.png 2
open build/dev/chart_demo.app
```
