#include "MaterialPreviewWindow.h"

#include "Async/BackgroundTask.h"
#include "Mesh/BMeshUtil.h"
#include "Render/Renderer.h"
#include "Render/environment.h"
#include "Windows/MaterialEditor/mesh_preview.h"
#include "util/mime_files.h"

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

#include <assetlib/bmesh_io.h>
#include <assetlib_structs/BMesh.h>
#include <bgl/Camera.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/PreparedStaticMesh.h>

namespace
{
	QString
	FirstMeshUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".bmesh");
	}

	QString
	FirstEnvironmentUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".benv");
	}
}

MaterialPreviewWindow::MaterialPreviewWindow(
	QWidget*               parent,
	RenderTargetWindowDesc rt,
	MaterialPreviewEnv     env) : RenderTargetWindow(parent, std::move(rt))
{
	setAcceptDrops(true);

	// Wheel events only reach a widget that can take focus, and the camera needs them to dolly.
	setFocusPolicy(Qt::StrongFocus);

	m_Environment.configured = std::move(env);

	m_DefaultMaterial = GetRenderer()->Invoke([&] {
		bgl::IScene* scene = GetPreviewScene();

		editor::BindEnvironment(
			scene,
			GetPreviewView(),
			m_Environment,
			m_Environment.configured.environmentMap,
			m_Environment.configured.dataRoot,
			"MaterialPreview");

		return scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 1.0f });
	});

	ShowDefaultSphere();
}

void
MaterialPreviewWindow::ClearGeometry()
{
	GetRenderer()->Invoke([&] {
		for (const InstanceRef& instance : m_Instances)
		{
			if (!instance.handle.IsValid())
				continue;

			try
			{
				GetPreviewView()->DeleteMeshInstance(instance.handle);
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialPreview: failed to delete an instance: %s", e.what());
			}
		}

		for (const bgl::GeomHandle& geom : m_Geoms)
		{
			if (!geom.IsValid())
				continue;

			try
			{
				GetPreviewScene()->DeleteGeom(geom);
			}
			catch (const std::exception& e)
			{
				qWarning("MaterialPreview: failed to delete a geom: %s", e.what());
			}
		}
	});

	m_Raycaster.Clear();
	m_Instances.clear();
	m_Geoms.clear();
	m_SubmeshRefs.clear();
	m_SubmeshNames.clear();
	m_SubmeshMaterialPaths.clear();
	m_MeshPath.clear();  // LoadMesh sets it again once it has succeeded
}

uint32_t
MaterialPreviewWindow::SourceSubmesh(uint32_t submeshIndex) const noexcept
{
	if (m_MeshPath.empty() || submeshIndex >= m_SubmeshRefs.size())
		return assetlib::c_InvalidIndex;
	return m_SubmeshRefs[submeshIndex].sourceSubmesh;
}

bool
MaterialPreviewWindow::SubmeshHasTangent(uint32_t submeshIndex) const noexcept
{
	return submeshIndex >= m_SubmeshRefs.size() || m_SubmeshRefs[submeshIndex].hasTangent;
}

void
MaterialPreviewWindow::ShowDefaultSphere()
{
	ClearGeometry();

	try
	{
		GetRenderer()->Invoke([&] {
			m_Geoms.push_back(GetPreviewScene()->AddSphereGeom(32, 32, 1.0f, m_DefaultMaterial));
			m_Instances.push_back(
				{ GetPreviewView()->CreateStaticMeshInstance(m_Geoms.back(), glm::mat4(1.0f)), 0 });

			// The sphere's triangles never exist on the CPU, so its raycast shadow is analytic.
			m_Raycaster.AddInstance(m_Raycaster.AddSphere(1.0f), glm::mat4(1.0f));
		});
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: could not show the default sphere: %s", e.what());

		ClearGeometry();
		Q_EMIT GeometryChanged();
		return;
	}

	m_SubmeshRefs.push_back({ 0, 0, 0, true });  // AddSphereGeom writes a tangent
	m_SubmeshNames = QStringList{ "Sphere" };    // procedural sphere: a single submesh

	m_SubmeshMaterialPaths = QStringList{ QString() };
	FocusOn(glm::vec3(0.0f), 1.0f);

	Q_EMIT GeometryChanged();
}

void
MaterialPreviewWindow::Reset()
{
	ShowDefaultSphere();
	RestoreConfiguredEnvironment();
}

