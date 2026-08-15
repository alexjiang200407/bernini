#include "Thumbnails/AssetThumbnailCache.h"

#include "Mesh/BMeshUtil.h"
#include "Render/environment.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QImage>
#include <QRunnable>

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/image_io.h>
#include <assetlib_structs/BMaterial.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/ImageData.h>
#include <bgl/Camera.h>
#include <bgl/Viewport.h>
#include <core/file/LooseFileSystem.h>

namespace
{
	// The render target is this many times the output edge, and the capture is box-filtered back
	// down: silhouette antialiasing for a renderer that gets one frame and no accumulation. Any
	// higher and the readback's conversion becomes a frame-loop stall of its own. Hashed alpha
	// needs more averaging than this buys, so it is not resolved here at all -- the private manager
	// loads it as the blend it converges to (AssetManagerOptions::hashedAsBlend).
	constexpr uint32_t c_Supersample = 2;

	// A tick that overruns this gets logged with what it did: a slow build points at an asset's
	// upload, slow draws at the GPU.
	constexpr double c_SlowTickMs = 8.0;

	// Textures decode to the stored mip tail covering twice the output edge: the sphere and the
	// three-quarter mesh view carry no more detail than that at 256px, and the supersampled render
	// only magnifies the top level it kept.
	constexpr uint32_t c_TextureSupersample = 2;

	// A three-quarter view reads better than a straight-on one: it shows a silhouette and some depth.
	constexpr float c_Yaw   = 0.6f;
	constexpr float c_Pitch = 0.45f;

	constexpr auto c_MeshSuffix     = ".bmesh";
	constexpr auto c_MaterialSuffix = ".bmaterial";

	// Deep-copy: `shot` owns the pixels and dies with the caller's scope.
	QImage
	ToImage(const assetlib::ImageData& shot)
	{
		return QImage(
				   reinterpret_cast<const uchar*>(shot.pixels.data()),
				   static_cast<int>(shot.width),
				   static_cast<int>(shot.height),
				   static_cast<qsizetype>(shot.subresources.front().rowPitch),
				   QImage::Format_RGBA8888)
		    .copy();
	}

	// Decodes every texture the material at `relPath` names into `out`, capped at `textureMaxDim`. A
	// texture that will not decode is left out and reported here; AcquireTexture then resolves it to
	// the scene's default map rather than re-reading the file on the render thread.
	void
	PrefetchMaterial(
		const std::filesystem::path& dataRoot,
		const std::string&           relPath,
		uint32_t                     textureMaxDim,
		game::TexturePrefetch&       out)
	{
		if (relPath.empty())
			return;

		const assetlib::BMaterial material = assetlib::loadMaterial(dataRoot / relPath);

		const core::file::LooseFileSystem files(dataRoot);

		for (const std::string& texture :
		     game::MaterialTextures(material, assetlib::drawsLoose(material, files)))
		{
			if (texture.empty() || out.contains(texture))
				continue;

			try
			{
				out.emplace(
					texture,
					assetlib::loadKTX2(
						dataRoot / texture,
						assetlib::Ktx2Decode::kGpu,
						textureMaxDim));
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: cannot decode '%s': %s", texture.c_str(), e.what());
			}
		}
	}

	using CookedMeshes = std::unordered_map<uint32_t, bgl::PreparedStaticMesh>;

	/**
	 * Reads an asset, decodes the textures it needs and cooks its geometry -- everything about a
	 * thumbnail that does not touch the GPU, which is everything expensive except the uploads and
	 * the draw itself.
	 *
	 * The KTX2 decode transcodes a whole Basis mip chain and CookStaticMesh flattens the meshlet
	 * streams; both are pure CPU, and both dwarf the uploads that must be on the render thread. So
	 * the split is here.
	 */
	class LoadTask : public QRunnable
	{
	public:
		using Sink = std::function<void(
			std::shared_ptr<assetlib::BMesh>,
			std::shared_ptr<CookedMeshes>,
			std::shared_ptr<game::TexturePrefetch>)>;

		LoadTask(
			QString               path,
			std::string           relPath,
			bool                  isMaterial,
			std::filesystem::path dataRoot,
			uint32_t              textureMaxDim,
			Sink                  sink) :
			m_Path(std::move(path)), m_RelPath(std::move(relPath)), m_IsMaterial(isMaterial),
			m_DataRoot(std::move(dataRoot)), m_TextureMaxDim(textureMaxDim), m_Sink(std::move(sink))
		{
			setAutoDelete(true);
		}

