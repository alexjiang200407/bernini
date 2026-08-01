#include "Platform/MetalSurface.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

namespace platform
{
	void*
	MetalLayerForView(WId view)
	{
		NSView* nsView = reinterpret_cast<NSView*>(view);
		if (nsView == nil)
			return nullptr;

		CAMetalLayer* layer = [CAMetalLayer layer];

		// Without this the layer renders at point resolution and the drawable comes back smaller
		// than the widget on any Retina display.
		NSWindow* window   = [nsView window];
		layer.contentsScale = window != nil ? [window backingScaleFactor] : 1.0;

		// Order matters: assigning the layer first and then asking for layer-backing keeps this
		// layer, where setting wantsLayer first would have AppKit make one of its own.
		[nsView setLayer:layer];
		[nsView setWantsLayer:YES];

		return (__bridge void*)layer;
	}
}
