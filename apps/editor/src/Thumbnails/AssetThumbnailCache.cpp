#include "Thumbnails/AssetThumbnailCache.h"

#include "Mesh/BMeshUtil.h"
#include "Render/ContextWorker.h"
#include "Render/Renderer.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QImage>
#include <QRunnable>
#include <QTimer>

#include <assetlib/bmaterial_io.h>
#include <assetlib/bmesh_io.h>
#include <assetlib/image_io.h>
#include <bgl/Camera.h>
#include <bgl/RenderJob.h>
#include <bgl/SkyboxDesc.h>
#include <bgl/Viewport.h>

namespace
{
	// The backbuffer and the per-frame upload rings are c_SwapchainImageCount deep, so a single draw can
	// present a slot the asset's instance data has not reached yet. Draw enough to fill every slot.
	// Must track bgl's c_SwapchainImageCount, which is not public; the thumbnail goldens are what
	// catch this being too low.
	constexpr int c_WarmupFrames = 2;

	// A three-quarter view reads better than a straight-on one: it shows a silhouette and some depth.
	constexpr float c_Yaw   = 0.6f;
	constexpr float c_Pitch = 0.45f;

	constexpr auto c_MeshSuffix     = ".bmesh";
	constexpr auto c_MaterialSuffix = ".bmaterial";

	bgl::TextureAssetHandle
	TryLoadTexture(bgl::IScene* scene, const std::string& path)
	{
		if (path.empty())
			return {};
		try
		{
			return scene->AddTextureAsset(assetlib::loadKTX2(path));
		}
		catch (const std::exception& e)
		{
			qWarning("AssetThumbnail: failed to load '%s': %s", path.c_str(), e.what());
			return {};
		}
	}

	// Decodes every texture the material at `relPath` names into `out`. A texture that will not decode
	// is left out: AcquireTexture then falls back to reading the file, and reports it there.
	void
	PrefetchMaterial(
		const std::filesystem::path& dataRoot,
		const std::string&           relPath,
		game::TexturePrefetch&       out)
	{
		if (relPath.empty())
			return;

		const assetlib::BMaterial material = assetlib::loadMaterial(dataRoot / relPath);

		for (const std::string& texture : game::materialTextures(material))
		{
			if (texture.empty() || out.contains(texture))
				continue;

			try
			{
				out.emplace(texture, assetlib::loadKTX2(dataRoot / texture));
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: cannot decode '%s': %s", texture.c_str(), e.what());
			}
		}
	}

	/**
	 * Reads an asset and decodes the textures it needs -- everything about a thumbnail that does not
	 * touch the GPU, which is everything expensive except the draw itself.
	 *
	 * A KTX2 decode transcodes a whole Basis mip chain, and is the single costliest step; the upload
	 * that follows is the only part that has to be on the render thread. So the split is here.
	 */
	class LoadTask : public QRunnable
	{
	public:
		using Sink = std::function<
			void(std::shared_ptr<assetlib::BMesh>, std::shared_ptr<game::TexturePrefetch>)>;

		LoadTask(
			QString               path,
			std::string           relPath,
			bool                  isMaterial,
			std::filesystem::path dataRoot,
			Sink                  sink) :
			m_Path(std::move(path)), m_RelPath(std::move(relPath)), m_IsMaterial(isMaterial),
			m_DataRoot(std::move(dataRoot)), m_Sink(std::move(sink))
		{
			setAutoDelete(true);
		}

		void
		run() override
		{
			std::shared_ptr<assetlib::BMesh>       mesh;
			std::shared_ptr<game::TexturePrefetch> prefetch;

			try
			{
				prefetch = std::make_shared<game::TexturePrefetch>();

				if (m_IsMaterial)
				{
					PrefetchMaterial(m_DataRoot, m_RelPath, *prefetch);
				}
				else
				{
					mesh = std::make_shared<assetlib::BMesh>(
						assetlib::load(std::filesystem::path(m_Path.toStdWString())));

					if (mesh->meshes.empty())
						throw std::runtime_error("mesh contains no meshes");

					// Without a data root the mesh's materials cannot be resolved at all, and every
					// submesh falls back to the neutral default -- so there is nothing to decode.
					if (!m_DataRoot.empty())
					{
						for (const std::string& relPath : mesh->materials)
							PrefetchMaterial(m_DataRoot, relPath, *prefetch);
					}
				}
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: cannot read '%s': %s", qPrintable(m_Path), e.what());
				mesh.reset();
				prefetch.reset();
			}

			m_Sink(std::move(mesh), std::move(prefetch));
		}

