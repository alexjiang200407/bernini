// The whole point of this translation unit is what it does NOT link. It compiles the public surface
// against bgl alone, so a public header that reaches into libs/bgl_extended/src stops the build here
// rather than when a second renderer is written.
//
// It proves the *include* closure and nothing else: bgl.h declares symbols only a renderer defines
// (CreateGraphics, CookStaticMesh, PreparedStaticMesh's special members), so this target must stay a
// library. An executable would demand them at link time and there would be nothing to satisfy it.
#include <bgl/bgl.h>

namespace
{
	// Without a definition the archive is empty, which some toolchains warn on.
	[[maybe_unused]] const bgl::GraphicsOptions c_Unused;
}
