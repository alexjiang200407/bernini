#include "MeshPreviewWindow.h"

#include "Windows/MeshEditor/lod_view.h"
#include "mesh_drop_import.h"
#include <QEvent>
#include <QVBoxLayout>
#include <core/err/util.h>
#include <editor_plugin_api/IEditorViewport.h>
#include <editor_sdk/mesh_load.h>
#include <editor_sdk/toon_content.h>

#include <editor_plugin_api/IEditorHost.h>
#include <editor_sdk/environment.h>

#include <assetlib_structs/Mesh.h>
#include <assetlib_structs/Node.h>
#include <bgl/types/GeomHandle.h>
#include <editor_sdk/BMeshUtil.h>
#include <editor_sdk/BackgroundTask.h>
#include <editor_sdk/mesh_drop.h>
#include <editor_sdk/mime_files.h>
#include <gamelib/Ray.h>

#include <QApplication>
#include <QDebug>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <assetlib/MeshBindings.h>
#include <assetlib/RegenMesh.h>
#include <assetlib/mesh_tangents.h>
#include <assetlib_structs/BMesh.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/PreparedStaticMesh.h>
#include <bgl/types/Camera.h>
#include <bgl/types/StaticMeshGeomDesc.h>
#include <bgl/types/StaticMeshInstanceDesc.h>
#include <cstddef>
#include <cstdint>
#include <editor_plugin_api/localize.h>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	QString
	ResolveMaterialPath(
		const assetlib::MeshBindings& bindings,
		uint32_t                      submesh,
		const std::filesystem::path&  dataRoot)
	{
		if (dataRoot.empty() || submesh >= bindings.submeshMaterials.size())
			return {};

		const std::string& relative = bindings.submeshMaterials[submesh];
		if (relative.empty())
			return {};

		const auto resolved = (dataRoot / relative).lexically_normal();
		return QString::fromStdWString(resolved.wstring());
	}

	QString
	FirstEnvironmentUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".benv");
	}
}

MeshPreviewWindow::MeshPreviewWindow(
	editor::IEditorHost& host,
	QWidget*             parent,
	editor::ViewportDesc rt,
	MeshPreviewEnv env) : QWidget(parent), m_Host(host), m_Viewport(host.CreateViewport(this, rt))
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_Viewport);
	m_Viewport->setAcceptDrops(true);
	m_Viewport->installEventFilter(this);
	m_DataRoot = host.GetStore().GetDataRoot();
	setAcceptDrops(true);

	// Wheel events only reach a widget that can take focus, and the camera needs them to dolly.
	setFocusPolicy(Qt::StrongFocus);

	m_Environment.configured = std::move(env);

	BindConfiguredEnvironment();

	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		bgl::IScene* scene = &context.scene;

		m_DefaultMaterial = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
	});

	ShowDefaultSphere();
}

MeshPreviewWindow::~MeshPreviewWindow()
{
	SetRenderingEnabled(false);
	ClearGeometry();
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		try
		{
			editor::ReleaseEnvironment(&context.scene, m_Environment);
			if (m_DefaultMaterial.IsValid())
				context.scene.DeleteMaterial(m_DefaultMaterial);
		}
		catch (const std::exception& error)
		{
			qWarning("MeshPreview: failed to release preview resources: %s", error.what());
		}
	});
}

void
MeshPreviewWindow::ClearGeometry()
{
	// Before anything is released, so a listener still sees the mesh these submeshes belong to.
	if (!m_MeshPath.empty())
		Q_EMIT GeometryAboutToChange();

	m_SubmeshToon.clear();
	m_Viewport->SetShowsToonContent(false);

	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		DeleteGeometry(context.scene, *view, m_Instances, m_Geoms);
	});

	if (m_ForcedLod.has_value())
		SetForcedLod(std::nullopt);

	m_Raycaster.Clear();
	m_Instances.clear();
	m_Geoms.clear();
	m_GeomLods.clear();
	m_ShownGeom = 0;
	m_LastLod.reset();
	m_SubmeshRefs.clear();
	m_SubmeshNames.clear();
	m_SubmeshMaterialPaths.clear();
	m_MeshPath.clear();  // LoadMesh sets it again once it has succeeded
	m_MeshBindings = {};
	m_MeshSourceKey.clear();
}

