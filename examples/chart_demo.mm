#import <AppKit/AppKit.h>

#include "sample_chart.hpp"
#include "tinychart/macos.hpp"

@interface ChartView : NSView
@end

@implementation ChartView {
    std::optional<tinychart::Chart> _chart;
    std::optional<tinychart::Frame> _frame;
    std::optional<tinychart::Point> _mouse;
    NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _chart.emplace(sampleChart());
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) {
        [self removeTrackingArea:_tracking];
    }
    _tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited |
                NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
        owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (void)mouseMoved:(NSEvent*)event {
    const auto point = [self convertPoint:event.locationInWindow fromView:nil];
    _mouse = tinychart::Point{point.x, point.y};
    self.needsDisplay = YES;
}

- (void)mouseExited:(NSEvent*)event {
    (void)event;
    _mouse.reset();
    self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    const auto size = self.bounds.size;
    if (size.width <= 0 || size.height <= 0) {
        return;
    }
    if (!_frame || _frame->viewport.width != size.width || _frame->viewport.height != size.height) {
        tinychart::MacTextShaper text;
        _frame = tinychart::buildFrame(_chart->spec(), {size.width, size.height}, text);
    }

    tinychart::Layer overlay;
    if (_mouse && _frame->layout.x) {
        const auto plot = _frame->layout.plot;
        const auto point = *_mouse;
        const auto right = plot.x + plot.width;
        const auto bottom = plot.y + plot.height;
        if (point.x >= plot.x && point.x <= right && point.y >= plot.y && point.y <= bottom) {
            overlay.clip = plot;
            const tinychart::StrokeStyle stroke{{0.20, 0.24, 0.30, 0.48}, 1};
            overlay.commands.emplace_back(
                tinychart::StrokePolyline{{{point.x, plot.y}, {point.x, bottom}}, stroke});
            overlay.commands.emplace_back(
                tinychart::StrokePolyline{{{plot.x, point.y}, {right, point.y}}, stroke});
        }
    }
    tinychart::drawFrame(NSGraphicsContext.currentContext.CGContext, *_frame, &overlay);
}
@end

@interface DemoDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow* window;
@end

@implementation DemoDelegate
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    const auto bounds = NSMakeRect(0, 0, 800, 480);
    self.window = [[NSWindow alloc] initWithContentRect:bounds
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                  NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"tinychart";
    self.window.contentView = [[ChartView alloc] initWithFrame:bounds];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}
@end

int main() {
    @autoreleasepool {
        NSApplication* app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        __attribute__((objc_precise_lifetime)) DemoDelegate* delegate = [DemoDelegate new];
        app.delegate = delegate;
        [app run];
    }
}
