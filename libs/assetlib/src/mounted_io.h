#pragma once
#include <assetlib/image_io.h>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct AnimationSet;
	struct BEnv;
	struct BEnvLighting;
	struct BMaterial;
	struct BMesh;
	struct BSky;
	struct BVat;
	struct EnvMapRoute;
	struct ImageData;
	struct MeshRefs;
	struct ResolvedEnvironment;
	struct Skeleton;
	struct SourceStamp;
	struct VatRefs;

	/**
	 * Every read addressed to a mount, gathered here because a mount alone is not a way anyone
	 * outside this library should be reading a project.
	 *
	 * `AssetStore` is that way: it carries the mount *and* the writable root the answers are
	 * relative to, so a caller cannot hold half the pair. These are what its methods forward to --
	 * the primitive, not the API. A `.cpp` in this library uses them freely; nothing else can, and
	 * that is the point.
	 *
	 * The path-taking overloads stay public: they address a file on the host that no project owns,
	 * which is a different question and one an `AssetStore` cannot answer.
	 */

	[[nodiscard]] BMesh
	load(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] MeshRefs
	loadMeshRefs(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] Skeleton
	loadSkeleton(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] AnimationSet
	loadAnimations(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] std::string
	loadAnimationSkeletonPath(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] BMaterial
	loadMaterial(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] BEnv
	loadEnv(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] BSky
	loadSky(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] BEnvLighting
	loadEnvLighting(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] BVat
	loadVat(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] BVat
	loadVatTables(const core::file::IFileSystem& fileSystem, std::string_view path);

	[[nodiscard]] VatRefs
	loadVatRefs(const core::file::IFileSystem& fileSystem, std::string_view path);

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
	vatIsStale(const BVat& vat, const core::file::IFileSystem& fileSystem);

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

	/**
	 * The mounted form of describe: each routed source stamped, so a stale bake is visible.
	 *
	 * @param fileSystem Null to report what the container records and stop, which is what the public
	 *        `describe` overloads are -- one body serves both rather than two that can drift.
	 */
	[[nodiscard]] std::string
	describe(const BMaterial& material, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BSky& sky, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BEnvLighting& lighting, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BEnv& env, const core::file::IFileSystem* fileSystem);

	[[nodiscard]] std::string
	describe(const BVat& vat, const core::file::IFileSystem* fileSystem);
}
