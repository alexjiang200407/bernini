#pragma once

#include <QCache>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <bgl/GeomHandle.h>
#include <bgl/IGraphics.h>
#include <bgl/IRenderContext.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <gamelib/AssetManager.h>

class ContextWorker;
class QTimer;
class Renderer;

// A thumbnail scene holds one asset at a time -- but that one asset can be as large as anything
// the level loads, so the size budgets must match the editor scene's or a mesh near its limits
// loads in the level yet fails to thumbnail. MainWindow passes its own settings-driven SceneDesc;
// these mirror its defaults.
inline bgl::SceneDesc
ThumbnailSceneDesc()
{
	auto desc                    = bgl::SceneDesc();
	desc.maxGeom                 = 256;
	desc.maxMeshlets             = 32768;
	desc.maxSubmeshes            = 512;
	desc.maxVertexBufferByteSize = 33'554'432;
	desc.maxIndices              = 2'000'000;
	desc.maxPbrMaterials         = 256;
	desc.maxLoosePbrMaterials    = 256;
	return desc;
}

struct AssetThumbnailDesc
{
	Renderer* renderer = nullptr;

	uint32_t dimension    = 256;
	uint32_t maxInstances = 256;

	bgl::SceneDesc sceneDesc = ThumbnailSceneDesc();

	std::string skybox;
	std::string irradiance;
	std::string prefilter;
	std::string brdfLut;
	float       exposure = 1.0f;
};

/**
 * Renders the assets the Content Explorer can illustrate to small pixmaps: a `.bmesh` as itself, a
 * `.bmaterial` on a sphere.
 *
 * The twin of TexturePreviewCache, and deliberately the same shape (Lookup / Request / a ready
 * signal, with an mtime stamp deciding staleness). It renders on a ContextWorker of its own -- its
 * own submission context, scene and thread -- so a folder populating its tiles neither stalls the
 * viewports' frame loop nor is stalled by it. The `.bmesh` read and texture decode run on the
 * pool; the render is drained from the UI thread one asset per event-loop turn, each handed to the
 * worker. The readback is split-phase -- submitted on one turn, resolved on a later one -- so no
 * turn blocks on the GPU.
 *
 * The cache's scene is its own (contexts must not share one), so its assets are uploaded by its
 * own AssetManager, separately from the editor's -- the price of isolation until scenes can be
 * shared across contexts.
 */
class AssetThumbnailCache : public QObject
{
	Q_OBJECT

public:
	static constexpr int c_BudgetKb = 64 * 1024;

	explicit AssetThumbnailCache(AssetThumbnailDesc desc, QObject* parent = nullptr);
	~AssetThumbnailCache() override;

	/**
	 * Points the cache at a project's Data root, the only way from a `.bmaterial` to a scene
	 * material. The cache builds its own AssetManager over its own scene from it. Empty (the
	 * default, and what a closed project means) leaves materials unresolvable, so they get no
	 * thumbnail and every mesh draws in the neutral default.
	 *
	 * Drops everything already rendered -- the same relative path means a different asset under a
	 * different root.
	 */
	void
	SetDataRoot(const std::filesystem::path& dataRoot);

	// Whether `path` names an asset this cache knows how to draw.
	[[nodiscard]] static bool
	CanThumbnail(const QString& path);

	// The thumbnail for `path`, or a null pixmap when it is absent, still rendering, undrawable, or
	// stale because the file changed on disk since it was rendered.
	[[nodiscard]] QPixmap
	Lookup(const QString& path) const;

	// Renders `path` unless a current copy is cached or one is already in flight. Emits ThumbnailReady
	// on success.
	void
	Request(const QString& path);

	// Modification time of `path` in ms, or 0 when it cannot be read.
	[[nodiscard]] static qint64
	FileStamp(const QString& path);

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

Q_SIGNALS:
	void
	ThumbnailReady(const QString& path, const QPixmap& thumbnail);

private:
	enum class ThumbnailType
	{
		kMesh,
		kMaterial,
	};

	// What a worker produced, waiting its turn on the GPU. `mesh` is null for a material; `prefetch`
	// holds the decoded textures whatever the kind, and only their upload is left to do.
	//
	// Both are shared_ptr because the queue must stay copyable and an ImageData is move-only.
	struct PendingRender
	{
		QString                                path;
		ThumbnailType                          type = ThumbnailType::kMesh;
		std::shared_ptr<assetlib::BMesh>       mesh;
		std::shared_ptr<game::TexturePrefetch> prefetch;
		qint64                                 stamp = 0;
	};

	// Hands a finished read back. Called by a worker via a queued invocation, so it always runs on the
	// UI thread. A null `prefetch` means the read failed and only clears the in-flight entry.
	void
	Enqueue(const QString& path, ThumbnailType type, PendingRender pending);

	// One step of the pipeline per call, so the event loop keeps turning: finish the in-flight
	// capture if its GPU copy has landed, then render and submit the next queued asset.
	void
	DrainOne();

	// Abandons the in-flight capture, if any. For teardown and project switches, where its image
	// would land under a stale key.
	void
	DiscardPendingCapture();

	// Puts the mesh in the scene at each node that references it, wearing the materials it names.
	[[nodiscard]] bgl::CaptureTicket
	RenderMesh(const PendingRender& pending);

	// Puts a sphere in the scene wearing the material the request named.
	[[nodiscard]] bgl::CaptureTicket
	RenderMaterial(const PendingRender& pending);

	// Frames [center, radius], draws, and submits the readback of the result -- resolved on a
	// later drain turn, so neither this thread nor the worker waits on the GPU.
	[[nodiscard]] bgl::CaptureTicket
	Shoot(const glm::vec3& center, float radius);

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

	struct CachedThumbnail
	{
		QPixmap pixmap;
		qint64  stamp = 0;
	};

	mutable QCache<QString, CachedThumbnail> m_Cache{ c_BudgetKb };

	// The one capture whose GPU copy is still in flight, and the cache entry it will become.
	struct PendingCapture
	{
		bgl::CaptureTicket ticket;
		QString            path;
		qint64             stamp = 0;
	};

	std::optional<PendingCapture> m_PendingCapture;

	QSet<QString>         m_InFlight;
	QQueue<PendingRender> m_Queue;
	QThreadPool           m_Pool;
	QTimer*               m_DrainTimer = nullptr;

	AssetThumbnailDesc   m_Desc;
	bgl::RenderTargetRef m_RenderTarget;
	bgl::SceneViewRef    m_SceneView;
	bgl::MaterialHandle  m_DefaultMaterial;

	// The cache's own context, scene and thread. Null when there is no device.
	std::unique_ptr<ContextWorker> m_Worker;

	// Turns a `.bmaterial` into a scene material, and owns the textures it names. The editor's only
	// route to that: the baked/loose branch lives in gamelib and nowhere else.
	//
	// The cache's own, over its own scene -- see SetDataRoot. Null until a project is open.
	// Created and destroyed on the worker thread; DataRoot() reads an immutable member, so the UI
	// thread may call it.
	std::unique_ptr<game::AssetManager> m_Assets;

	// Live only for the duration of one render.
	std::vector<bgl::GeomHandle>         m_Geoms;
	std::vector<bgl::MeshInstanceHandle> m_Instances;

	// Live until the queue drains -- see ReleaseMaterials.
	std::vector<bgl::MaterialHandle> m_Materials;
};
