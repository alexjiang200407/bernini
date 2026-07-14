#pragma once
#include <core/str/str.h>

namespace assetlib
{
	/** The three kinds of asset file a project holds, one file extension each. */
	enum class AssetType : uint32_t
	{
		kMesh,      // .bmesh
		kMaterial,  // .bmaterial
		kTexture,   // .ktx2
	};

	/** Why one asset holds another alive. */
	enum class RefKind : uint32_t
	{
		kSubmeshMaterial,  // a .bmesh names a .bmaterial
		kBakedMap,         // a .bmaterial names a map its bake wrote
		kChannelRoute,     // a .bmaterial routes a channel from a source texture
	};

	/** `referrer` names `target`. Both relative to the data root, in generic form. */
	struct AssetRef
	{
		std::string referrer;
		std::string target;
		RefKind     kind = RefKind::kSubmeshMaterial;

		friend bool
		operator==(const AssetRef&, const AssetRef&) = default;
	};

	struct AssetRefScanDesc
	{
		std::filesystem::path dataRoot;  // the project's Data directory
	};

	/**
	 * The asset kind `path`'s extension names, or nullopt for anything this project stores no assets of --
	 * a `.txt`, a `.glb` waiting to be imported, a directory.
	 *
	 * By the extension, and not by the file's contents: the project writes every one of these names
	 * itself, and a caller holding a path out of a file browser has not opened the file. Compare
	 * `assetlib_cli`'s `sniff`, which reads the container magic precisely so that `describe` works on a
	 * file named anything at all.
	 */
	[[nodiscard]] std::optional<AssetType>
	assetTypeFromExtension(const std::filesystem::path& path);

	/**
	 * Who references what, across a whole project: one walk of the data root, reading each mesh's material
	 * chunk (never its geometry -- see loadMaterialPaths) and each material whole.
	 *
	 * A snapshot, not a live view. The data root is shared with the user's file manager, so it is rebuilt
	 * at the point a question is asked rather than cached -- a cached graph would not merely go stale, it
	 * would refuse a deletion while naming a blocker that had since been deleted from under it.
	 *
	 * A *target* that is not on disk goes to `broken`, and is not an error: one file removed behind the
	 * editor's back must not make every deletion in the project impossible.
	 */
	class AssetRefGraph
	{
	public:
		/**
		 * @throws std::runtime_error if `dataRoot` is not a directory, or if a *referrer* -- a `.bmesh` or
		 *         `.bmaterial` -- below it cannot be read. Fatal on purpose, and for the reason the prune
		 *         is: edges we cannot see are edges we would delete through.
		 */
		[[nodiscard]] static AssetRefGraph
		Scan(const AssetRefScanDesc& desc);

		/** The edges naming `asset`. Empty means nothing holds it, and it can be deleted. */
		[[nodiscard]] std::span<const AssetRef>
		ReferrersOf(std::string_view asset) const;

		[[nodiscard]] bool
		IsReferenced(std::string_view asset) const
		{
			return !ReferrersOf(asset).empty();
		}

		/** The edges `asset` itself names. Linear: only a dialog asks, and only about one asset. */
		[[nodiscard]] std::vector<AssetRef>
		ReferencesOf(std::string_view asset) const;

		/**
		 * The edges reaching *into* `directory` from outside it: something beneath it is named by
		 * something that is not.
		 *
		 * That is what holds a directory. An edge wholly inside it is not -- both ends go together. Nor is
		 * an edge pointing out of it, for the same reason deleting a mesh does not take its materials:
		 * what the deleted thing referenced was never the deleted thing's to take.
		 */
		[[nodiscard]] std::vector<AssetRef>
		ReferrersInto(std::string_view directory) const;

		[[nodiscard]] std::span<const AssetRef>
		Edges() const noexcept
		{
			return m_Edges;
		}

