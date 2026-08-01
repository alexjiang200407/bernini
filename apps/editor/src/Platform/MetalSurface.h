#pragma once
#include <QtGui/qwindowdefs.h>

namespace platform
{
	/**
	 * Makes `view` layer-backed with a CAMetalLayer and returns that layer.
	 *
	 * macOS only. `RenderTargetDesc::wnd` is a CAMetalLayer on this platform whoever created the
	 * window, so the conversion from a toolkit's native view lives with the toolkit rather than in
	 * the renderer -- a `void*` meaning two different types depending on the caller is a crash
	 * waiting for the wrong one.
	 *
	 * Must be called on the GUI thread: it touches AppKit.
	 */
	void*
	MetalLayerForView(WId view);
}