void
MaterialPreviewWindow::LoadMesh(const std::filesystem::path& path)
{
	assetlib::BMesh mesh;
	const QString   name = QString::fromStdString(path.filename().string());

	auto build = editor::MeshPreviewBuild();

	// See the same line in AnimationPreviewWindow: the commit is the only half on the render thread.
	double prepareMs = 0.0;

	// The read, the meshlet cook and the picking copy: every part of standing a mesh up that does
	// not need the render thread. Leaving any of them in the commit below stops the frame loop for
	// every viewport in the editor, not just this one.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Loading %1").arg(name),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Reading mesh...");
			mesh = assetlib::load(path);
			if (mesh.meshes.empty())
				throw std::runtime_error("mesh contains no meshes");

			progress.Report(0, 0, "Building the mesh...");

			const auto prepareStart = std::chrono::steady_clock::now();
			build                   = editor::PrepareMeshPreview(mesh, m_DataRoot);
			prepareMs               = std::chrono::duration<double, std::milli>(
										  std::chrono::steady_clock::now() - prepareStart)
		                                  .count();
		});

	if (!result.Completed())
	{
		qWarning(
			"MaterialPreview: failed to load mesh '%s': %s",
			path.string().c_str(),
			qPrintable(result.error));

		QMessageBox::warning(
			window(),
			QStringLiteral("Load Mesh"),
			QStringLiteral("Could not load '%1':\n\n%2").arg(name, result.error));

		ShowDefaultSphere();
		return;
	}

	try
	{
		struct Focus
		{
			glm::vec3 center;
			float     radius;
		};

		const auto  commitStart = std::chrono::steady_clock::now();
		const Focus focus       = GetRenderer()->Invoke([&] {
			ClearGeometry();

			bgl::IScene* scene = GetPreviewScene();

			// The preview authors a material, so bind the same neutral material to every source
			// material slot; the graph then rebinds it per submesh.
			const auto materials = std::vector<bgl::MaterialHandle>(
				std::max<size_t>(1, mesh.materials.size()),
				m_DefaultMaterial);

			m_Geoms.reserve(build.meshes.size());
			for (editor::PreparedPreviewMesh& entry : build.meshes)
				m_Geoms.push_back(scene->AddStaticMeshGeom(std::move(entry.cooked), materials));

			auto aabbMin = glm::vec3(std::numeric_limits<float>::max());
			auto aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

			m_Instances.reserve(build.placements.size());
			for (const editor::PreviewPlacement& placement : build.placements)
			{
				m_Instances.push_back(
					{ GetPreviewView()->CreateStaticMeshInstance(
						  m_Geoms[placement.entry],
						  placement.world),
				      placement.entry });

				bmesh::GrowBoundsForMesh(
					mesh,
					build.meshes[placement.entry].meshIndex,
					placement.world,
					aabbMin,
					aabbMax);
			}

			const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
			const float     radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);
			return Focus{ center, radius };
		});

		qInfo(
			"MaterialPreview: loaded '%s' -- %.0f ms prepared on the worker, %.1f ms committed on "
			"the render thread",
			qPrintable(name),
			prepareMs,
			std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - commitStart)
				.count());

		// After the commit, because ClearGeometry inside it drops what the last load left here.
		m_Raycaster            = std::move(build.raycaster);
		m_SubmeshNames         = std::move(build.submeshNames);
		m_SubmeshMaterialPaths = std::move(build.submeshMaterialPaths);

		m_SubmeshRefs.clear();
		m_SubmeshRefs.reserve(build.submeshRefs.size());
		for (const editor::PreviewSubmeshRef& ref : build.submeshRefs)
			m_SubmeshRefs.push_back(
				{ ref.entry, ref.localSubmesh, ref.sourceSubmesh, ref.hasTangent });

		FocusOn(focus.center, focus.radius);

		m_MeshPath = path;

		Q_EMIT GeometryChanged();
	}
	catch (const std::exception& e)
	{
		qWarning("MaterialPreview: failed to load mesh '%s': %s", path.string().c_str(), e.what());

		QMessageBox::warning(
			window(),
			QStringLiteral("Load Mesh"),
			QStringLiteral("Could not show '%1':\n\n%2").arg(name, QString::fromUtf8(e.what())));

		ShowDefaultSphere();
	}
}

