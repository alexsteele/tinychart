# tinychart

A lightweight C++20 line-chart library for macOS.

## Example

Link your program to the CMake target `chart`.

```cpp
#include <tinychart/macos.hpp>

int main() {
    auto plot = tinychart::makeChart({
        .title = "Price trend",
        .xLabel = "Time (hours)",
        .yLabel = "Price (USD)"
    });
    plot.addLine("Market price", {{0, 12}, {1, 18}, {2, 15}, {3, 24}});
    plot.addScatter({.label = "Observations", .points = {{0.8, 16}, {2.2, 18}}});
    tinychart::writePng(plot, "chart.png");
}
```

![demo chart](examples/sample.png)

## Features

- Line and scatter charts with titles, axis labels, and legends.
- Draws into an AppKit view or exports to PNG.

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
