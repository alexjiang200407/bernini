#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib/image_io.h>
#include <core/file/IFileSystem.h>
#include <tracy/Tracy.hpp>

namespace assetlib
{
	struct BEnvLighting;
	struct BMaterial;
	struct BSky;
	struct EnvMapRoute;
	struct ImageData;
	struct MeshRefs;
	struct ResolvedEnvironment;
	struct SourceStamp;

	/**
	 * Every read addressed to a mount, gathered here because a mount alone is not a way anyone
	 * outside this library should be reading a project.
	 *
	 * `AssetStore` is that way: it carries the mount *and* the writable root the answers are
	 * relative to, so a caller cannot hold half the pair. These are what its methods forward to --
	 * the primitive, not the API. A `.cpp` in this library uses them freely; nothing else can, and
	 * that is the point.
	 *
	 * Whole containers go through `load<T>` below; what remains named is the partial reads, which
	 * are not codecs -- each pulls a few chunks out of a file worth megabytes and stops.
	 */

	/**
	 * Any whole container, read through a mount and decoded by its codec.
	 *
	 * Eight named functions stood here, each of them exactly this line. They collapse because
	 * `AssetCodec<T>` now says which deserializer a type uses, so the type is the only thing that
	 * differed between them.
	 *
	 * `AssetStore::Load` is the public form and the one anything outside this library uses; this
	 * exists for the internals that hold a mount without the writable root beside it -- the
	 * environment resolve, and the reads a bake makes of its own inputs.
	 *
	 * @throws whatever `IFileSystem::Read` and the codec's deserializer throw.
	 */
	template <AssetCodecFor T>
	[[nodiscard]] T
	load(const core::file::IFileSystem& fileSystem, std::string_view path)
	{
		ZoneScopedN("assetlib container load");
		ZoneTextF("%.*s", static_cast<int>(path.size()), path.data());

		return AssetCodec<T>::Deserialize(fileSystem.Read(path));
	}

	[[nodiscard]] MeshRefs
	loadMeshRefs(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] std::string
	loadAnimationSkeletonPath(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] ImageData
	loadKTX2(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		Ktx2Decode                     decode = Ktx2Decode::kGpu,
		uint32_t                       maxDim = 0);

	[[nodiscard]] ImageData
	loadKTX2Preview(
		const core::file::IFileSystem& fileSystem,
		std::string_view               path,
		uint32_t                       maxDim = 128);

	[[nodiscard]] SourceStamp
	stampOf(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] bool
	bakeIsStale(const BMaterial& material, const core::file::IFileSystem& fileSystem);

	[[nodiscard]] bool
	drawsLoose(const BMaterial& material, const core::file::IFileSystem& fileSystem);

	[[nodiscard]] bool
	isSkyBakeStale(const BSky& sky, const core::file::IFileSystem& fileSystem);

	[[nodiscard]] bool
	isEnvLightingBakeStale(const BEnvLighting& lighting, const core::file::IFileSystem& fileSystem);

	[[nodiscard]] const std::string&
	envMapToDraw(const EnvMapRoute& route, const core::file::IFileSystem& fileSystem);

	[[nodiscard]] ResolvedEnvironment
	resolveEnvironment(
		const std::filesystem::path&   benvPath,
		const core::file::IFileSystem& fileSystem);
}
