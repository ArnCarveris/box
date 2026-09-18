/**
 * @file platform/platform_macos.m
 * @brief Attaches a CAMetalLayer to a Cocoa window so WebGPU can present to it.
 */

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

/** Create a metal layer on the content view of a window.
 *
 * @param ns_window The NSWindow
 * @return The CAMetalLayer
 */
void* box_macos_metal_layer(
    void *ns_window)
{
    NSWindow *window = (__bridge NSWindow*)ns_window;
    NSView *view = [window contentView];
    [view setWantsLayer:YES];
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.contentsScale = [window backingScaleFactor];
    [view setLayer:layer];

    return (__bridge void*)layer;
}

#endif
