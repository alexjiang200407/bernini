#pragma once

#include <QQueue>
#include <QThreadPool>

#include "Render/Renderer.h"
#include "Thumbnails/StampedPixmapCache.h"

#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/RenderJob.h>
#include <gamelib/AssetManager.h>

class QImage;

struct AssetThumbnailDesc
{
	Renderer* renderer = nullptr;

	uint32_t dimension        = 256;
	uint32_t initialInstances = 256;

	std::string environmentMap;

	// What the paths inside that `.benv` resolve against; see MaterialPreviewEnv::dataRoot.
	std::filesystem::path dataRoot;

	std::optional<float> exposureOverride;

	// Matches the material preview's default, so a thumbnail and the preview it was generated from
	// stand against the same backdrop. See MaterialPreviewEnv::skyMipLevelOverride.
	std::optional<uint32_t> skyMipLevelOverride = 3;
};

/**
 * Renders the assets the Content Explorer can illustrate to small pixmaps: a `.bmesh` as itself, a
 * `.bmaterial` on a sphere.
 *
 * The twin of TexturePreviewCache, and deliberately the same shape (Lookup / Request / a ready
 * signal, with an mtime stamp deciding staleness), but it cannot render on a worker: bgl has no
 * internal synchronization, so BeginFrame/Draw/EndFrame/Capture are render-thread-only. Only the
 * `.bmesh` read and texture decode run on the pool; the GPU half advances as a state machine
 * inside the renderer's frame loop -- one supersampled frame per asset, downscaled on resolve --
 * so a shot is a bounded slice of a tick and the GUI thread never waits on the render thread. The
 * readback is split-phase -- submitted on one tick, resolved on a later one -- and the finished
 * image comes back to the GUI thread as a queued call.
 *
 * Geometry is added to the shared scene and torn down again after each shot, so a thumbnail leaves
 * nothing behind for the Level Editor's view to draw.
 */
class AssetThumbnailCache : public StampedPixmapCache
{
	Q_OBJECT

public:
	static constexpr int c_BudgetKb = 64 * 1024;

	explicit AssetThumbnailCache(AssetThumbnailDesc desc, QObject* parent = nullptr);
	~AssetThumbnailCache() override;

	/**
	 * Points the cache at the editor's asset manager, which owns the project's Data root. Null (the
	 * default, and what a closed project means) leaves materials unresolvable, so they get no
	 * thumbnail and every mesh draws in the neutral default.
	 *
	 * Borrowed, not owned, and never acquired through: the cache builds a private manager over the
	 * same scene, because its texture uploads are mip-capped to the thumbnail's size and the shared
	 * manager keys textures by path -- a capped upload registered there would be served to a
	 * viewport asking for the same texture at full resolution. It must outlive this cache.
	 *
	 * Drops everything already rendered, and everything queued or mid-render: all of it was made
	 * against the manager being replaced.
	 */
	void
	SetAssets(game::AssetManager* assets);

	// Whether `path` names an asset this cache knows how to draw.
	[[nodiscard]] static bool
	CanThumbnail(const QString& path);

	// Renders `path` unless a current copy is cached or one is already being rendered. Emits Ready on
	// success.
	void
	Request(const QString& path) override;

	// True if the cache has a working render target, i.e. thumbnails are possible at all.
	[[nodiscard]] bool
	IsReady() const noexcept
	{
		return m_RenderTarget.Get() != nullptr;
	}

	// The edge length of a rendered thumbnail, in pixels.
	[[nodiscard]] uint32_t
	Dimension() const noexcept
	{
		return m_Desc.dimension;
	}

private:
	enum class ThumbnailType
	{
		kMesh,
		kMaterial,
	};

	// The worker's CookStaticMesh output for every mesh the nodes reference, keyed by mesh index.
	// The CPU half of AddStaticMeshGeom, taken off the render thread; the commit consumes the entries.
	using CookedMeshes = std::unordered_map<uint32_t, bgl::PreparedStaticMesh>;

	// What a worker produced, waiting its turn on the GPU. `mesh` and `cooked` are null for a
	// material; `prefetch` holds the decoded textures whatever the kind. Only uploads are left.
	//
	// All shared_ptr because the queue must stay copyable and the payloads are not.
	struct PendingRender
	{
		QString                                path;
		ThumbnailType                          type = ThumbnailType::kMesh;
		std::shared_ptr<assetlib::BMesh>       mesh;
		std::shared_ptr<CookedMeshes>          cooked;
		std::shared_ptr<game::TexturePrefetch> prefetch;
		qint64                                 stamp = 0;
		QString failure;  // why the read failed; empty when it did not
	};

	// One asset's trip through the GPU: built, drawn and submitted on its first tick, resolved on a
	// later one. Render thread only -- installed by PumpQueue's Post, cleared by the tick that
	// finishes it or by CancelShot's closure.
	struct Shot
	{
		PendingRender      item;
		uint64_t           epoch = 0;
		bool               built = false;
		bgl::RenderJob     job;
		bgl::CaptureTicket ticket;
	};

	// Hands a finished read back. Called by a worker via a queued invocation, so it always runs on the
	// UI thread. A null `prefetch` means the read failed, and the path is rejected with its reason.
	void
	Enqueue(const QString& path, ThumbnailType type, PendingRender pending);