void
MeshPreviewWindow::DeleteGeometry(
	bgl::IScene&                     scene,
	bgl::ISceneView&                 view,
	std::span<const InstanceRef>     instances,
	std::span<const bgl::GeomHandle> geoms) noexcept
{
	for (const InstanceRef& instance : instances)
	{
		if (!instance.handle.IsValid())
			continue;

		try
		{
			view.DeleteMeshInstance(instance.handle);
		}
		catch (const std::exception& e)
		{
			qWarning("MeshPreview: failed to delete an instance: %s", e.what());
		}
	}

	for (const bgl::GeomHandle& geom : geoms)
	{
		if (!geom.IsValid())
			continue;

		try
		{
			scene.DeleteGeom(geom);
		}
		catch (const std::exception& e)
		{
			qWarning("MeshPreview: failed to delete a geom: %s", e.what());
		}
	}
}

uint32_t
MeshPreviewWindow::SourceSubmesh(uint32_t submeshIndex) const noexcept
{
	if (m_MeshPath.empty() || submeshIndex >= m_SubmeshRefs.size())
		return assetlib::c_InvalidIndex;
	return m_SubmeshRefs[submeshIndex].sourceSubmesh;
}

bool
MeshPreviewWindow::SubmeshHasTangent(uint32_t submeshIndex) const noexcept
{
	return submeshIndex >= m_SubmeshRefs.size() || m_SubmeshRefs[submeshIndex].hasTangent;
}

void
MeshPreviewWindow::ShowDefaultSphere()
{
	ClearGeometry();

	try
	{
		m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
			m_Geoms.push_back(context.scene.AddSphereGeom(32, 32, 1.0f, m_DefaultMaterial));
			m_GeomLods.emplace_back();
			m_Instances.push_back(
				{ view->CreateStaticMeshInstance(
					  bgl::StaticMeshInstanceDesc().SetGeom(m_Geoms.back())),
			      0 });

			// The sphere's triangles never exist on the CPU, so its raycast shadow is analytic.
			m_Raycaster.AddInstance(m_Raycaster.AddSphere(1.0f), glm::mat4(1.0f));
		});
	}
	catch (const std::exception& e)
	{
		qWarning("MeshPreview: could not show the default sphere: %s", e.what());

		ClearGeometry();
		Q_EMIT ShownLodsChanged();
		Q_EMIT GeometryChanged();
		return;
	}

	m_SubmeshRefs.push_back({ 0, 0, 0, true });  // AddSphereGeom writes a tangent
	// Not localized: the submesh selector's itemText() is read back as the stem of an auto-saved
	// material's filename (FlushEditedGraphs), so translating it would make the path locale-dependent.
	m_SubmeshNames = QStringList{ "Sphere" };  // procedural sphere: a single submesh

	m_SubmeshMaterialPaths = QStringList{ QString() };
	FocusOn(glm::vec3(0.0f), 1.0f);

	Q_EMIT ShownLodsChanged();
	Q_EMIT GeometryChanged();
}

void
MeshPreviewWindow::Reset()
{
	ShowDefaultSphere();
	RestoreConfiguredEnvironment();
}

