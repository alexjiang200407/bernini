#pragma once
#include <core/str/str.h>

namespace assetlib
{
	class AssetStore;

	/** The kinds of asset file a project holds, one file extension each. */
	enum class AssetType : uint32_t
	{
		kMesh,            // .bmesh
		kMaterial,        // .bmaterial
		kTexture,         // .ktx2
		kEnvironment,     // .benv
		kSky,             // .bsky
		kEnvLighting,     // .benvl
		kSkeleton,        // .bskel
		kAnimation,       // .banim
		kImportDocument,  // .bimport -- the authored half of one imported source; text, never packed
		kAvatar,          // .bavatar -- the authored half of one rig; text
		// The number of asset kinds. Anchors the assertion that every one of them has a codec;
		// anchoring that on whichever enumerator happens to be last instead means appending one
		// silently satisfies it.
		kCount,
	};

	/**
	 * Whether `type` is one of the three containers a mesh import produces together. They travel as a
	 * group everywhere: regenerated as one, produced as one, counted as one.
	 */
	[[nodiscard]] constexpr bool
	isGeometryContainer(const AssetType type) noexcept
	{
		return type == AssetType::kMesh || type == AssetType::kSkeleton ||
		       type == AssetType::kAnimation;
	}

	/** Why one asset holds another alive. */
	enum class RefKind : uint32_t
	{
		kSubmeshMaterial,   // a .bmesh, or a .bimport's binding, names a .bmaterial
		kBakedMap,          // a .bmaterial, .bsky or .benvl names a map its bake wrote
		kChannelRoute,      // a .bmaterial routes a channel from a source texture
		kEnvironmentPart,   // a .benv names the .bsky or .benvl it composes
		kEnvSource,         // a .bsky or .benvl names the radiance its bake read
		kMeshSkeleton,      // a .bmesh's joint indices address a .bskel
		kClipSkeleton,      // a .banim's clips were resampled against a .bskel
		kImportedSource,    // a .bimport names the .glb it was imported from
		kDocumentSkeleton,  // a .bimport names the .bskel its source's joint indices address
		kDocumentOutput,    // a .bimport names a container its source produced
		kAvatarSkeleton,  // a .bavatar's bone names address the .bskel it sits by convention beside
	};

	/**
	 * Whether an edge of this kind is a path *stored inside* the referrer, and so something a
	 * rename has to rewrite there.
	 *
	 * False for the two the scan derives from the referrer's own key: a `.bimport` sits beside its
	 * `.glb` and a `.bavatar` at the swapped key of its `.bskel`. Those follow a rename by the file
	 * moving, and there is nothing in either document to edit -- an avatar in particular holds bone
	 * names and no path at all, so a rename that tried to rewrite one would have nothing to write.
	 */
	[[nodiscard]] constexpr bool
	isStoredRef(const RefKind kind) noexcept
	{
		return kind != RefKind::kImportedSource && kind != RefKind::kAvatarSkeleton;
	}

	/** `referrer` names `target`. Both relative to the data root, in generic form. */
	struct AssetRef
	{
		std::string referrer;
		std::string target;
		RefKind     kind = RefKind::kSubmeshMaterial;

		friend bool
		operator==(const AssetRef&, const AssetRef&) = default;
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
	 * Who references what, across a whole project: one walk of the data root, reading each mesh's and each
	 * clip set's reference chunks (never their bulk -- see loadMeshRefs) and each material whole.
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
		 * @throws std::runtime_error if a *referrer* -- a `.bmesh`, `.bmaterial`, `.banim`, `.benv`,
		 *         `.bsky`, `.benvl` or `.bimport` -- in `store` cannot be read. For a `.bimport`
		 *         that includes a merge left unresolved: its edges are blockers, so a document the
		 *         scan cannot parse is knowingly fatal. The error names the file. Fatal on purpose,
		 *         and for the reason the prune is: edges we cannot see are edges we would delete
		 *         through.
		 */
		[[nodiscard]] static AssetRefGraph
		Scan(const AssetStore& store);

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

		/** Where a rename or a delete writes; every path in the graph is relative to this. */
		[[nodiscard]] const std::filesystem::path&
		DataRoot() const noexcept
		{
			return m_DataRoot;
		}

