#pragma once
#include <assetlib/banim_io.h>
#include <assetlib/benvl_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/bskel_io.h>
#include <assetlib/bsky_io.h>
#include <assetlib/bvat_io.h>

namespace assetlib
{
	/**
	 * The names this library's writers have always used for their bake tokens, now reading the
	 * values out of each container's codec rather than holding a second copy.
	 *
	 * The values live in `AssetCodec<T>::c_BakeToken`, beside the `Serialize` a bump has to move
	 * with -- a token one file away from its writer is a token that gets forgotten, and a forgotten
	 * bump makes stale files read as current. These aliases exist only so the existing writers keep
	 * compiling; they go when their callers move to the codec.
	 */
	inline constexpr uint64_t c_BMeshBakeToken        = AssetCodec<BMesh>::c_BakeToken;
	inline constexpr uint64_t c_BSkelBakeToken        = AssetCodec<Skeleton>::c_BakeToken;
	inline constexpr uint64_t c_BAnimBakeToken        = AssetCodec<AnimationSet>::c_BakeToken;
	inline constexpr uint64_t c_BSkyBakeToken         = AssetCodec<BSky>::c_BakeToken;
	inline constexpr uint64_t c_BEnvLightingBakeToken = AssetCodec<BEnvLighting>::c_BakeToken;
	inline constexpr uint64_t c_BVatBakeToken         = AssetCodec<BVat>::c_BakeToken;
}
