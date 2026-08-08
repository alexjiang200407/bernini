#pragma once

namespace assetlib
{
	/**
	 * The file extension that names each container this library writes.
	 *
	 * One place, because an extension was spelled out once per consumer -- the reference scan, the
	 * texture prune, the material bake -- so a new container, or a renamed one, meant editing
	 * several files in lockstep or watching them drift.
	 *
	 * The extension is the identity a file browser sees; it never opens the file. The magic is the
	 * identity a reader trusts, and lives beside the structs it heads
	 * (`assetlib_structs/magic.h`). See `assetTypeFromExtension` for the former, the CLI's `sniff`
	 * for the latter.
	 */

	inline constexpr std::string_view c_MeshExtension        = ".bmesh";
	inline constexpr std::string_view c_MaterialExtension    = ".bmaterial";
	inline constexpr std::string_view c_TextureExtension     = ".ktx2";
	inline constexpr std::string_view c_EnvironmentExtension = ".benv";
	inline constexpr std::string_view c_SkyExtension         = ".bsky";
	inline constexpr std::string_view c_EnvLightingExtension = ".benvl";
	inline constexpr std::string_view c_SkeletonExtension    = ".bskel";
	inline constexpr std::string_view c_AnimationExtension   = ".banim";
}
