// The whole point of this translation unit is what it does NOT link. It compiles the public surface
// against assetlib_structs alone, so a header that reaches into assetlib -- for a codec, a store, or
// a question one of these containers cannot answer for itself -- stops the build here rather than
// when a renderer that links this and not assetlib tries to include it.
//
// A library and not an executable, unlike a check with symbols to demand: nothing declared here is
// defined elsewhere, because nothing is declared here at all. This proves the *include* closure,
// which is the only thing a data-only surface can get wrong.
#include <assetlib_structs/Animation.h>        // IWYU pragma: keep
#include <assetlib_structs/BEnv.h>             // IWYU pragma: keep
#include <assetlib_structs/BMaterial.h>        // IWYU pragma: keep
#include <assetlib_structs/BMaterialImport.h>  // IWYU pragma: keep
#include <assetlib_structs/BMesh.h>            // IWYU pragma: keep
#include <assetlib_structs/BMeshImport.h>      // IWYU pragma: keep
#include <assetlib_structs/Bounds.h>           // IWYU pragma: keep           // IWYU pragma: keep
#include <assetlib_structs/ImageData.h>        // IWYU pragma: keep
#include <assetlib_structs/Mesh.h>             // IWYU pragma: keep
#include <assetlib_structs/Node.h>             // IWYU pragma: keep
#include <assetlib_structs/Skeleton.h>         // IWYU pragma: keep
#include <assetlib_structs/SourceRef.h>        // IWYU pragma: keep
#include <assetlib_structs/SourceStamp.h>      // IWYU pragma: keep
#include <assetlib_structs/VertexLayout.h>     // IWYU pragma: keep
#include <assetlib_structs/VkFormat.h>         // IWYU pragma: keep
#include <assetlib_structs/magic.h>            // IWYU pragma: keep

namespace assetlib
{
	// Without a definition the archive is empty, which some toolchains warn on. A function and not a
	// constant: an unused constant is discarded and then warned about (MSVC C5264, which
	// [[maybe_unused]] does not reach), while a definition with external linkage is always emitted.
	void
	assetlibStructsSelfcheck() noexcept
	{}
}