MeshPreviewWindow::Upload
MeshPreviewWindow::UploadMesh(const assetlib::RegenMesh& loaded) const
{
	const assetlib::BMesh& mesh = loaded.mesh;

	auto upload = Upload();

	// A .bmesh spreads its submeshes across several meshes, and a node instances a mesh (the same
	// mesh can be instanced by several nodes). Each mesh is cooked once, and every node that
	// references one places an instance of it at the node's world transform.
	struct Placement
	{
		uint32_t  geomIndex;
		glm::mat4 world;
	};

	auto cooked       = std::vector<bgl::PreparedStaticMesh>();
	auto cookedMeshes = std::vector<uint32_t>();  // upload geom index -> mesh index
	auto placements   = std::vector<Placement>();
	auto geomForMesh  = std::unordered_map<uint32_t, uint32_t>();
	auto raycastGeoms = std::vector<uint32_t>();  // upload geom index -> raycaster geometry
	auto aabbMin      = glm::vec3(std::numeric_limits<float>::max());
	auto aabbMax      = glm::vec3(std::numeric_limits<float>::lowest());

	for (uint32_t nodeIndex = 0; nodeIndex < mesh.nodes.size(); ++nodeIndex)
	{
		const assetlib::Node& node = mesh.nodes[nodeIndex];
		if (!bmesh::ReferencesMesh(mesh, node))
			continue;

		auto [it, inserted] =
			geomForMesh.try_emplace(node.mesh, static_cast<uint32_t>(cooked.size()));
		if (inserted)
		{
			cooked.push_back(bgl::CookStaticMesh(mesh, node.mesh));
			cookedMeshes.push_back(node.mesh);
			upload.geomLods.push_back(editor::LodsOf(mesh, node.mesh));
			raycastGeoms.push_back(upload.raycaster.AddMesh(mesh, node.mesh));

			// Name each of this mesh's submeshes once, in the order the selector shows them.
			const assetlib::Mesh& entry = mesh.meshes[node.mesh];
			for (uint32_t i = 0; i < entry.submeshCount; ++i)
			{
				const assetlib::Submesh& submesh = mesh.submeshes[entry.firstSubmesh + i];

				const std::string_view pooled = mesh.stringPool.at(submesh.nameOffset);
				auto name = QString::fromUtf8(pooled.data(), static_cast<qsizetype>(pooled.size()));
				// Not localized, like the sphere's "Sphere": this too is read back as a filename
				// stem for an unnamed submesh's auto-saved material.
				if (name.isEmpty())
					name = QString("Submesh %1").arg(upload.submeshNames.size());
				upload.submeshNames << name;
				upload.submeshMaterialPaths
					<< ResolveMaterialPath(loaded.bindings, entry.firstSubmesh + i, m_DataRoot);
				upload.submeshRefs.push_back(
					{ it->second, i, entry.firstSubmesh + i, assetlib::hasTangent(submesh) });
			}
		}

		const glm::mat4 world = bmesh::GetInstanceTransform(mesh, nodeIndex);
		placements.push_back({ it->second, world });
		upload.raycaster.AddInstance(raycastGeoms[it->second], world);

		bmesh::GrowBoundsForMesh(mesh, node.mesh, world, aabbMin, aabbMax);
	}

	if (cooked.empty())
		core::throw_runtime_error("no node references a mesh");

	upload.center = (aabbMin + aabbMax) * 0.5f;
	upload.radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);

	try
	{
		m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
			for (size_t geom = 0; geom < cooked.size(); ++geom)
			{
				upload.geoms.push_back(
					context.scene.AddStaticMeshGeom(std::move(cooked[geom]), {}));

				const uint32_t submeshCount = mesh.meshes[cookedMeshes[geom]].submeshCount;
				for (uint32_t i = 0; i < submeshCount; ++i)
					context.scene.SetSubmeshMaterial(upload.geoms.back(), i, m_DefaultMaterial);
			}

			for (const Placement& placement : placements)
				upload.instances.push_back(
					{ view->CreateStaticMeshInstance(
						  bgl::StaticMeshInstanceDesc()
							  .SetGeom(upload.geoms[placement.geomIndex])
							  .SetTransform(placement.world)),
				      placement.geomIndex,
				      placement.world });
		});
	}
	catch (...)
	{
		// The upload's own failure is the one to report, so a cleanup that fails as well only logs.
		try
		{
			m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
				DeleteGeometry(context.scene, *view, upload.instances, upload.geoms);
			});
		}
		catch (const std::exception& e)
		{
			qWarning("MeshPreview: could not delete a failed upload: %s", e.what());
		}
		throw;
	}

	return upload;
}