	// Starts the next queued asset if none is mid-render; at the end of a batch, returns the shared
	// materials and leaves the frame loop.
	void
	PumpQueue();

	// A finished shot arriving back on the UI thread. `image` is null when the render failed; a stale
	// `epoch` means the shot was cancelled after it completed, and its claim is already gone.
	void
	OnShotDone(const QString& path, qint64 stamp, uint64_t epoch, const QImage& image);

	// One frame-loop tick of the in-flight shot: build, draw and submit it, or resolve its readback
	// -- whichever it is up to. Catches everything: a throw here would make the frame loop drop the
	// hook, stranding the claim.
	void
	Advance();

	// Puts the shot's asset in the scene and frames the camera on it. Throws if there is nothing to
	// draw; Advance abandons the shot.
	void
	BuildShot(Shot& shot);

	// Puts the mesh in the scene at each node that references it, wearing the materials it names.
	void
	BuildMesh(Shot& shot);

	// Puts a sphere in the scene wearing the material the request named.
	void
	BuildMaterial(Shot& shot);

	// Frames [center, radius] in the shot's camera and viewport.
	void
	FrameShot(Shot& shot, const glm::vec3& center, float radius);

	// Posts the shot's outcome back to the UI thread. The shot is dead afterwards.
	void
	FinishShot(const Shot& shot, QImage image);

	// The success path: hands the captured frame to a pool worker to convert and box-filter down to
	// the tile size, which then posts the outcome to the UI thread. The shot is dead afterwards.
	void
	FinishShotOnPool(const Shot& shot, assetlib::ImageData image);

	// A shot's failure path: hands back whatever it held and reports it as a null image.
	void
	AbortShot(const Shot& shot);

	// Abandons the in-flight shot, if any, and invalidates any completion already posted. For
	// teardown and project switches, where its image would land under a stale key.
	void
	CancelShot();

	void
	AttachToFrameLoop();

	void
	DetachFromFrameLoop();

	// The scene material the `.bmaterial` at `relPath` describes, or the neutral default if it cannot
	// be resolved, uploading its textures from `prefetch` rather than re-reading them.
	[[nodiscard]] bgl::MaterialHandle
	AcquireMaterial(std::string_view relPath, game::TexturePrefetch* prefetch);

	// Hands back what one render placed in the scene. Runs after every render: geometry is the bulk of
	// it, and nothing else in the queue wants this mesh's.
	void
	ReleaseGeometry();

	/**
	 * Hands back the materials the batch acquired, and the textures they held.
	 *
	 * Deferred to the end of the batch rather than run per render, because AssetManager shares a
	 * material by path: a folder whose meshes all use one material then uploads its textures once
	 * instead of once per tile. Safe only once ReleaseGeometry has run for every render, which is what
	 * "the queue is empty" means.
	 */
	void
	ReleaseMaterials();

	// The project's Data directory, or empty when no project is open.
	[[nodiscard]] std::filesystem::path
	DataRoot() const;

	// `path` relative to the data root, which is how every asset reference is stored. Empty if it does
	// not lie under the root.
	[[nodiscard]] std::string
	ToRelative(const QString& path) const;

	QQueue<PendingRender> m_Queue;
	QThreadPool           m_Pool;

	// The downscale's own worker: both of m_Pool's threads can be deep in KTX2 decodes, and a
	// finished capture waiting on them would stall the whole GPU pipeline behind the reads it was
	// split from. A member, not the global pool, so teardown drains it before the object dies.
	QThreadPool m_ScalePool;

	// Render thread only; see Shot.
	std::optional<Shot> m_Shot;

	// A shot has been handed to the render thread and its completion has not come back yet.
	bool m_ShotInFlight = false;

	// Stale-completion guard: bumped by CancelShot, so a completion posted before the cancel ran is
	// recognized and dropped rather than stored under a dead claim.
	uint64_t m_Epoch = 0;

	// The frame-loop registration that drives Advance, held only while a batch renders. 0 when
	// detached.
	Renderer::ViewportId m_FrameLoopId = 0;

	AssetThumbnailDesc   m_Desc;
	bgl::RenderTargetRef m_RenderTarget;
	bgl::SceneViewRef    m_SceneView;
	bgl::MaterialHandle  m_DefaultMaterial;

	// The project's manager, kept for its data root -- see SetAssets. Null until a project is open.
	game::AssetManager* m_Assets = nullptr;

	// What the cache actually acquires through: a manager of its own over the shared scene, so its
	// mip-capped texture uploads never sit in the shared cache under the path a viewport would ask
	// full resolution for. The baked/loose branch lives in gamelib and nowhere else, which is why a
	// manager is used at all. Lives on the render thread: created, used and destroyed there.
	std::unique_ptr<game::AssetManager> m_ThumbAssets;

	// Live only for the duration of one render.
	std::vector<bgl::GeomHandle>         m_Geoms;
	std::vector<bgl::MeshInstanceHandle> m_Instances;

	// Live until the queue drains -- see ReleaseMaterials.
	std::vector<bgl::MaterialHandle> m_Materials;
};