		/**
		 * Whether the scan saw `path` as a file.
		 *
		 * Answered from the snapshot rather than from the disk, so it agrees with the edges beside it:
		 * a plan built from this graph describes the project as the scan found it, and re-stat'ing one
		 * path at plan time would make that one answer newer than the rest.
		 */
		[[nodiscard]] bool
		Contains(std::string_view path) const;

		/**
		 * Every file the scan saw beneath `directory`, sorted, and empty when there are none.
		 *
		 * A mount enumerates files and not directories, so a directory *is* what is under it: one that
		 * holds nothing does not exist to ask about, however it looks in a file browser.
		 */
		[[nodiscard]] std::vector<std::string>
		GetFilesUnder(std::string_view directory) const;

		std::vector<AssetRef> broken;  // `target` is named by `referrer`, but is not on disk

		size_t meshesScanned          = 0;
		size_t importDocumentsScanned = 0;
		size_t materialsScanned       = 0;
		size_t environmentsScanned    = 0;  // .benv, .bsky and .benvl together
		size_t clipSetsScanned        = 0;
		size_t avatarsScanned         = 0;

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

		// Every file the scan enumerated, sorted: what Contains and GetFilesUnder answer from.
		std::vector<std::string> m_Files;
	};

	/** What a deletion would destroy, and what stands in its way. */
	struct DeletionPlan
	{
		std::string target;  // relative to the data root: an asset file, or a directory

		/** What `target` is, or nullopt when it is a directory -- which is not an asset. */
		std::optional<AssetType> assetType;

		std::vector<AssetRef> blockers;

		/**
		 * The `.bimport` documents naming anything this plan deletes -- the cascade included --
		 * among their `outputs`, which DeleteAsset rewrites to drop those entries.
		 *
		 * Not a blocker: a document does not *need* what it produced. But an
		 * `outputs` entry naming a file that is gone reads as **absent** to Reimport, which would
		 * put it straight back.
		 */
		std::vector<std::string> producers;

		/**
		 * For a directory: every file beneath it, which all go with it -- including files of no kind this
		 * project stores anything about, because removing a directory removes what is in it. Empty for a
		 * single asset, which takes nothing with it.
		 */
		std::vector<std::string> contents;

		/**
		 * What planCascadeDeletion adds: every asset that nothing would reference once the target
		 * (and the rest of this list) is gone, sorted. Always empty from planDeletion, and for a
		 * plan that is not Allowed() -- a blocked deletion frees nothing.
		 */
		std::vector<std::string> cascade;

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
	 * A mesh always comes back allowed. The one edge into a `.bmesh` -- the `kDocumentOutput` its own
	 * `.bimport` carries -- is a claim about what produced it, not a need, so it lands in `producers`
	 * rather than blocking. Deleting a mesh therefore leaves the materials it named behind, which is
	 * the point -- a material is a shareable asset, not a part of the mesh that happened to name it
	 * first.
	 *
	 * A **directory** is held only by an edge reaching into it from outside (see ReferrersInto), and takes
	 * everything beneath it. So `Derived/Meshes/` deletes and leaves every material, while a folder
	 * of textures a material routes from does not. Whether a directory is one the *project* needs
	 * is not a question this can answer -- the caller owns its own layout.
	 *
	 * @throws std::runtime_error if `target` is a file of no kind this project stores anything about, or
	 *         does not resolve to somewhere inside the data root.
	 */
	[[nodiscard]] DeletionPlan
	planDeletion(const AssetRefGraph& graph, std::string_view target);

	/**
	 * planDeletion, with `cascade` also filled: everything the target alone was holding alive, freed
	 * the way dropping a row resolves its foreign keys. An asset the deleted set references goes with
	 * it **only when nothing outside the set references it too**, applied transitively -- a material
	 * freed by its last mesh frees the textures it alone routed. It never reaches *up*: what
	 * references the target blocks this plan exactly as it blocks planDeletion's.
	 *
	 * @throws std::runtime_error as planDeletion does.
	 */
	[[nodiscard]] DeletionPlan
	planCascadeDeletion(const AssetRefGraph& graph, std::string_view target);

