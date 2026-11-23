#import <AppKit/AppKit.h>

#include "sample_chart.hpp"
#include "tinychart/macos.hpp"

@interface ChartView : NSView <NSMenuItemValidation>
- (void)selectTheme:(NSMenuItem*)sender;
@end

@implementation ChartView {
    std::optional<tinychart::Chart> _chart;
    std::optional<tinychart::Frame> _frame;
    std::optional<tinychart::Point> _mouse;
    NSTrackingArea* _tracking;
    NSInteger _appearanceChoice;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }

- (void)selectTheme:(NSMenuItem*)sender {
    _appearanceChoice = sender.tag;
    if (_appearanceChoice == 0) {
        self.window.appearance = nil;
    } else {
        self.window.appearance = [NSAppearance appearanceNamed:
            _appearanceChoice == 1 ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua];
    }
    self.needsDisplay = YES;
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    item.state = item.tag == _appearanceChoice ? NSControlStateValueOn : NSControlStateValueOff;
    return YES;
}

- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    self.needsDisplay = YES;
}

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
    const auto appearance = [self.effectiveAppearance bestMatchFromAppearancesWithNames:
        @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    const auto theme = [appearance isEqualToString:NSAppearanceNameDarkAqua]
        ? tinychart::Theme::MarketDark : tinychart::Theme::Light;
    if (!_chart || _chart->spec().theme != theme) {
        _chart.emplace(sampleChart(theme));
        _frame.reset();
    }

    tinychart::MacTextShaper text;
    if (!_frame || _frame->viewport.width != size.width || _frame->viewport.height != size.height) {
        _frame = tinychart::buildFrame(_chart->spec(), {size.width, size.height}, text);
    }
    const auto overlay = _mouse
        ? _frame->hoverOverlay(*_mouse, text) : tinychart::Layer{};
    tinychart::drawFrame(NSGraphicsContext.currentContext.CGContext, *_frame, _mouse ? &overlay : nullptr);
}
@end

static void installMenu(ChartView* chart) {
    NSMenu* mainMenu = [NSMenu new];
    NSMenuItem* appItem = [mainMenu addItemWithTitle:@"tinychart" action:nil keyEquivalent:@""];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"tinychart"];
    [appMenu addItemWithTitle:@"Quit tinychart" action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;

    NSMenuItem* themeItem = [mainMenu addItemWithTitle:@"Theme" action:nil keyEquivalent:@""];
    NSMenu* themeMenu = [[NSMenu alloc] initWithTitle:@"Theme"];
    NSInteger tag = 0;
    for (NSString* title in @[@"System", @"Light", @"Market Dark"]) {
        NSMenuItem* item = [themeMenu addItemWithTitle:title action:@selector(selectTheme:) keyEquivalent:@""];
        item.target = chart;
        item.tag = tag++;
    }
    themeItem.submenu = themeMenu;
    NSApplication.sharedApplication.mainMenu = mainMenu;
}

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
    ChartView* chart = [[ChartView alloc] initWithFrame:bounds];
    self.window.contentView = chart;
    installMenu(chart);
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