void
MeshPreviewWindow::LoadMesh(const std::filesystem::path& path)
{
	auto          loaded = assetlib::RegenMesh();
	auto          upload = Upload();
	const QString name   = QString::fromStdString(path.filename().string());

	// Before the screen: the upload lands in the scene this view draws, beside nothing of the last
	// mesh, and only the GUI thread may say the last mesh is going.
	ClearGeometry();

	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		editor::Localize(
			m_Host.GetLanguageResolver(),
			"bernini.material.loading_mesh_title",
			{ name },
			"Loading {0}"),
		[&](background::Progress& progress) {
			progress.Report(
				0,
				0,
				editor::Localize(
					m_Host.GetLanguageResolver(),
					"bernini.material.reading_mesh_progress",
					"Reading mesh..."));
			loaded = editor::LoadMeshThroughSeam(m_Host.GetStore(), path);
			if (loaded.mesh.meshes.empty())
				core::throw_runtime_error("mesh contains no meshes");

			progress.Report(
				0,
				0,
				editor::Localize(
					m_Host.GetLanguageResolver(),
					"bernini.material.uploading_mesh_progress",
					"Uploading geometry..."));
			upload = UploadMesh(loaded);
		});

	if (!result.Completed())
	{
		qWarning(
			"MeshPreview: failed to load mesh '%s': %s",
			path.string().c_str(),
			qPrintable(result.error));

		QMessageBox::warning(
			window(),
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.material.load_mesh_title",
				"Load Mesh"),
			editor::Localize(
				m_Host.GetLanguageResolver(),
				"bernini.material.load_mesh_failed",
				{ name, result.error },
				"Could not load '{0}':\n\n{1}"));

		ShowDefaultSphere();
		return;
	}

	m_Geoms                = std::move(upload.geoms);
	m_GeomLods             = std::move(upload.geomLods);
	m_Instances            = std::move(upload.instances);
	m_SubmeshRefs          = std::move(upload.submeshRefs);
	m_SubmeshNames         = std::move(upload.submeshNames);
	m_SubmeshMaterialPaths = std::move(upload.submeshMaterialPaths);
	m_Raycaster            = std::move(upload.raycaster);
	m_MeshPath             = path;
	m_MeshBindings         = std::move(loaded.bindings);
	m_MeshSourceKey        = std::move(loaded.sourceKey);

	FocusOn(upload.center, upload.radius);

	Q_EMIT ShownLodsChanged();
	Q_EMIT GeometryChanged();
}

void
MeshPreviewWindow::SetSubmeshMaterial(uint32_t submeshIndex, bgl::MaterialHandle material)
{
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		m_SubmeshToon.resize(m_SubmeshRefs.size(), false);
		if (submeshIndex < m_SubmeshToon.size())
			m_SubmeshToon[submeshIndex] =
				material.IsValid() && editor::IsToonMaterial(context.graphics, material);

		if (!material.IsValid() || submeshIndex >= m_SubmeshRefs.size())
			return;

		const SubmeshRef& ref = m_SubmeshRefs[submeshIndex];
		if (ref.geomIndex >= m_Geoms.size() || !m_Geoms[ref.geomIndex].IsValid())
			return;

		try
		{
			// An override on the instances, not Scene::SetSubmeshMaterial on the geom. The geom's
			// default is the *asset's* material: rewriting it here would edit the .bmesh's binding as
			// a side effect of typing.
			for (const SubmeshTarget& target :
			     GetInstanceTargets(m_SubmeshRefs, m_Instances, submeshIndex))
			{
				view->SetSubmeshMaterialOverride(target.instance, target.submeshIndex, material);
			}
		}
		catch (const std::exception& e)
		{
			qWarning("MeshPreview: SetSubmeshMaterial(%u) failed: %s", submeshIndex, e.what());
		}
	});

	m_Viewport->SetShowsToonContent(std::ranges::find(m_SubmeshToon, true) != m_SubmeshToon.end());
}

std::vector<MeshPreviewWindow::SubmeshTarget>
MeshPreviewWindow::GetInstanceTargets(
	std::span<const SubmeshRef>  refs,
	std::span<const InstanceRef> instances,
	uint32_t                     submeshIndex)
{
	auto targets = std::vector<SubmeshTarget>();

	if (submeshIndex >= refs.size())
		return targets;

	targets.reserve(instances.size());

	const SubmeshRef& ref = refs[submeshIndex];
	for (const InstanceRef& instance : instances)
	{
		if (instance.geomIndex == ref.geomIndex && instance.handle.IsValid())
			targets.push_back({ instance.handle, ref.localSubmesh });
	}

	return targets;
}