		void
		run() override
		{
			std::shared_ptr<assetlib::BMesh>       mesh;
			std::shared_ptr<CookedMeshes>          cooked;
			std::shared_ptr<game::TexturePrefetch> prefetch;

			try
			{
				prefetch = std::make_shared<game::TexturePrefetch>();

				if (m_IsMaterial)
				{
					PrefetchMaterial(m_DataRoot, m_RelPath, m_TextureMaxDim, *prefetch);
				}
				else
				{
					mesh = std::make_shared<assetlib::BMesh>(
						assetlib::load(std::filesystem::path(m_Path.toStdWString())));

					if (mesh->meshes.empty())
						throw std::runtime_error("mesh contains no meshes");

					cooked = std::make_shared<CookedMeshes>();
					for (const assetlib::Node& node : mesh->nodes)
					{
						if (node.mesh == assetlib::c_InvalidIndex ||
						    node.mesh >= mesh->meshes.size() || cooked->contains(node.mesh))
							continue;

						cooked->emplace(node.mesh, bgl::CookStaticMesh(*mesh, node.mesh));
					}

					// Without a data root the mesh's materials cannot be resolved at all, and every
					// submesh falls back to the neutral default -- so there is nothing to decode.
					if (!m_DataRoot.empty())
					{
						for (const std::string& relPath : mesh->materials)
							PrefetchMaterial(m_DataRoot, relPath, m_TextureMaxDim, *prefetch);
					}
				}
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: cannot read '%s': %s", qPrintable(m_Path), e.what());
				mesh.reset();
				cooked.reset();
				prefetch.reset();
			}

			m_Sink(std::move(mesh), std::move(cooked), std::move(prefetch));
		}

	private:
		QString               m_Path;
		std::string           m_RelPath;
		bool                  m_IsMaterial = false;
		std::filesystem::path m_DataRoot;
		uint32_t              m_TextureMaxDim = 0;
		Sink                  m_Sink;
	};
}

AssetThumbnailCache::AssetThumbnailCache(AssetThumbnailDesc desc, QObject* parent) :
	StampedPixmapCache(c_BudgetKb, parent), m_Desc(std::move(desc))
{
	// Reading a .bmesh is I/O plus a parse; two at a time keeps the explorer responsive without
	// queueing up more decoded meshes than the GPU drain can retire.
	m_Pool.setMaxThreadCount(2);

	// At most one capture is ever awaiting its downscale; see PumpQueue.
	m_ScalePool.setMaxThreadCount(1);

	// No device (the editor runs without one in tests): stay inert. Lookup then always misses and
	// Request is a no-op, so callers need no special case.
	if (m_Desc.renderer == nullptr)
		return;

	m_Desc.renderer->Invoke([&] {
		try
		{
			auto rtDesc     = bgl::RenderTargetDesc();
			rtDesc.width    = static_cast<int>(m_Desc.dimension * c_Supersample);
			rtDesc.height   = static_cast<int>(m_Desc.dimension * c_Supersample);
			rtDesc.headless = true;

			m_RenderTarget = m_Desc.renderer->GetGraphics()->CreateRenderTarget(rtDesc);
			m_SceneView    = m_Desc.renderer->GetGraphics()->CreateSceneView(
				m_Desc.renderer->GetScene(),
				m_Desc.initialInstances);
		}
		catch (const std::exception& e)
		{
			qWarning("AssetThumbnail: no render target, thumbnails disabled: %s", e.what());
			m_RenderTarget = nullptr;
			m_SceneView    = nullptr;
			return;
		}

		bgl::IScene*     scene = m_Desc.renderer->GetScene().Get();
		bgl::ISceneView* view  = m_SceneView.Get();
		// The same helper the material preview uses, so a thumbnail cannot be lit differently from
		// the preview it was generated from.
		static_cast<void>(editor::ApplyEnvironment(
			scene,
			view,
			m_Desc.environmentMap,
			m_Desc.dataRoot,
			m_Desc.exposureOverride,
			m_Desc.skyMipLevelOverride,
			"AssetThumbnail"));

		// What a submesh gets when the mesh names no material, or names one that will not load. A
		// fresh import names none at all: toBMesh drops the source's materials on purpose.
		m_DefaultMaterial = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
	});
}