	enum class DeletionStatus
	{
		kDeleted,  // gone; a file that had already vanished counts, as it does for the prune
		kRefused,  // still referenced, and nothing was touched
		// Could not be removed: held open by another process, or an I/O error. Also reported when
		// the files went but a `producers` document could not be rewritten -- the claim is stale
		// until the next deletion or migrate settles it.
		kFailed,
	};

	struct DeletionResult
	{
		DeletionStatus status = DeletionStatus::kFailed;
		std::string    error;  // non-empty only when status == kFailed
	};

	/** A file moving from one key to another. Both data-root-relative. */
	struct RenameMove
	{
		std::string from;
		std::string to;

		friend bool
		operator==(const RenameMove&, const RenameMove&) = default;
	};

	/** What a rename would move, and every stored reference that must follow it. */
	struct RenamePlan
	{
		RenameMove subject;  // an asset file, or a directory

		/** What `subject` is, or nullopt when it is a directory -- which is not an asset. */
		std::optional<AssetType> assetType;

		/**
		 * The `.glb` an import document describes. **Authored**, so a rename that cannot move it
		 * fails: nothing regenerates one, and `Reimport` reads from it to write `outputs`.
		 */
		std::optional<RenameMove> source;

		/**
		 * The containers that document produced, still named after `subject`'s stem. **Cache**, so
		 * one that is not on disk is skipped rather than failing the rename.
		 *
		 * Rename targets like `subject`, so `referrers` covers the edges naming any of them -- a
		 * `.bskel` a second source's document binds is rewritten there rather than orphaned.
		 * [docs/assetlib_api.md](docs/assetlib_api.md)
		 */
		std::vector<RenameMove> outputs;

		/**
		 * The `.bavatar` beside each `.bskel` this rename moves -- `subject` itself, or one of
		 * `outputs`. **Authored**, so unlike an output a move that fails is fatal: nothing
		 * regenerates one.
		 *
		 * Here rather than folded into `outputs` because the path *is* the attachment: an avatar
		 * left behind by a skeleton that moved is not stale, it is detached, and no re-cook
		 * reattaches it. Only avatars that exist are listed; most rigs have none.
		 */
		std::vector<RenameMove> avatars;

		/**
		 * The edges whose stored path must be rewritten: every reference to `from`, or -- for a
		 * directory -- to anything beneath it, wherever the referrer sits. An edge from inside a renamed
		 * directory counts too: its referrer moves with the directory, but the target path it stores
		 * does not rewrite itself.
		 */
		std::vector<AssetRef> referrers;

		[[nodiscard]] bool
		IsDirectory() const noexcept
		{
			return !assetType.has_value();
		}
	};

	/**
	 * What renaming `from` to `to` (both relative to the data root) would touch. A rename is never
	 * blocked by references the way a deletion is -- they are rewritten to follow -- so the plan's
	 * `referrers` are work, not blockers.
	 *
	 * An **imported source** may be named on either side, and plans the whole import: a `.glb` and
	 * its `.bimport` are one asset under two names, so both spellings resolve to the document and
	 * the rest of the group lands in `source` and `outputs`. `from` and `to` therefore read back as
	 * `.bimport`
	 * keys even when a `.glb` was asked for -- the document is the half that is an asset.
	 *
	 * @throws std::runtime_error if either path does not resolve to somewhere inside the data root, if
	 *         they name the same thing, if `from` does not exist or is a file of no kind this project
	 *         stores anything about, if the rename would change what kind of asset the file is, if a
	 *         directory would move into itself, if `to` already exists (a rename never overwrites --
	 *         except for the same file spelled in a different case, which is how a case-insensitive
	 *         filesystem answers a case-only rename), or if `to`'s parent directory does not exist.
	 *         For an imported source, also if its `.bimport` is absent or will not parse: the
	 *         document is what says which containers move, and moving a source without them would
	 *         strand every one.
	 */
	[[nodiscard]] RenamePlan
	planRename(const AssetRefGraph& graph, std::string_view from, std::string_view to);

	enum class RenameStatus
	{
		kRenamed,  // moved, and every referrer rewritten
		kFailed,   // nothing changed: whatever had been rewritten was put back before reporting
	};

	struct RenameResult
	{
		RenameStatus status = RenameStatus::kFailed;
		std::string  error;  // non-empty only when status == kFailed
	};
}