void
MeshPreviewWindow::SetSelectedSubmesh(std::optional<uint32_t> submeshIndex)
{
	const uint32_t shown = submeshIndex.has_value() && *submeshIndex < m_SubmeshRefs.size() ?
	                           m_SubmeshRefs[*submeshIndex].geomIndex :
	                           0;
	if (shown != m_ShownGeom)
	{
		m_ShownGeom = shown;
		m_LastLod.reset();
		Q_EMIT ShownLodsChanged();
	}

	m_Viewport->Invoke([&](editor::RenderContext&, const bgl::SceneViewRef& view) {
		try
		{
			view->ClearSelection();

			if (!submeshIndex.has_value())
				return;

			for (const SubmeshTarget& target :
			     GetInstanceTargets(m_SubmeshRefs, m_Instances, *submeshIndex))
			{
				view->SetSubmeshSelected(target.instance, target.submeshIndex, true);
			}
		}
		catch (const std::exception& e)
		{
			qWarning(
				"MeshPreview: SetSelectedSubmesh(%u) failed: %s",
				submeshIndex.value_or(0xFFFFFFFFu),
				e.what());
		}
	});
}

bool
MeshPreviewWindow::AcceptsDrop(const QMimeData* mime)
{
	return editor::IsMeshDrag(mime) || !FirstEnvironmentUrl(mime).isEmpty();
}

bool
MeshPreviewWindow::TakeDrop(const QMimeData* mime)
{
	if (const QString environment = FirstEnvironmentUrl(mime); !environment.isEmpty())
	{
		SetEnvironment(environment.toStdString());
		return true;
	}

	const QString mesh =
		editor::MeshForDrop(m_Host, mime, QString::fromStdWString(m_DataRoot.wstring()));
	if (mesh.isEmpty())
		return false;

	LoadMesh(std::filesystem::path(mesh.toStdWString()));
	return true;
}

void
MeshPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (AcceptsDrop(event->mimeData()))
		event->acceptProposedAction();
}

void
MeshPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (AcceptsDrop(event->mimeData()))
		event->acceptProposedAction();
}

void
MeshPreviewWindow::dropEvent(QDropEvent* event)
{
	if (TakeDrop(event->mimeData()))
		event->acceptProposedAction();
}

void
MeshPreviewWindow::SetEnvironment(const std::string& benvPath)
{
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		editor::BindEnvironment(
			&context.scene,
			view.Get(),
			m_Environment,
			benvPath,
			m_Host.GetStore(),
			"MeshPreview");
	});
}

void
MeshPreviewWindow::BindConfiguredEnvironment()
{
	m_Viewport->Invoke([&](editor::RenderContext& context, const bgl::SceneViewRef& view) {
		if (!m_Environment.configured.environmentMap.empty())
		{
			try
			{
				auto external = std::optional<assetlib::AssetStore>();
				if (!m_Environment.configured.dataRoot.empty() &&
				    m_Environment.configured.dataRoot != m_Host.GetStore().GetDataRoot())
					external.emplace(m_Environment.configured.dataRoot);
				const auto& store = external ? *external : m_Host.GetStore();

				editor::BindEnvironment(
					&context.scene,
					view.Get(),
					m_Environment,
					m_Environment.configured.environmentMap,
					store,
					"MeshPreview");
			}
			catch (const std::exception& error)
			{
				qWarning("Configured environment could not be loaded: %s", error.what());
			}
		}
	});
}

void
MeshPreviewWindow::RestoreConfiguredEnvironment()
{
	const std::optional<std::string> restore = editor::GetEnvironmentToRestore(m_Environment);
	if (!restore)
		return;

	BindConfiguredEnvironment();
}

void
MeshPreviewWindow::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	UpdateCamera();
}

void
MeshPreviewWindow::FocusOn(const glm::vec3& center, float radius)
{
	m_Orbit.FocusOn(center, radius);
	UpdateCamera();
}

void
MeshPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_DragButton   = event->button();
	m_LastMousePos = event->position().toPoint();
	m_PressPos     = m_LastMousePos;
	m_Dragged      = false;
}

void
MeshPreviewWindow::mouseReleaseEvent(QMouseEvent* event)
{
	// A press the camera never followed is a click: pick what it landed on.
	if (event->button() == Qt::LeftButton && m_DragButton == Qt::LeftButton && !m_Dragged)
		PickAt(event->position());

	m_DragButton = Qt::NoButton;
}