AssetThumbnailCache::~AssetThumbnailCache()
{
	CancelShot();
	ReleaseGeometry();
	ReleaseMaterials();

	if (m_Desc.renderer == nullptr)
		return;

	// Member destruction would otherwise release these on whichever thread ran the destructor -- the
	// GUI thread -- flushing the command queue and freeing SceneView's allocations while the render
	// thread is still presenting the viewports.
	m_Desc.renderer->Invoke([&] {
		m_ThumbAssets.reset();

		if (m_DefaultMaterial.IsValid())
		{
			try
			{
				m_Desc.renderer->GetScene()->DeleteMaterial(m_DefaultMaterial);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to delete the default material: %s", e.what());
			}
		}

		m_SceneView    = nullptr;
		m_RenderTarget = nullptr;
	});
}

void
AssetThumbnailCache::SetAssets(game::AssetManager* assets)
{
	if (m_Assets == assets)
		return;

	// Hand the old project's assets back through the manager that acquired them, before it goes.
	CancelShot();
	ReleaseGeometry();
	ReleaseMaterials();

	m_Assets = assets;

	if (IsReady())
	{
		m_Desc.renderer->Invoke([&] {
			m_ThumbAssets.reset();
			if (m_Assets != nullptr)
				m_ThumbAssets = std::make_unique<game::AssetManager>(
					m_Desc.renderer->GetScene(),
					m_Assets->DataRoot(),
					game::AssetManagerOptions{ .hashedAsBlend = true });
		});
	}

	// The pixmaps were rendered with the old manager's materials, and the queued reads resolve
	// against the old data root.
	Clear();
	m_Queue.clear();
}

std::filesystem::path
AssetThumbnailCache::DataRoot() const
{
	return m_Assets != nullptr ? m_Assets->DataRoot() : std::filesystem::path();
}

bool
AssetThumbnailCache::CanThumbnail(const QString& path)
{
	return path.endsWith(c_MeshSuffix, Qt::CaseInsensitive) ||
	       path.endsWith(c_MaterialSuffix, Qt::CaseInsensitive);
}

std::string
AssetThumbnailCache::ToRelative(const QString& path) const
{
	const std::filesystem::path dataRoot = DataRoot();
	if (dataRoot.empty())
		return {};

	std::error_code ec;
	const auto      relative =
		std::filesystem::relative(std::filesystem::path(path.toStdWString()), dataRoot, ec);

	if (ec || relative.empty() || *relative.begin() == "..")
		return {};

	return relative.generic_string();
}

void
AssetThumbnailCache::Request(const QString& path)
{
	if (!IsReady() || !CanThumbnail(path))
		return;

	const bool material = path.endsWith(c_MaterialSuffix, Qt::CaseInsensitive);

	// A material is nothing but references into the data root, so without one there is nothing to
	// draw. A mesh still has its geometry, and falls back to the neutral default.
	if (material && m_Assets == nullptr)
		return;

	const std::optional<qint64> claimed = BeginRequest(path);
	if (!claimed)
		return;

	const qint64 stamp = *claimed;

	auto sink = [this, path, material, stamp](
					std::shared_ptr<assetlib::BMesh>       mesh,
					std::shared_ptr<CookedMeshes>          cooked,
					std::shared_ptr<game::TexturePrefetch> prefetch) {
		QMetaObject::invokeMethod(
			this,
			[this,
		     path,
		     material,
		     mesh     = std::move(mesh),
		     cooked   = std::move(cooked),
		     prefetch = std::move(prefetch),
		     stamp]() mutable {
				auto pending     = PendingRender();
				pending.path     = path;
				pending.type     = material ? ThumbnailType::kMaterial : ThumbnailType::kMesh;
				pending.mesh     = std::move(mesh);
				pending.cooked   = std::move(cooked);
				pending.prefetch = std::move(prefetch);
				pending.stamp    = stamp;

				Enqueue(path, pending.type, std::move(pending));
			},
			Qt::QueuedConnection);
	};

	m_Pool.start(new LoadTask(
		path,
		ToRelative(path),
		material,
		DataRoot(),
		m_Desc.dimension * c_TextureSupersample,
		std::move(sink)));
}

void
AssetThumbnailCache::Enqueue(const QString& path, ThumbnailType type, PendingRender pending)
{
	// The claim can be gone by the time the read lands: a project switch cleared it, and this asset
	// was read against the project that closed.
	if (!IsClaimed(path))
		return;

	// The worker failed: no prefetch, and for a mesh no mesh or cook either.
	if (pending.prefetch == nullptr ||
	    (type == ThumbnailType::kMesh && (pending.mesh == nullptr || pending.cooked == nullptr)))
	{
		Abandon(path);
		return;
	}

	m_Queue.enqueue(std::move(pending));
	PumpQueue();
}

