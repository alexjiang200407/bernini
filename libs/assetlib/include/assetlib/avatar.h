#pragma once
#include <assetlib_structs/Skeleton.h>
#include <core/file/IFileSystem.h>

namespace assetlib
{
	/**
	 * One leg, as the four bone *names* the rig spells them with.
	 *
	 * Names and not indices: a bone index is a fact about one cook of one `.bskel`, and a re-import
	 * that reorders the rig would leave an avatar of indices silently naming other joints. The names
	 * are pack conventions -- `Dog L Foot`, `Coyote L Foot` -- which is also why nothing sniffs them.
	 */
	struct AvatarLeg
	{
		std::string hip;
		std::string knee;
		std::string ankle;
		std::string toe;

		bool
		operator==(const AvatarLeg&) const = default;
	};

	/**
	 * The authored half of a rig: what a person decided about a skeleton, as opposed to what the
	 * import measured off the source. A `.bmaterial` is a mesh's; this is a `.bskel`'s.
	 *
	 * Found **by convention** from the skeleton it belongs to (`avatarKeyFor`), not by a field
	 * naming it: nothing attaches an avatar, the path is the attachment. That is what lets a packed
	 * project reach one and the reference scan derive the edge, the same way a `.bimport` is found
	 * beside its source.
	 *
	 * Text, because it is authored -- canonical JSON, so two branches merge it like code. Keys a
	 * reader does not know survive in `extraJson` and are written back, so a newer branch's
	 * authoring is not destroyed by an older one saving over it.
	 */
	struct Avatar
	{
		std::vector<AvatarLeg> legs;
		std::string            extraJson = "{}";

		bool
		operator==(const Avatar&) const = default;
	};

	/**
	 * `Derived/Skeletons/dog.bskel` -> `Authored/Skeletons/dog.bavatar`.
	 *
	 * Both halves of the key change: the extension, as `importDocumentKeyFor` swaps one, and the
	 * origin directory, which that rule never has to -- a `.bimport` sits beside its source, and a
	 * skeleton is derived while its avatar is authored.
	 *
	 * @throws std::runtime_error unless `skeletonKey` is a `.bskel` under the skeletons directory.
	 */
	[[nodiscard]] std::string
	avatarKeyFor(std::string_view skeletonKey);

	/**
	 * `Authored/Skeletons/dog.bavatar` -> `Derived/Skeletons/dog.bskel`, the inverse.
	 *
	 * @throws std::runtime_error unless `avatarKey` is a `.bavatar` under the avatars directory.
	 */
	[[nodiscard]] std::string
	skeletonKeyForAvatar(std::string_view avatarKey);

	/** One leg's joints as bone indices into the skeleton they were resolved against. */
	struct AvatarLegChain
	{
		uint32_t hip   = 0;
		uint32_t knee  = 0;
		uint32_t ankle = 0;
		uint32_t toe   = 0;

		bool
		operator==(const AvatarLegChain&) const = default;
	};

	/**
	 * The avatar's legs as indices into `skeleton`, in authored order.
	 *
	 * Resolved at load and nowhere else: the `.bskel` keeps its names and the avatar enters no cache
	 * key, so a rig re-imported under a reordered bone table resolves afresh rather than going
	 * stale. Whether the four are a *direct* parent chain is not judged here -- the pose pass is
	 * what cannot walk an indirect one, and `IScene::AddRig` refuses it naming the bone.
	 *
	 * @throws std::runtime_error naming the leg and the bone if `skeleton` carries no such name.
	 */
	[[nodiscard]] std::vector<AvatarLegChain>
	resolveLegChains(const Avatar& avatar, const Skeleton& skeleton);

	/** @throws what `IFileSystem::Read` and `AssetCodec<Avatar>::Deserialize` throw. */
	[[nodiscard]] Avatar
	loadAvatar(const core::file::IFileSystem& files, std::string_view key);

	/**
	 * The legs the rig at `skeletonKey` authors, resolved against `skeleton`, or empty when no
	 * avatar sits beside it -- which is most rigs, and costs one stat.
	 *
	 * The whole of "does this rig plant, and where": the avatar found by its convention, read, and
	 * its names resolved. One door for the cook and the load both, so the two cannot disagree about
	 * which file a rig's legs come from.
	 *
	 * An avatar that will not parse, or that names bones this rig does not carry, resolves to no
	 * legs and says so in the log rather than failing the caller: the rig still animates, unplanted,
	 * but an authored file being ignored is not something to pass over in silence.
	 */
	[[nodiscard]] std::vector<AvatarLegChain>
	legChainsForRig(
		const core::file::IFileSystem& files,
		std::string_view               skeletonKey,
		const Skeleton&                skeleton);
}
