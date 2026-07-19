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
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>
#include <gamelib/AssetManager.h>

class QTimer;
class Renderer;

struct AssetThumbnailDesc
{
	Renderer* renderer = nullptr;

	uint32_t dimension    = 256;
	uint32_t maxInstances = 256;

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
 * signal, with an mtime stamp deciding staleness), but it cannot render on a worker: bgl has no
 * internal synchronization, so BeginFrame/Draw/EndFrame/Screenshot are render-thread-only. Only the
 * `.bmesh` read runs on the pool; the render is drained from the UI thread one asset per event-loop
 * turn, which keeps a folder from freezing the editor while it populates.
 *
 * Geometry is added to the shared scene and torn down again after each shot, so a thumbnail leaves
 * nothing behind for the Level Editor's view to draw.
 */
class AssetThumbnailCache : public QObject
{
	Q_OBJECT

public:
	static constexpr int c_BudgetKb = 64 * 1024;

	explicit AssetThumbnailCache(AssetThumbnailDesc desc, QObject* parent = nullptr);
	~AssetThumbnailCache() override;

	/**
	 * Points the cache at the editor's asset manager, which owns the project's Data root and is the
	 * only route from a `.bmaterial` to a scene material. Null (the default, and what a closed project
	 * means) leaves materials unresolvable, so they get no thumbnail and every mesh draws in the
	 * neutral default.
	 *
	 * Borrowed, not owned: one manager is shared across the editor so that a material loaded twice is
	 * one upload and one reference count. It must outlive this cache.
	 *
	 * Drops everything already rendered -- the same relative path means a different asset under a
	 * different root.
	 */
	void
	SetAssets(game::AssetManager* assets);

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

	// Renders the next queued asset, if any. One per call so the event loop keeps turning.
	void
	DrainOne();

	// Puts the mesh in the scene at each node that references it, wearing the materials it names.
	[[nodiscard]] QImage
	RenderMesh(const PendingRender& pending);

	// Puts a sphere in the scene wearing the material the request named.
	[[nodiscard]] QImage
	RenderMaterial(const PendingRender& pending);

	// Frames [center, radius], draws, and reads the target back: the half of a render both kinds share.
	[[nodiscard]] QImage
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

	QSet<QString>         m_InFlight;
	QQueue<PendingRender> m_Queue;
	QThreadPool           m_Pool;
	QTimer*               m_DrainTimer = nullptr;

	AssetThumbnailDesc   m_Desc;
	bgl::RenderTargetRef m_RenderTarget;
	bgl::SceneViewRef    m_SceneView;
	bgl::MaterialHandle  m_DefaultMaterial;

	// Turns a `.bmaterial` into a scene material, and owns the textures it names. The editor's only
	// route to that: the baked/loose branch lives in gamelib and nowhere else.
	//
	// Not owned -- see SetAssets. Null until a project is open.
	game::AssetManager* m_Assets = nullptr;

	// Live only for the duration of one render.
	std::vector<bgl::GeomHandle>         m_Geoms;
	std::vector<bgl::MeshInstanceHandle> m_Instances;

	// Live until the queue drains -- see ReleaseMaterials.
	std::vector<bgl::MaterialHandle> m_Materials;
};