void
AssetThumbnailCache::PumpQueue()
{
	if (m_ShotInFlight)
		return;

	if (m_Queue.isEmpty())
	{
		// The batch is over, so the materials it shared can go back, and the frame loop no longer
		// needs to tick for us.
		ReleaseMaterials();
		DetachFromFrameLoop();
		return;
	}

	// The claim ends when the shot's completion lands in OnShotDone, many frame-loop ticks from
	// now -- not here. A repaint in between misses on Lookup, and would otherwise restart the read
	// and render.
	m_ShotInFlight = true;
	AttachToFrameLoop();

	m_Desc.renderer->Post([this, pending = m_Queue.dequeue(), epoch = m_Epoch]() mutable {
		m_Shot.emplace();
		m_Shot->item  = std::move(pending);
		m_Shot->epoch = epoch;
	});
}

void
AssetThumbnailCache::OnShotDone(
	const QString& path,
	qint64         stamp,
	uint64_t       epoch,
	const QImage&  image)
{
	// Cancelled after it completed: the claim is gone and the image was rendered against whatever
	// the cancel replaced.
	if (epoch != m_Epoch)
		return;

	if (image.isNull())
		Abandon(path);
	else
		Store(path, QPixmap::fromImage(image), stamp);

	m_ShotInFlight = false;
	PumpQueue();
}

void
AssetThumbnailCache::Advance()
{
	if (!m_Shot.has_value())
		return;

	using Clock = std::chrono::steady_clock;

	const auto tickStart = Clock::now();
	double     buildMs   = 0.0;
	double     drawMs    = 0.0;
	bool       submitted = false;

	const auto msSince = [](Clock::time_point start) {
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	};

	const auto logIfSlow = [&](const QString& path) {
		const double ms = msSince(tickStart);
		if (ms > c_SlowTickMs)
			qWarning(
				"AssetThumbnail: %.1f ms tick on '%s' (build %.1f, draw %.1f, submitted %d)",
				ms,
				qPrintable(path),
				buildMs,
				drawMs,
				submitted);
	};

	Shot& shot = *m_Shot;
	try
	{
		if (!shot.built)
		{
			const auto buildStart = Clock::now();
			BuildShot(shot);
			buildMs = msSince(buildStart);

			// One frame: the capture reads the backbuffer the last DrawFrame presented, and that
			// frame's Scene::Update uploads this asset on its own list. Zero would capture a blank
			// backbuffer; the thumbnail goldens catch it.
			const auto drawStart = Clock::now();
			m_Desc.renderer->GetGraphics()->DrawFrame(m_RenderTarget, shot.job);
			drawMs = msSince(drawStart);

			shot.ticket = m_Desc.renderer->GetGraphics()->SubmitCapture(m_RenderTarget);
			shot.built  = true;
			submitted   = true;

			// Safe with the capture in flight: it copies the presented backbuffer, and the scene's
			// deletes are fence-deferred behind it.
			ReleaseGeometry();
			logIfSlow(shot.item.path);
			return;
		}

		auto image = m_Desc.renderer->GetGraphics()->TryResolveCapture(shot.ticket);
		if (!image.has_value())
			return;  // The GPU copy is still in flight; try again next tick.

		const QString path = shot.item.path;
		FinishShotOnPool(shot, std::move(*image));
		logIfSlow(path);
	}
	catch (const std::exception& e)
	{
		qWarning("AssetThumbnail: cannot render '%s': %s", qPrintable(shot.item.path), e.what());
		AbortShot(shot);
	}
	catch (...)
	{
		qWarning("AssetThumbnail: cannot render '%s'", qPrintable(shot.item.path));
		AbortShot(shot);
	}

	m_Shot.reset();
}

void
AssetThumbnailCache::AbortShot(const Shot& shot)
{
	// A no-op unless the failure left the readback in flight: resolve frees the slot on its own
	// throw path, and a shot that never submitted has no ticket.
	m_Desc.renderer->GetGraphics()->DiscardCapture(shot.ticket);
	ReleaseGeometry();
	FinishShot(shot, QImage());
}