	private:
		QString               m_Path;
		std::string           m_RelPath;
		bool                  m_IsMaterial = false;
		std::filesystem::path m_DataRoot;
		Sink                  m_Sink;
	};
}

AssetThumbnailCache::AssetThumbnailCache(AssetThumbnailDesc desc, QObject* parent) :
	QObject(parent), m_Desc(std::move(desc))
{
	// Reading a .bmesh is I/O plus a parse; two at a time keeps the explorer responsive without
	// queueing up more decoded meshes than the GPU drain can retire.
	m_Pool.setMaxThreadCount(2);

	// No device (the editor runs without one in tests): stay inert. Lookup then always misses and
	// Request is a no-op, so callers need no special case.
	if (m_Desc.renderer == nullptr)
		return;

	try
	{
		m_Worker = std::make_unique<ContextWorker>(*m_Desc.renderer, m_Desc.sceneDesc);
	}
	catch (const std::exception& e)
	{
		qWarning("AssetThumbnail: no render context, thumbnails disabled: %s", e.what());
		return;
	}

	m_Worker->Invoke([&] {
		try
		{
			auto rtDesc     = bgl::RenderTargetDesc();
			rtDesc.width    = static_cast<int>(m_Desc.dimension);
			rtDesc.height   = static_cast<int>(m_Desc.dimension);
			rtDesc.headless = true;

			m_RenderTarget = m_Worker->GetContext()->CreateRenderTarget(rtDesc);
			m_SceneView =
				m_Worker->GetGraphics()->CreateSceneView(m_Worker->GetScene(), m_Desc.maxInstances);
		}
		catch (const std::exception& e)
		{
			qWarning("AssetThumbnail: no render target, thumbnails disabled: %s", e.what());
			m_RenderTarget = nullptr;
			m_SceneView    = nullptr;
			return;
		}

		bgl::IScene*     scene = m_Worker->GetScene().Get();
		bgl::ISceneView* view  = m_SceneView.Get();
		view->SetExposure(m_Desc.exposure);

		const auto irradiance = TryLoadTexture(scene, m_Desc.irradiance);
		const auto prefilter  = TryLoadTexture(scene, m_Desc.prefilter);
		const auto brdfLut    = TryLoadTexture(scene, m_Desc.brdfLut);
		if (irradiance.textureSlot && prefilter.textureSlot && brdfLut.textureSlot)
		{
			try
			{
				view->SetEnvironmentMap({ irradiance, prefilter, brdfLut });
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: SetEnvironmentMap failed: %s", e.what());
			}
		}

		if (const auto skybox = TryLoadTexture(scene, m_Desc.skybox); skybox.textureSlot)
		{
			try
			{
				view->SetSkyBox({ skybox });
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: SetSkyBox failed: %s", e.what());
			}
		}

		// What a submesh gets when the mesh names no material, or names one that will not load. A
		// fresh import names none at all: toBMesh drops the source's materials on purpose.
		m_DefaultMaterial = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
	});

	// Target creation may have failed inside the closure and left the cache inert.
	if (!IsReady())
		return;

	m_DrainTimer = new QTimer(this);
	m_DrainTimer->setInterval(0);
	connect(m_DrainTimer, &QTimer::timeout, this, &AssetThumbnailCache::DrainOne);
}

AssetThumbnailCache::~AssetThumbnailCache()
{
	ReleaseGeometry();
	ReleaseMaterials();

	if (m_Worker == nullptr)
		return;

	// Everything context-affine is released on the worker thread that owns it; member destruction
	// would otherwise release it from the GUI thread.
	m_Worker->Invoke([&] {
		if (m_DefaultMaterial.IsValid())
		{
			try
			{
				m_Worker->GetScene()->DeleteMaterial(m_DefaultMaterial);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to delete the default material: %s", e.what());
			}
		}

		m_Assets.reset();
		m_SceneView    = nullptr;
		m_RenderTarget = nullptr;
	});

	// Releases the scene and context on the worker thread, then joins it.
	m_Worker.reset();
}