void
MaterialPreviewWindow::SetSubmeshMaterial(uint32_t submeshIndex, bgl::MaterialHandle material)
{
	// Fire-and-forget: a graph compiles on every keystroke, and the override needs no result back.
	GetRenderer()->Post([this, submeshIndex, material] {
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
				GetPreviewView()->SetSubmeshMaterialOverride(
					target.instance,
					target.submeshIndex,
					material);
			}
		}
		catch (const std::exception& e)
		{
			qWarning("MaterialPreview: SetSubmeshMaterial(%u) failed: %s", submeshIndex, e.what());
		}
	});
}

std::vector<MaterialPreviewWindow::SubmeshTarget>
MaterialPreviewWindow::GetInstanceTargets(
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
MaterialPreviewWindow::SetSelectedSubmesh(std::optional<uint32_t> submeshIndex)
{
	// Fire-and-forget like SetSubmeshMaterial: the selection needs no result back.
	GetRenderer()->Post([this, submeshIndex] {
		try
		{
			GetPreviewView()->ClearSelection();

			if (!submeshIndex.has_value())
				return;

			for (const SubmeshTarget& target :
			     GetInstanceTargets(m_SubmeshRefs, m_Instances, *submeshIndex))
			{
				GetPreviewView()->SetSubmeshSelected(target.instance, target.submeshIndex, true);
			}
		}
		catch (const std::exception& e)
		{
			qWarning(
				"MaterialPreview: SetSelectedSubmesh(%u) failed: %s",
				submeshIndex.value_or(0xFFFFFFFFu),
				e.what());
		}
	});
}

void
MaterialPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (!FirstMeshUrl(event->mimeData()).isEmpty() ||
	    !FirstEnvironmentUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (!FirstMeshUrl(event->mimeData()).isEmpty() ||
	    !FirstEnvironmentUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
MaterialPreviewWindow::dropEvent(QDropEvent* event)
{
	if (const QString environment = FirstEnvironmentUrl(event->mimeData()); !environment.isEmpty())
	{
		SetEnvironment(environment.toStdString());
		event->acceptProposedAction();
		return;
	}

	const QString file = FirstMeshUrl(event->mimeData());
	if (file.isEmpty())
		return;

	LoadMesh(std::filesystem::path(file.toStdWString()));
	event->acceptProposedAction();
}

void
MaterialPreviewWindow::SetEnvironment(const std::string& benvPath)
{
	// A dropped `.benv` belongs to the open project, so its own data root is the one that resolves
	// it. The configured root only stands in before a project is opened.
	const std::filesystem::path& dataRoot =
		m_DataRoot.empty() ? m_Environment.configured.dataRoot : m_DataRoot;

	GetRenderer()->Invoke([&] {
		editor::BindEnvironment(
			GetPreviewScene(),
			GetPreviewView(),
			m_Environment,
			benvPath,
			dataRoot,
			"MaterialPreview");
	});
}

void
MaterialPreviewWindow::RestoreConfiguredEnvironment()
{
	const std::optional<std::string> restore = editor::GetEnvironmentToRestore(m_Environment);
	if (!restore)
		return;

	GetRenderer()->Invoke([&] {
		editor::BindEnvironment(
			GetPreviewScene(),
			GetPreviewView(),
			m_Environment,
			*restore,
			m_Environment.configured.dataRoot,
			"MaterialPreview");
	});
}

void
MaterialPreviewWindow::resizeEvent(QResizeEvent* event)
{
	RenderTargetWindow::resizeEvent(event);
	UpdateCamera();
}

void
MaterialPreviewWindow::FocusOn(const glm::vec3& center, float radius)
{
	m_Orbit.FocusOn(center, radius);
	UpdateCamera();
}

void
MaterialPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_DragButton   = event->button();
	m_LastMousePos = event->position().toPoint();
	m_PressPos     = m_LastMousePos;
	m_Dragged      = false;
}

void
MaterialPreviewWindow::mouseReleaseEvent(QMouseEvent* event)
{
	// A press the camera never followed is a click: pick what it landed on.
	if (event->button() == Qt::LeftButton && m_DragButton == Qt::LeftButton && !m_Dragged)
		PickAt(event->position());

	m_DragButton = Qt::NoButton;
}

void
MaterialPreviewWindow::mouseMoveEvent(QMouseEvent* event)
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
MaterialPreviewWindow::wheelEvent(QWheelEvent* event)
{
	m_Orbit.Dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
	UpdateCamera();
	event->accept();
}

void
MaterialPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;

	m_Camera = m_Orbit.GetCamera(aspect);
	SetCamera(m_Camera);
}

void
MaterialPreviewWindow::PickAt(const QPointF& pixel)
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
MaterialPreviewWindow::SelectorIndexOf(
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