void
AssetThumbnailCache::FinishShotOnPool(const Shot& shot, assetlib::ImageData image)
{
	// The capture is the full supersampled frame; converting and box-filtering it is milliseconds
	// of CPU, which is a worker's job, not the frame loop's or the GUI thread's.
	m_ScalePool.start(
		QRunnable::create([this,
	                       path      = shot.item.path,
	                       stamp     = shot.item.stamp,
	                       epoch     = shot.epoch,
	                       capture   = std::make_shared<assetlib::ImageData>(std::move(image)),
	                       dimension = m_Desc.dimension] {
			const QImage scaled = ToImage(*capture).scaled(
				static_cast<int>(dimension),
				static_cast<int>(dimension),
				Qt::IgnoreAspectRatio,
				Qt::SmoothTransformation);

			QMetaObject::invokeMethod(
				this,
				[this, path, stamp, epoch, scaled] { OnShotDone(path, stamp, epoch, scaled); },
				Qt::QueuedConnection);
		}));
}

void
AssetThumbnailCache::FinishShot(const Shot& shot, QImage image)
{
	QMetaObject::invokeMethod(
		this,
		[this,
	     path  = shot.item.path,
	     stamp = shot.item.stamp,
	     epoch = shot.epoch,
	     image = std::move(image)] { OnShotDone(path, stamp, epoch, image); },
		Qt::QueuedConnection);
}

void
AssetThumbnailCache::CancelShot()
{
	if (!IsReady())
		return;

	// A completion already queued to the UI thread carries the old epoch and is dropped on arrival.
	++m_Epoch;

	if (m_ShotInFlight)
	{
		// Queued behind any pending install, so it cannot race one in.
		m_Desc.renderer->Invoke([&] {
			if (!m_Shot.has_value())
				return;

			m_Desc.renderer->GetGraphics()->DiscardCapture(m_Shot->ticket);
			m_Shot.reset();
		});

		ReleaseGeometry();
		m_ShotInFlight = false;
	}

	DetachFromFrameLoop();
}

void
AssetThumbnailCache::AttachToFrameLoop()
{
	if (m_FrameLoopId != 0)
		return;

	m_FrameLoopId = m_Desc.renderer->AddViewport([this] { Advance(); });
}

void
AssetThumbnailCache::DetachFromFrameLoop()
{
	if (m_FrameLoopId == 0)
		return;

	m_Desc.renderer->RemoveViewport(m_FrameLoopId);
	m_FrameLoopId = 0;
}

bgl::MaterialHandle
AssetThumbnailCache::AcquireMaterial(std::string_view relPath, game::TexturePrefetch* prefetch)
{
	if (m_ThumbAssets == nullptr || relPath.empty())
		return m_DefaultMaterial;

	try
	{
		const bgl::MaterialHandle material = m_ThumbAssets->AcquireMaterial(relPath, prefetch);
		m_Materials.push_back(material);
		return material;
	}
	catch (const std::exception& e)
	{
		qWarning(
			"AssetThumbnail: cannot load material '%s': %s",
			std::string(relPath).c_str(),
			e.what());
		return m_DefaultMaterial;
	}
}

void
AssetThumbnailCache::BuildShot(Shot& shot)
{
	if (shot.item.type == ThumbnailType::kMesh)
		BuildMesh(shot);
	else
		BuildMaterial(shot);
}