void
AssetThumbnailCache::SetDataRoot(const std::filesystem::path& dataRoot)
{
	if (DataRoot() == dataRoot)
		return;

	// Hand the old project's assets back through the manager that owns them, before we let go of it.
	ReleaseGeometry();
	ReleaseMaterials();

	if (IsReady())
	{
		// ~AssetManager hands every asset it still holds back to the scene, so both it and its
		// replacement live on the worker thread with the scene they serve.
		m_Worker->Invoke([&] {
			m_Assets.reset();

			if (dataRoot.empty())
				return;

			try
			{
				m_Assets = std::make_unique<game::AssetManager>(m_Worker->GetScene(), dataRoot);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: cannot open the data root: %s", e.what());
			}
		});
	}

	m_Cache.clear();
	m_Queue.clear();
	m_InFlight.clear();
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

qint64
AssetThumbnailCache::FileStamp(const QString& path)
{
	const QFileInfo info(path);
	return info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
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

QPixmap
AssetThumbnailCache::Lookup(const QString& path) const
{
	const CachedThumbnail* entry = m_Cache.object(path);
	if (entry == nullptr || entry->stamp != FileStamp(path))
		return {};

	return entry->pixmap;
}

void
AssetThumbnailCache::Request(const QString& path)
{
	if (!IsReady() || path.isEmpty() || m_InFlight.contains(path) || !CanThumbnail(path))
		return;

	const bool material = path.endsWith(c_MaterialSuffix, Qt::CaseInsensitive);

	// A material is nothing but references into the data root, so without one there is nothing to
	// draw. A mesh still has its geometry, and falls back to the neutral default.
	if (material && m_Assets == nullptr)
		return;

	const qint64 stamp = FileStamp(path);

	const CachedThumbnail* entry = m_Cache.object(path);
	if (entry != nullptr)
	{
		if (entry->stamp == stamp)
			return;

		// Rebaked on disk since we rendered it. The editor is also the asset-cook host, so this is
		// reachable without ever closing the folder.
		m_Cache.remove(path);
	}

	m_InFlight.insert(path);

	auto sink = [this, path, material, stamp](
					std::shared_ptr<assetlib::BMesh>       mesh,
					std::shared_ptr<game::TexturePrefetch> prefetch) {
		QMetaObject::invokeMethod(
			this,
			[this,
		     path,
		     material,
		     mesh     = std::move(mesh),
		     prefetch = std::move(prefetch),
		     stamp]() mutable {
				auto pending     = PendingRender();
				pending.path     = path;
				pending.type     = material ? ThumbnailType::kMaterial : ThumbnailType::kMesh;
				pending.mesh     = std::move(mesh);
				pending.prefetch = std::move(prefetch);
				pending.stamp    = stamp;

				Enqueue(path, pending.type, std::move(pending));
			},
			Qt::QueuedConnection);
	};

	m_Pool.start(new LoadTask(path, ToRelative(path), material, DataRoot(), std::move(sink)));
}

void
AssetThumbnailCache::Enqueue(const QString& path, ThumbnailType type, PendingRender pending)
{
	// The worker failed: no prefetch, and for a mesh no mesh either.
	if (pending.prefetch == nullptr || (type == ThumbnailType::kMesh && pending.mesh == nullptr))
	{
		m_InFlight.remove(path);
		return;
	}

	m_Queue.enqueue(std::move(pending));

	if (!m_DrainTimer->isActive())
		m_DrainTimer->start();
}

void
AssetThumbnailCache::DrainOne()
{
	if (m_Queue.isEmpty())
	{
		// The batch is over, so the materials it shared can go back.
		ReleaseMaterials();
		m_DrainTimer->stop();
		return;
	}

	const PendingRender pending = m_Queue.dequeue();
	m_InFlight.remove(pending.path);

	const QImage image = m_Worker->Invoke([&] {
		QImage img;
		try
		{
			img = pending.type == ThumbnailType::kMesh ? RenderMesh(pending) :
			                                             RenderMaterial(pending);
		}
		catch (const std::exception& e)
		{
			qWarning("AssetThumbnail: cannot render '%s': %s", qPrintable(pending.path), e.what());
		}

		ReleaseGeometry();
		return img;
	});

	if (image.isNull())
		return;

	const QPixmap thumbnail = QPixmap::fromImage(image);

	const int costKb = std::max(
		1,
		static_cast<int>(
			(static_cast<qint64>(thumbnail.width()) * thumbnail.height() * thumbnail.depth() / 8) /
			1024));

	m_Cache.insert(pending.path, new CachedThumbnail{ thumbnail, pending.stamp }, costKb);
	Q_EMIT ThumbnailReady(pending.path, thumbnail);
}

bgl::MaterialHandle
AssetThumbnailCache::AcquireMaterial(std::string_view relPath, game::TexturePrefetch* prefetch)
{
	if (m_Assets == nullptr || relPath.empty())
		return m_DefaultMaterial;

	try
	{
		const bgl::MaterialHandle material = m_Assets->AcquireMaterial(relPath, prefetch);
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

QImage
AssetThumbnailCache::RenderMesh(const PendingRender& pending)
{
	const assetlib::BMesh& mesh = *pending.mesh;

	bgl::IScene*     scene = m_Worker->GetScene().Get();
	bgl::ISceneView* view  = m_SceneView.Get();

	auto materials = std::vector<bgl::MaterialHandle>();
	materials.reserve(mesh.materials.size());
	for (const std::string& relPath : mesh.materials)
		materials.push_back(AcquireMaterial(relPath, pending.prefetch.get()));

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
			m_Geoms.push_back(scene->AddStaticMesh(mesh, node.mesh, materials));

		const glm::mat4               world = bmesh::WorldTransform(mesh, nodeIndex);
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

	return Shoot(center, radius);
}

QImage
AssetThumbnailCache::RenderMaterial(const PendingRender& pending)
{
	const std::string relPath = ToRelative(pending.path);
	if (relPath.empty())
		throw std::runtime_error("material does not lie under the project's data root");

	// The Material Editor previews on a sphere, so a material's thumbnail is the shape the user
	// authored it against.
	const bgl::MaterialHandle material = AcquireMaterial(relPath, pending.prefetch.get());

	m_Geoms.push_back(m_Worker->GetScene()->AddSphereGeom(32, 32, 1.0f, material));
	m_Instances.push_back(m_SceneView->CreateStaticMeshInstance(m_Geoms.back(), glm::mat4(1.0f)));

	return Shoot(glm::vec3(0.0f), 1.0f);
}

QImage
AssetThumbnailCache::Shoot(const glm::vec3& center, float radius)
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

	auto job   = bgl::RenderJob();
	job.camera = camera;
	job.view   = m_SceneView;
	job.viewport =
		bgl::Viewport(static_cast<float>(m_Desc.dimension), static_cast<float>(m_Desc.dimension));

	for (int i = 0; i < c_WarmupFrames; ++i) m_Worker->GetContext()->DrawFrame(m_RenderTarget, job);

	const assetlib::ImageData shot = m_Worker->GetContext()->ScreenshotToMemory(m_RenderTarget);

	// Deep-copy: `shot` owns the pixels and dies at the end of this scope.
	return QImage(
			   reinterpret_cast<const uchar*>(shot.pixels.data()),
			   static_cast<int>(shot.width),
			   static_cast<int>(shot.height),
			   static_cast<qsizetype>(shot.subresources.front().rowPitch),
			   QImage::Format_RGBA8888)
	    .copy();
}

void
AssetThumbnailCache::ReleaseGeometry()
{
	if (!IsReady())
		return;

	m_Worker->Invoke([&] {
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
				m_Worker->GetScene()->DeleteGeom(geom);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to delete a geom: %s", e.what());
			}
		}
	});

	m_Instances.clear();
	m_Geoms.clear();
}

void
AssetThumbnailCache::ReleaseMaterials()
{
	if (m_Assets == nullptr || !IsReady())
	{
		m_Materials.clear();
		return;
	}

	m_Worker->Invoke([&] {
		for (const bgl::MaterialHandle& material : m_Materials)
		{
			try
			{
				m_Assets->ReleaseMaterial(material);
			}
			catch (const std::exception& e)
			{
				qWarning("AssetThumbnail: failed to release a material: %s", e.what());
			}
		}
	});

	m_Materials.clear();
}