		/** What the graph was scanned against; every path in it is relative to this. */
		[[nodiscard]] const std::filesystem::path&
		DataRoot() const noexcept
		{
			return m_DataRoot;
		}

		std::vector<AssetRef> broken;  // `target` is named by `referrer`, but is not on disk

		size_t meshesScanned    = 0;
		size_t materialsScanned = 0;

	private:
		struct Range
		{
			uint32_t first = 0;
			uint32_t count = 0;
		};

		// Sorted by target, so every referrer of one asset is a contiguous run: ReferrersOf is the hot
		// query -- "what holds the thing I am about to delete?" -- and answers it in one hash lookup.
		std::vector<AssetRef>               m_Edges;
		core::str::unordered_str_map<Range> m_ByTarget;

		std::filesystem::path m_DataRoot;
	};

	/** What a deletion would destroy, and what stands in its way. */
	struct DeletionPlan
	{
		std::string target;  // relative to the data root: an asset file, or a directory

		/** What `target` is, or nullopt when it is a directory -- which is not an asset. */
		std::optional<AssetType> assetType;

		std::vector<AssetRef> blockers;

		/**
		 * For a directory: every file beneath it, which all go with it -- including files of no kind this
		 * project stores anything about, because removing a directory removes what is in it. Empty for a
		 * single asset, which takes nothing with it.
		 */
		std::vector<std::string> contents;

		[[nodiscard]] bool
		IsDirectory() const noexcept
		{
			return !assetType.has_value();
		}

		[[nodiscard]] bool
		Allowed() const noexcept
		{
			return blockers.empty();
		}
	};

	/**
	 * Whether `asset` -- a file, or a whole directory -- can be deleted, and if not, every edge that says
	 * otherwise.
	 *
	 * A mesh always comes back allowed, with no special case for it: nothing in the project produces an
	 * edge into a `.bmesh`. Deleting one therefore leaves the materials it named behind, which is the
	 * point -- a material is a shareable asset, not a part of the mesh that happened to name it first.
	 *
	 * A **directory** is held only by an edge reaching into it from outside (see ReferrersInto), and takes
	 * everything beneath it. So `Meshes/` deletes and leaves every material, while a folder of textures a
	 * material routes from does not. Whether a directory is one the *project* needs is not a question this
	 * can answer -- the caller owns its own layout.
	 *
	 * @throws std::runtime_error if `target` is a file of no kind this project stores anything about, or
	 *         does not resolve to somewhere inside the data root.
	 */
	[[nodiscard]] DeletionPlan
	planDeletion(const AssetRefGraph& graph, std::string_view target);

	enum class DeletionStatus
	{
		kDeleted,  // gone; a file that had already vanished counts, as it does for the prune
		kRefused,  // still referenced, and nothing was touched
		kFailed,   // could not be removed: held open by another process, or an I/O error
	};

	struct DeletionResult
	{
		DeletionStatus status = DeletionStatus::kFailed;
		std::string    error;  // non-empty only when status == kFailed
	};

	/**
	 * Deletes what `plan` names -- one file, or a directory and everything beneath it -- having re-checked
	 * the plan itself, so the rule cannot be bypassed by a caller that forgot to look.
	 *
	 * Reports a failure rather than throwing, because a failure here is ordinary: a preview decode holds
	 * the `.ktx2` open, and Windows refuses to unlink an open file. "Still referenced" and "the file is in
	 * use" are different things to tell a user, which is why they are different statuses. A directory whose
	 * removal fails part-way through is reported kFailed, with whatever came off already gone -- there is
	 * no undo, and pretending otherwise would be worse than saying so.
	 *
	 * Deletion is not cascading. Deleting a material leaves the baked maps it alone named on disk; they are
	 * what findUnusedBakedTextures then sweeps.
	 *
	 * Pass the `desc` the plan's graph was scanned with -- the path is relative to its `dataRoot`.
	 */
	DeletionResult
	deleteAsset(const DeletionPlan& plan, const AssetRefScanDesc& desc);
}