void
AssetThumbnailCache::BuildMesh(Shot& shot)
{
	const assetlib::BMesh& mesh = *shot.item.mesh;

	bgl::IScene*     scene = m_Desc.renderer->GetScene().Get();
	bgl::ISceneView* view  = m_SceneView.Get();

	auto materials = std::vector<bgl::MaterialHandle>();
	materials.reserve(mesh.materials.size());
	for (const std::string& relPath : mesh.materials)
		materials.push_back(AcquireMaterial(relPath, shot.item.prefetch.get()));

	// A node instances a mesh and the same mesh can be instanced by several nodes, so upload each
	// mesh once and place an instance per referencing node, at that node's world transform.
	auto geomForMesh = std::unordered_map<uint32_t, uint32_t>();
	auto aabbMin     = glm::vec3(std::numeric_limits<float>::max());
	auto aabbMax     = glm::vec3(std::numeric_limits<float>::lowest());

	for (uint32_t nodeIndex = 0; nodeIndex < mesh.nodes.size(); ++nodeIndex)
	{
		const assetlib::Node& node = mesh.nodes[nodeIndex];
		if (node.mesh == assetlib::c_InvalidIndex || node.mesh >= mesh.meshes.size())
			continue;

		auto [it, inserted] =
			geomForMesh.try_emplace(node.mesh, static_cast<uint32_t>(m_Geoms.size()));
		if (inserted)
			m_Geoms.push_back(
				scene->AddStaticMeshGeom(std::move(shot.item.cooked->at(node.mesh)), materials));

		const glm::mat4               world = bmesh::GetInstanceTransform(mesh, nodeIndex);
		const bgl::MeshInstanceHandle instance =
			view->CreateStaticMeshInstance(m_Geoms[it->second], world);
		m_Instances.push_back(instance);

		const assetlib::Mesh& entry = mesh.meshes[node.mesh];
		for (uint32_t i = 0; i < entry.submeshCount; ++i)
		{
			const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];

			if (submesh.material >= materials.size())
				view->SetSubmeshMaterialOverride(instance, i, m_DefaultMaterial);

			bmesh::GrowBounds(world, submesh.aabbMin, submesh.aabbMax, aabbMin, aabbMax);
		}
	}

	if (m_Geoms.empty())
		throw std::runtime_error("no node references a mesh");

	const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
	const float     radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);

	FrameShot(shot, center, radius);
}

void
AssetThumbnailCache::BuildMaterial(Shot& shot)
{
	const std::string relPath = ToRelative(shot.item.path);
	if (relPath.empty())
		throw std::runtime_error("material does not lie under the project's data root");

	// The Material Editor previews on a sphere, so a material's thumbnail is the shape the user
	// authored it against.
	const bgl::MaterialHandle material = AcquireMaterial(relPath, shot.item.prefetch.get());

	m_Geoms.push_back(m_Desc.renderer->GetScene()->AddSphereGeom(32, 32, 1.0f, material));
	m_Instances.push_back(m_SceneView->CreateStaticMeshInstance(m_Geoms.back(), glm::mat4(1.0f)));

	FrameShot(shot, glm::vec3(0.0f), 1.0f);
}

void
AssetThumbnailCache::FrameShot(Shot& shot, const glm::vec3& center, float radius)
{
	// Pull back far enough that the bounding sphere fits the field of view with a margin. A mesh's
	// radius is a half-diagonal, so it over-estimates and frames itself loosely; a material's sphere
	// is exactly its radius and would otherwise sit hard against the edges.
	const float distance = radius * 3.1f;

	const glm::vec3 direction(
		std::cos(c_Pitch) * std::sin(c_Yaw),
		std::sin(c_Pitch),
		std::cos(c_Pitch) * std::cos(c_Yaw));

	auto camera = bgl::Camera();
	camera.LookAt(center + direction * distance, center, glm::vec3(0.0f, 1.0f, 0.0f))
		.Perspective(
			glm::radians(45.0f),
			1.0f,
			std::max(0.001f, radius * 0.01f),
			distance + radius * 50.0f);

	shot.job.camera   = camera;
	shot.job.view     = m_SceneView;
	shot.job.viewport = bgl::Viewport(
		static_cast<float>(m_Desc.dimension * c_Supersample),
		static_cast<float>(m_Desc.dimension * c_Supersample));
}

void
AssetThumbnailCache::ReleaseGeometry()
{
	if (!IsReady())
		return;

	m_Desc.renderer->Invoke([&] {
		for (const bgl::MeshInstanceHandle& instance : m_Instances)
		{
			if (!instance.IsValid())
				continue;

			try
			{
				m_SceneView->DeleteMeshInstance(instance);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to delete an instance: %s", e.what());
			}
		}

		for (const bgl::GeomHandle& geom : m_Geoms)
		{
			if (!geom.IsValid())
				continue;

			try
			{
				m_Desc.renderer->GetScene()->DeleteGeom(geom);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to delete a geom: %s", e.what());
			}
		}

		m_Instances.clear();
		m_Geoms.clear();
	});
}

void
AssetThumbnailCache::ReleaseMaterials()
{
	if (m_ThumbAssets == nullptr || !IsReady())
	{
		m_Materials.clear();
		return;
	}

	m_Desc.renderer->Invoke([&] {
		for (const bgl::MaterialHandle& material : m_Materials)
		{
			try
			{
				m_ThumbAssets->ReleaseMaterial(material);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to release a material: %s", e.what());
			}
		}

		m_Materials.clear();
	});
}
