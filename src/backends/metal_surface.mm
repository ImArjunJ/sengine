#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
namespace sengine {
void* make_metal_layer(void* handle) {
    NSWindow* window = (__bridge NSWindow*)handle;
    NSView* view = window.contentView;
    view.wantsLayer = YES;
    CAMetalLayer* layer = [CAMetalLayer layer];
    view.layer = layer;
    return (__bridge void*)layer;
}
}