void
MeshPreviewWindow::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos   = event->position().toPoint();
	const QPoint delta = pos - m_LastMousePos;
	m_LastMousePos     = pos;

	if (m_DragButton != Qt::NoButton &&
	    (pos - m_PressPos).manhattanLength() > QApplication::startDragDistance())
		m_Dragged = true;

	if (m_DragButton == Qt::LeftButton)
	{
		m_Orbit.Orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	}
	else if (m_DragButton == Qt::MiddleButton || m_DragButton == Qt::RightButton)
	{
		m_Orbit.Pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	}
	else
	{
		return;
	}

	UpdateCamera();
}

void
MeshPreviewWindow::wheelEvent(QWheelEvent* event)
{
	m_Orbit.Dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
	UpdateCamera();
	event->accept();
}

void
MeshPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	m_Camera = m_Orbit.GetCamera(aspect);
	m_Viewport->SetCamera(m_Camera);
	Q_EMIT ViewChanged();
}

const editor::MeshLods*
MeshPreviewWindow::GetShownLods() const noexcept
{
	if (m_ShownGeom >= m_GeomLods.size() || m_GeomLods[m_ShownGeom].minPixels.empty())
		return nullptr;
	return &m_GeomLods[m_ShownGeom];
}

std::optional<editor::LodReadout>
MeshPreviewWindow::ReadShownLod()
{
	const editor::MeshLods* lods = GetShownLods();
	if (lods == nullptr)
		return std::nullopt;

	const auto placement = std::ranges::find(m_Instances, m_ShownGeom, &InstanceRef::geomIndex);
	if (placement == m_Instances.end())
		return std::nullopt;

	return editor::ReadLodInView(
		*lods,
		placement->world,
		m_Camera,
		m_Orbit.GetEyePosition(),
		m_Viewport->GetRenderHeight(),
		m_ForcedLod,
		m_LastLod);
}

void
MeshPreviewWindow::SetForcedLod(std::optional<uint32_t> level)
{
	m_ForcedLod = level;
	editor::PinLod(*m_Viewport, level);
}

void
MeshPreviewWindow::PickAt(const QPointF& pixel)
{
	if (width() <= 0 || height() <= 0)
		return;

	// Logical pixels against the logical size: NDC is scale-invariant, so the device pixel ratio
	// and the render scale never enter into it.
	const game::Ray ray = game::RayThroughPixel(
		m_Camera.GetViewProjection(),
		glm::vec2(pixel.x(), pixel.y()),
		glm::vec2(width(), height()));

	int index = -1;
	if (const auto hit = m_Raycaster.Raycast(ray);
	    hit.has_value() && hit->instance < m_Instances.size())
	{
		index =
			SelectorIndexOf(m_SubmeshRefs, m_Instances[hit->instance].geomIndex, hit->submeshIndex);
	}

	Q_EMIT SubmeshPicked(index);
}

int
MeshPreviewWindow::SelectorIndexOf(
	std::span<const SubmeshRef> refs,
	uint32_t                    geomIndex,
	uint32_t                    localSubmesh)
{
	for (size_t i = 0; i < refs.size(); ++i)
	{
		if (refs[i].geomIndex == geomIndex && refs[i].localSubmesh == localSubmesh)
			return static_cast<int>(i);
	}
	return -1;
}

QStringList
MeshPreviewWindow::GetHeldOpenPaths() const
{
	return editor::GetHeldOpenEnvironment(m_Environment);
}

bool
MeshPreviewWindow::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != m_Viewport)
		return QWidget::eventFilter(watched, event);
	switch (event->type())
	{
	case QEvent::MouseButtonPress:
		mousePressEvent(static_cast<QMouseEvent*>(event));
		return true;
	case QEvent::MouseButtonRelease:
		mouseReleaseEvent(static_cast<QMouseEvent*>(event));
		return true;
	case QEvent::MouseMove:
		mouseMoveEvent(static_cast<QMouseEvent*>(event));
		return true;
	case QEvent::Wheel:
		wheelEvent(static_cast<QWheelEvent*>(event));
		return true;
	case QEvent::DragEnter:
		dragEnterEvent(static_cast<QDragEnterEvent*>(event));
		return true;
	case QEvent::DragMove:
		dragMoveEvent(static_cast<QDragMoveEvent*>(event));
		return true;
	case QEvent::Drop:
		dropEvent(static_cast<QDropEvent*>(event));
		return true;
	case QEvent::Resize:
		UpdateCamera();
		break;
	default:
		break;
	}
	return QWidget::eventFilter(watched, event);
}
