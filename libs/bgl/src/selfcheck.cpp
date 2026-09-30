// The whole point of this translation unit is what it does NOT link. It compiles the public surface
// against bgl_headers alone, so a public header that reaches into src/ stops the build here rather
// than in the first client that includes it.
//
// It proves the *include* closure and nothing else: the public surface declares symbols only a
// renderer defines (CreateGraphics, CookStaticMesh, PreparedStaticMesh's special members), so this
// target must stay a library. An executable would demand them at link time and there would be
// nothing to satisfy it.
#include <bgl/IGraphics.h>

namespace
{
	// Without a definition the archive is empty, which some toolchains warn on.
	[[maybe_unused]] const bgl::GraphicsOptions c_Unused;
}
