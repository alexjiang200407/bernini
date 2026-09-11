#include "AnimationPreviewWindow.h"

#include "Mesh/mesh_load.h"
#include "Windows/AnimationEditor/playback_writes.h"

#include "Async/BackgroundTask.h"
#include "Mesh/BMeshUtil.h"
#include "Render/Renderer.h"
#include "Render/environment.h"
#include "Windows/AnimationEditor/animation_bindings.h"
#include "Windows/AnimationEditor/animation_draws.h"
#include "Windows/AnimationEditor/blend_sets.h"
#include "Windows/AnimationEditor/ground_slope.h"
#include "Windows/MaterialEditor/material_io.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"
#include "util/mesh_drop.h"
#include "util/mime_files.h"
#include <assetlib_structs/Mesh.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/InstanceDesc.h>
#include <bgl/MeshInstanceHandle.h>
#include <gamelib/BlendSpaceInfo.h>
#include <gamelib/ClipInfo.h>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>
#include <assetlib/AssetStore.h>
#include <assetlib/asset_refs.h>
#include <assetlib/skinning.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <bgl/types/FootIKDesc.h>
#include <core/glm.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <gamelib/anim_blend.h>
#include <iterator>
#include <limits>
#include <optional>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstringliteral.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
	QString
	FirstEnvironmentUrl(const QMimeData* mime)
	{
		return editor::FirstLocalFileWithSuffix(mime, u".benv");
	}
}

AnimationPreviewWindow::AnimationPreviewWindow(
	QWidget*                     parent,
	RenderTargetWindowDesc       rt,
	editor::EnvironmentApplyDesc env) : RenderTargetWindow(parent, std::move(rt))
{
	setAcceptDrops(true);

	// Wheel events only reach a widget that can take focus, and the camera needs them to dolly.
	setFocusPolicy(Qt::StrongFocus);

	m_Environment.configured = std::move(env);

	GetRenderer()->Invoke([&] {
		bgl::IScene* scene = GetPreviewScene();

		editor::BindEnvironment(
			scene,
			GetPreviewView(),
			m_Environment,
			m_Environment.configured.environmentMap,
			m_Environment.configured.dataRoot,
			"AnimationPreview");

		// Matte and mid-grey, so a sole meeting it reads against it rather than into it. Wide
		// enough that a clip with root motion does not walk off the edge.
		m_GroundMaterial = scene->CreatePbrMaterial(
			{ .baseColorFactor = glm::vec4(0.45f, 0.45f, 0.45f, 1.0f),
		      .metallicFactor  = 0.0f,
		      .roughnessFactor = 0.9f });
		m_GroundGeom = scene->AddPlaneGeom(1, 1, 40.0f, 40.0f, m_GroundMaterial);
	});

	UpdateCamera();
}

AnimationPreviewWindow::~AnimationPreviewWindow()
{
	ClearGeometry();

	GetRenderer()->Invoke([&] {
		try
		{
			if (m_GroundGeom.IsValid())
				GetPreviewScene()->DeleteGeom(m_GroundGeom);
			if (m_GroundMaterial.IsValid())
				GetPreviewScene()->DeleteMaterial(m_GroundMaterial);
		}
		catch (const std::exception& e)
		{
			qWarning("AnimationPreview: failed to delete the floor: %s", e.what());
		}
	});
}

void
AnimationPreviewWindow::SetGroundSlope(const float degrees)
{
	m_SlopeDegrees = degrees;
	ReplaceGround();
}

void
AnimationPreviewWindow::SetGroundHeading(const float degrees)
{
	m_HeadingDegrees = degrees;
	ReplaceGround();
}

void
AnimationPreviewWindow::SetFloorVisible(const bool visible)
{
	m_FloorVisible = visible;
	ReplaceGround();
}

void
AnimationPreviewWindow::SetFootPlanting(const bool enabled)
{
	m_FootPlanting = enabled;
	ReplaceGround();
}

void
AnimationPreviewWindow::SetFootIK(const bgl::FootIKDesc& desc)
{
	m_FootIK = desc;

	if (m_AnimatedDraws.empty())
		return;

	GetRenderer()->Invoke([&] {
		for (const AnimatedDraw& draw : m_AnimatedDraws) ApplyFootIK(draw.instance);
	});

	// A constant holds at prevTime as well as at time, so the frame after this write reprojects
	// through a pose nothing drew. The ground is re-placed for the reason the slope commits through
	// it: SetGround moves the temporal epoch, which is the one break in history the preview can
	// ask for.
	ReplaceGround();
}

void
AnimationPreviewWindow::ApplyFootIK(const bgl::MeshInstanceHandle instance)
{
	bgl::ISceneView* view = GetPreviewView();
	if (view->HasFootIK(instance))
		view->SetFootIK(instance, m_FootIK);
}

void
AnimationPreviewWindow::ReplaceGround()
{
	if (!m_GroundPlaced || !isVisible())
		return;

	GetRenderer()->Invoke([&] {
		try
		{
			PlaceGround();
		}
		catch (const std::exception& e)
		{
			qWarning("AnimationPreview: failed to place the ground: %s", e.what());
		}
	});
}

void
AnimationPreviewWindow::showEvent(QShowEvent* event)
{
	RenderTargetWindow::showEvent(event);

	// The ground is the scene's, and the scene is shared with the Level Editor: a slope set here
	// is a slope every skinned instance anywhere in it plants against. So it stands only while
	// this panel is on screen, and goes flat the moment it is not.
	ReplaceGround();
}

void
AnimationPreviewWindow::hideEvent(QHideEvent* event)
{
	if (m_GroundPlaced)
		GetRenderer()->Invoke([&] {
			GetPreviewScene()->SetGround({});
			GetPreviewScene()->SetFootPlanting(true);
		});

	RenderTargetWindow::hideEvent(event);
}

void
AnimationPreviewWindow::PlaceGround()
{
	bgl::ISceneView* view = GetPreviewView();

	for (bgl::MeshInstanceHandle& instance : m_GroundInstances)
	{
		if (instance.IsValid())
			view->DeleteMeshInstance(instance);
		instance = bgl::MeshInstanceHandle();
	}

	GetPreviewScene()->SetGround(editor::GroundForSlope(m_SlopeDegrees, m_HeadingDegrees));
	GetPreviewScene()->SetFootPlanting(m_FootPlanting);
	m_GroundPlaced = true;

	if (m_FloorVisible)
	{
		const glm::mat4 floor = editor::FloorTransformForSlope(m_SlopeDegrees, m_HeadingDegrees);

		// The second is the same floor turned to face the other way, so dropping the camera to the
		// floor -- where a contact is actually readable -- does not make it disappear.
		const glm::mat4 flip =
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));

		m_GroundInstances[0] = view->CreateStaticMeshInstance(m_GroundGeom, floor);
		m_GroundInstances[1] = view->CreateStaticMeshInstance(m_GroundGeom, floor * flip);
	}
}

void
AnimationPreviewWindow::SetAssets(game::AssetManager* assets)
{
	if (assets == m_Assets)
		return;

	// Through the manager that acquired it, before the pointer moves off it.
	ClearGeometry();
	m_Assets = assets;
}

void
AnimationPreviewWindow::Clear()
{
	ClearGeometry();
	RestoreConfiguredEnvironment();
	SetTime(0.0f);

	Q_EMIT MeshChanged(QString());
	Q_EMIT AnimationSourcesChanged(QStringList(), -1);
	Q_EMIT ClipsChanged({});
}

void
AnimationPreviewWindow::ClearGeometry()
{
	if (m_GroundPlaced)
	{
		// The floor leaves with the rig, and the ground it tilted goes back to flat: the scene is
		// shared, and nothing else in it should stand on a slope this panel set.
		GetRenderer()->Invoke([&] {
			for (bgl::MeshInstanceHandle& instance : m_GroundInstances)
			{
				if (instance.IsValid())
					GetPreviewView()->DeleteMeshInstance(instance);
				instance = bgl::MeshInstanceHandle();
			}
			m_GroundPlaced = false;
			GetPreviewScene()->SetGround({});
			GetPreviewScene()->SetFootPlanting(true);
		});
	}

	if (m_Assets != nullptr &&
	    (!m_Instances.empty() || !m_Geoms.empty() || !m_AnimatedDraws.empty()))
	{
		GetRenderer()->Invoke([&] {
			for (const AnimatedDraw& draw : m_AnimatedDraws)
			{
				try
				{
					m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				}
				catch (const std::exception& e)
				{
					qWarning("AnimationPreview: failed to destroy an instance: %s", e.what());
				}
			}

			for (const bgl::MeshInstanceHandle& instance : m_Instances)
			{
				try
				{
					m_Assets->DestroyInstance(GetPreviewViewRef(), instance);
				}
				catch (const std::exception& e)
				{
					qWarning("AnimationPreview: failed to destroy an instance: %s", e.what());
				}
			}

			for (const bgl::GeomHandle& geom : m_Geoms)
			{
				try
				{
					m_Assets->ReleaseGeom(geom);
				}
				catch (const std::exception& e)
				{
					qWarning("AnimationPreview: failed to release a geom: %s", e.what());
				}
			}
		});
	}

	m_AnimatedDraws.clear();
	m_Instances.clear();
	m_Geoms.clear();
	m_Clips.clear();
	m_Spaces.clear();
}

void
AnimationPreviewWindow::LoadMesh(
	const std::filesystem::path& absolutePath,
	const std::string&           animationsRelPath,
	const std::string&           blendRelPath)
{
	const QString name = QString::fromStdString(absolutePath.filename().string());

	if (m_Assets == nullptr || m_DataRoot.empty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("Open a project before loading a mesh."));
		return;
	}

	const auto relPath = absolutePath.lexically_relative(m_DataRoot).lexically_normal();
	const auto rel     = relPath.generic_string();
	if (rel.empty() || rel.starts_with(".."))
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("'%1' is outside the project's Data directory.").arg(name));
		return;
	}

	assetlib::BMesh           mesh;
	editor::AnimationBindings bindings;
	std::string               animations = animationsRelPath;
	std::vector<std::string>  blendSets;
	std::string               blend = blendRelPath;

	// The box every pose of every clip falls in: what the camera frames, and what the skinned geom
	// culls by. A bind-pose box is not it -- a clip carrying root motion walks the rig clean out of
	// it, so the camera frames the wrong place and the mesh culls away as soon as it travels.
	//
	// The box a placement poses in, read off the .banim's own bake, one per animated mesh entry --
	// it is that geom's culling volume, and a .bmesh may hold two separately rigged meshes. Only a
	// pairing the cook never measured is walked here, off the UI and render threads.
	auto skinnedBounds = std::unordered_map<uint32_t, assetlib::Bounds>();

	// Planned inside the task below so the measurement knows which mesh entries animate; the empty
	// case is judged out here, where it reads as a refusal rather than a failed load.
	auto plan = editor::AnimationDrawPlan();

	// The mesh read and the candidate scan, off the UI and render threads.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Loading %1").arg(name),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Reading mesh...");
			mesh = editor::LoadMeshThroughSeam(m_DataRoot, absolutePath);
			if (mesh.meshes.empty())
				throw std::runtime_error("mesh contains no meshes");

			progress.Report(0, 0, "Resolving animations...");

			// One scan answers both questions. It reads and parses every asset in the project, so
			// asking each of them for its own would double the cost of every load.
			const auto graph = assetlib::AssetRefGraph::Scan(assetlib::AssetStore(m_DataRoot));

			bindings = editor::ResolveAnimationBindings(graph, mesh.skeleton);
			if (animations.empty() && !bindings.animations.empty())
				animations = bindings.animations.front();

			// One edge over: the sets are per clip set, so which ones exist is only knowable once
			// the `.banim` is settled.
			blendSets = editor::ResolveBlendSets(graph, animations);

			plan = editor::PlanAnimationDraws(mesh);

			if (!animations.empty())
			{
				progress.Report(0, 0, "Reading the pose bounds...");

				// Through a store, like every other read: a project opens as a mount, so a rig that
				// ships inside a .bpak is only reachable that way.
				const auto store = assetlib::AssetStore(m_DataRoot);

				const assetlib::AnimationSet clips    = store.LoadRegenAnimations(animations);
				const assetlib::Skeleton     skeleton = store.LoadRegenSkeleton(clips.skeleton);

				const std::vector<std::optional<assetlib::Bounds>> baked =
					assetlib::findPosedBounds(clips, mesh, skeleton);

				for (const bmesh::InstancePlacement& placement : plan.animated)
				{
					if (skinnedBounds.contains(placement.meshIndex))
						continue;

					const std::optional<assetlib::Bounds>& box = baked[placement.meshIndex];
					if (!box)
						progress.Report(0, 0, "Measuring the pose...");

					skinnedBounds.emplace(
						placement.meshIndex,
						box ? *box :
							  assetlib::posedBounds(mesh, placement.meshIndex, skeleton, clips));
				}
			}
		});

	if (!result.Completed())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("Could not load '%1':\n\n%2").arg(name, result.error));
		return;
	}

	// This panel animates; a static mesh has nothing to animate, and silently previewing one
	// reads as the panel being broken. The Material Editor's preview is the place to look at it.
	if (mesh.skeleton.empty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("'%1' has no rig -- nothing to animate.").arg(name));
		return;
	}

	try
	{
		struct Loaded
		{
			glm::vec3                         center;
			float                             radius;
			std::vector<game::ClipInfo>       clips;
			std::vector<game::BlendSpaceInfo> spaces;

			// Empty when the tier stood up. A refusal is shown rather than thrown: the mesh is
			// still on screen in its bind pose, which beats a viewport cleared to nothing.
			QString refusal;

			// Which mesh entries took that fallback. Without them the dialog can say a clip cannot
			// play but not which part of the mesh stopped moving, which is the whole diagnosis when
			// one submesh of twenty-seven is the one refused.
			std::vector<uint32_t> refusedEntries;
		};

		if (plan.animated.empty() && plan.statics.empty())
			throw std::runtime_error("no node references a mesh");

		// Behind a loading screen, because acquiring is where the seconds are: a character's worth of
		// materials is a hundred-odd megabytes of texture to read and upload, and Invoke blocks its
		// caller until the render thread has run the closure. A worker may block on the render thread
		// -- only the reverse deadlocks (see Renderer) -- so the GUI thread stays free to paint.
		auto loaded = Loaded();

		const background::TaskResult upload = background::RunWithLoadingScreen(
			this,
			QString("Loading %1").arg(name),
			[&](background::Progress& progress) {
				progress.Report(0, 0, "Uploading materials and geometry...");
				loaded = GetRenderer()->Invoke([&] {
					ClearGeometry();

					auto out     = Loaded();
					auto aabbMin = glm::vec3(std::numeric_limits<float>::max());
					auto aabbMax = glm::vec3(std::numeric_limits<float>::lowest());

					const auto acquireStatic = [&](const bmesh::InstancePlacement& placement) {
						const bgl::GeomHandle geom =
							m_Assets->AcquireMesh(rel, placement.meshIndex);
						m_Geoms.push_back(geom);
						m_Instances.push_back(
							m_Assets->CreateInstance(GetPreviewViewRef(), geom, placement.world));
						bmesh::GrowBoundsForMesh(
							mesh,
							placement.meshIndex,
							placement.world,
							aabbMin,
							aabbMax);
					};

					for (const bmesh::InstancePlacement& placement : plan.statics)
						acquireStatic(placement);

					for (const bmesh::InstancePlacement& placement : plan.animated)
					{
						// A rig with no clip file anywhere falls back to bind pose as static geometry --
						// and so does one the acquire refuses (an unbaked or loose material): a mesh
						// standing still beats a viewport cleared to nothing, and the refusal is
						// surfaced once the load completes.
						if (animations.empty())
						{
							acquireStatic(placement);
							continue;
						}

						// The box this placement poses in, measured above. It is the geom's culling
						// volume as well as the camera's frame, so it is resolved per mesh entry
						// rather than shared across the file.
						const auto it = skinnedBounds.find(placement.meshIndex);
						const std::optional<assetlib::Bounds> posed =
							it != skinnedBounds.end() ? std::optional(it->second) : std::nullopt;

						try
						{
							// One acquire for both sources: they share the upload and differ only in
							// where the instance reads its pose. The box is handed over rather than
							// measured again -- this runs on the render thread, and posedBounds is
							// seconds on a dense rig. Absent only if the measurement was skipped, and
							// then the acquire makes it: a stall beats culling the mesh by a box of
							// nothing.
							game::AssetManager::SkinnedMesh skinned = m_Assets->AcquireSkinnedMesh(
								rel,
								animations,
								blend,
								placement.meshIndex,
								posed);

							const bgl::GeomHandle geom = skinned.geom;
							m_Geoms.push_back(geom);
							m_AnimatedDraws.push_back(
								{ geom, placement.world, SpawnAnimated(geom, placement.world, 0) });
							out.clips  = std::move(skinned.clips);
							out.spaces = std::move(skinned.spaces);
							m_Playback = bgl::SkinnedPlaybackDesc::FromClip(0);
						}
						catch (const std::exception& e)
						{
							out.refusal = QString::fromUtf8(e.what());
							out.refusedEntries.push_back(placement.meshIndex);
							acquireStatic(placement);  // grows the bind-pose bounds itself
							continue;
						}

						if (posed)
							bmesh::GrowBounds(
								placement.world,
								posed->min,
								posed->max,
								aabbMin,
								aabbMax);
						else
							bmesh::GrowBoundsForMesh(
								mesh,
								placement.meshIndex,
								placement.world,
								aabbMin,
								aabbMax);
					}

					// Under the rig, at whatever slope the panel last set: the floor is part of what
					// a rig is previewed against, and a planted foot only means something with
					// ground to meet.
					PlaceGround();

					out.center = (aabbMin + aabbMax) * 0.5f;
					out.radius = std::max(0.001f, glm::length(aabbMax - aabbMin) * 0.5f);
					return out;
				});
			},
			background::Cancellable::kNo);

		if (!upload.Completed())
		{
			QMessageBox::warning(
				window(),
				QStringLiteral("Open Mesh"),
				QStringLiteral("Could not load '%1':\n\n%2").arg(name, upload.error));
			return;
		}

		// Straight on, slightly above -- and arbitrary, because nothing here knows which way a rig
		// faces. Authoring conventions disagree on the forward axis, so any fixed yaw shows some rigs
		// a profile; the coyote is one of them, and orbiting once is the answer until bones can be
		// tagged (see docs/skinning.md).
		m_Orbit.FocusOn(loaded.center, loaded.radius, 0.0f, glm::radians(15.0f));
		UpdateCamera();
		SetTime(0.0f);

		auto candidates = QStringList();
		for (const std::string& candidate : bindings.animations)
			candidates << QString::fromStdString(candidate);
		const auto active = static_cast<int>(std::distance(
			bindings.animations.begin(),
			std::find(bindings.animations.begin(), bindings.animations.end(), animations)));

		auto setNames = QStringList();
		for (const std::string& set : blendSets) setNames << QString::fromStdString(set);
		const auto activeSet =
			static_cast<int>(std::distance(blendSets.begin(), std::ranges::find(blendSets, blend)));

		m_Clips  = loaded.clips;
		m_Spaces = loaded.spaces;

		Q_EMIT MeshChanged(QString::fromStdString(rel));
		Q_EMIT AnimationSourcesChanged(candidates, active < candidates.size() ? active : -1);
		Q_EMIT ClipsChanged(editor::ToClipInfos(loaded.clips));
		Q_EMIT BlendSetsChanged(
			setNames,
			blend.empty() || activeSet >= setNames.size() ? -1 : activeSet);
		Q_EMIT SpacesChanged(loaded.spaces);

		if (!loaded.refusal.isEmpty())
			OfferBakeForRefusal(
				mesh,
				absolutePath,
				animations,
				name,
				loaded.refusal,
				loaded.refusedEntries);
	}
	catch (const std::exception& e)
	{
		qWarning(
			"AnimationPreview: failed to show mesh '%s': %s",
			absolutePath.string().c_str(),
			e.what());

		QMessageBox::warning(
			window(),
			QStringLiteral("Open Mesh"),
			QStringLiteral("Could not show '%1':\n\n%2").arg(name, QString::fromUtf8(e.what())));

		Clear();
	}
}

namespace
{
	/**
	 * Which parts of the mesh took the bind-pose fallback, named by the materials they draw with.
	 *
	 * A refusal is per mesh entry, and a file may have many: on a character rig one submesh of
	 * twenty-seven standing still while the rest animates reads as a rig bug until you know which
	 * one it is, and its material is what a reader can act on.
	 */
	QString
	RefusedEntriesLine(const assetlib::BMesh& mesh, std::span<const uint32_t> entries)
	{
		if (entries.empty())
			return {};

		auto named = QStringList();
		for (const uint32_t entry : entries)
		{
			if (entry >= mesh.meshes.size())
				continue;

			const assetlib::Mesh& record = mesh.meshes[entry];
			for (uint32_t i = 0; i < record.submeshCount; ++i)
			{
				const uint32_t material = mesh.submeshes[record.firstSubmesh + i].material;
				if (material >= mesh.materials.size())
					continue;

				const QString stem = QString::fromStdString(
					std::filesystem::path(mesh.materials[material]).stem().string());
				if (!stem.isEmpty() && !named.contains(stem))
					named << stem;
			}
		}

		if (named.isEmpty())
			return {};

		return QStringLiteral(
				   "\n\nStanding still in bind pose: %1 (%2). The rest of the mesh "
				   "animates normally.")
		    .arg(named.join(QStringLiteral(", ")))
		    .arg(
				entries.size() == 1 ? QStringLiteral("1 part") :
									  QStringLiteral("%1 parts").arg(entries.size()));
	}
}

void
AnimationPreviewWindow::OfferBakeForRefusal(
	const assetlib::BMesh&          mesh,
	const std::filesystem::path&    absolutePath,
	const std::string&              animations,
	const QString&                  name,
	const QString&                  refusal,
	const std::span<const uint32_t> refusedEntries)
{
	// Every material of the file, not the refusing submesh's alone. A refusal about the rig itself
	// (no skin binding, a clip set cooked against another skeleton) can still offer a bake for some
	// unrelated unbaked material: that bake is worth doing and the text says only what is true, but
	// it will not lift this refusal.
	const std::vector<std::string> loose =
		editor::BakeableMaterials(assetlib::AssetStore(m_DataRoot), mesh.materials);

	auto box = QMessageBox(window());
	box.setIcon(QMessageBox::Information);
	box.setWindowTitle(QStringLiteral("Open Mesh"));
	box.setText(QStringLiteral("'%1' is shown in bind pose -- its clips cannot play:\n\n%2%3")
	                .arg(name, refusal, RefusedEntriesLine(mesh, refusedEntries)));

	QPushButton* bakeButton = nullptr;
	if (!loose.empty())
	{
		box.setInformativeText(
			QStringLiteral(
				"%1 of its materials %2 drawing unbaked (never baked, or the bake is stale "
				"-- a git pull makes every bake stale here), and this preview draws baked "
				"materials only.")
				.arg(loose.size())
				.arg(loose.size() == 1 ? "is" : "are"));
		bakeButton = box.addButton(QStringLiteral("Bake Now"), QMessageBox::AcceptRole);
	}
	box.addButton(QMessageBox::Ok);
	box.exec();

	if (bakeButton == nullptr || box.clickedButton() != bakeButton)
		return;

	auto files = QStringList();
	files.reserve(static_cast<qsizetype>(loose.size()));
	for (const std::string& relPath : loose) files << QString::fromStdString(relPath);

	// One loading screen over all of them; compositing is file-only, so it runs off the UI
	// thread like the Content Explorer's bake. Baking reads each material off disk, so the
	// routes composited are the ones last saved.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QStringLiteral("Baking materials"),
		[&](background::Progress& progress) { editor::BakeMaterials(m_DataRoot, files, progress); },
		background::Cancellable::kYes);

	if (result.Cancelled())
		return;

	if (result.Failed())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake Material"),
			QStringLiteral("Could not bake:\n\n%1").arg(result.error));
		return;
	}

	// The Material Editor reads its panel off the file; MainWindow routes this the same way it
	// routes the Content Explorer's bakes.
	for (const std::string& relPath : loose) Q_EMIT MaterialBaked(QString::fromStdString(relPath));

	LoadMesh(absolutePath, animations);
}

bgl::MeshInstanceHandle
AnimationPreviewWindow::SpawnAnimated(
	const bgl::GeomHandle geom,
	const glm::mat4&      world,
	const uint32_t        clip)
{
	// Phase 0 and rate 1: the panel's transport is the clock. `source` is the whole of what the two
	// tiers differ by at spawn -- one geom, one upload, two places to read a pose from.
	const bgl::MeshInstanceHandle instance = m_Assets->CreateSkinnedInstance(
		GetPreviewViewRef(),
		geom,
		world,
		bgl::SkinnedInstanceDesc{ clip, 0.0f, 1.0f, m_Source });
	ApplyFootIK(instance);
	return instance;
}

void
AnimationPreviewWindow::StampTransition(
	const uint32_t fromNode,
	const uint32_t toNode,
	const float    startSeconds,
	const float    duration)
{
	if (m_Assets == nullptr || m_AnimatedDraws.empty() || !editor::RewritesPlayback(m_Source))
		return;

	m_Playback = game::CrossfadeTo(
		bgl::SkinnedPlaybackDesc::FromClip(fromNode),
		toNode,
		startSeconds,
		duration);

	GetRenderer()->Invoke([&] {
		for (const AnimatedDraw& draw : m_AnimatedDraws)
		{
			try
			{
				GetPreviewViewRef()->SetSkinnedPlayback(draw.instance, m_Playback);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to stamp a transition: %s", e.what());
			}
		}
	});
}

void
AnimationPreviewWindow::SetPoseSource(const bgl::PoseSource source, const float nowSeconds)
{
	if (source == m_Source)
		return;

	m_Source = source;

	// A re-spawn, not a re-load: both sources draw the same upload, so what changes is only where
	// each instance reads its pose. SetSkinnedPlayback rewrites a record's slots, never its kind,
	// so the kind is the one thing a rewrite cannot do. With nothing shown there is nothing to
	// respawn, and the tier is simply what the next load spawns on.
	//
	// A spawn carries one clip, so the record cannot cross: it is reset onto the node it was mostly
	// showing, which makes the switch a hard cut. That is what it already was -- a respawn moves
	// the scene's temporal epoch either way.
	const uint32_t node = editor::DominantNode(m_Playback, nowSeconds);
	m_Playback          = bgl::SkinnedPlaybackDesc::FromClip(node);

	if (m_Assets == nullptr || m_AnimatedDraws.empty())
	{
		Q_EMIT PoseSourceChanged(m_Source);
		return;
	}

	GetRenderer()->Invoke([&] {
		for (AnimatedDraw& draw : m_AnimatedDraws)
		{
			try
			{
				m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				draw.instance = bgl::MeshInstanceHandle();
				draw.instance = SpawnAnimated(draw.geom, draw.world, node);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to switch an instance's tier: %s", e.what());
			}
		}
	});

	Q_EMIT PoseSourceChanged(m_Source);
}

QString
AnimationPreviewWindow::RetargetBlendParameters(const std::vector<game::BlendSpaceInfo>& spaces)
{
	if (m_Assets == nullptr || m_AnimatedDraws.empty())
		return QStringLiteral("Nothing is loaded to retarget.");

	auto refusal = QString();

	// One geom, not all of them: every animated entry here came from one file against one clip set,
	// so they are on one rig, and the manager writes the rig and sweeps every geom sharing it.
	GetRenderer()->Invoke([&] {
		try
		{
			m_Assets->SetBlendParameters(m_AnimatedDraws.front().geom, spaces);
		}
		catch (const std::exception& e)
		{
			refusal = QString::fromUtf8(e.what());
		}
	});

	// Only once the scene took it: the cursor's own math reads these for cycle lengths, and a table
	// ahead of the rig would retarget from a path the pose pass never walked.
	if (refusal.isEmpty())
		m_Spaces = spaces;

	return refusal;
}

void
AnimationPreviewWindow::ShowSpace(
	const uint32_t spaceIndex,
	const float    parameter,
	const float    nowSeconds)
{
	if (m_Assets == nullptr || m_AnimatedDraws.empty() || spaceIndex >= m_Spaces.size() ||
	    !editor::RewritesPlayback(m_Source))
	{
		return;
	}

	// Clips first and then the authored spaces, which is what makes a space's node index its
	// position after them -- and why adding a set never moves a clip's.
	const uint32_t node = static_cast<uint32_t>(m_Clips.size()) + spaceIndex;

	m_Playback                = bgl::SkinnedPlaybackDesc::FromClip(node);
	m_Playback.slot[0].param0 = parameter;
	m_Playback.slot[0].param1 = parameter;
	m_Playback.slot[0].tRef   = nowSeconds;

	GetRenderer()->Invoke([&] {
		for (AnimatedDraw& draw : m_AnimatedDraws)
		{
			try
			{
				m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				draw.instance = bgl::MeshInstanceHandle();
				draw.instance = SpawnAnimated(draw.geom, draw.world, node);
				GetPreviewViewRef()->SetSkinnedPlayback(draw.instance, m_Playback);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to show a blend space: %s", e.what());
			}
		}
	});
}

void
AnimationPreviewWindow::RetargetSpace(
	const uint32_t spaceIndex,
	const float    parameter,
	const float    nowSeconds,
	const float    duration)
{
	if (m_Assets == nullptr || m_AnimatedDraws.empty() || spaceIndex >= m_Spaces.size() ||
	    !editor::RewritesPlayback(m_Source))
	{
		return;
	}

	const uint32_t node = static_cast<uint32_t>(m_Clips.size()) + spaceIndex;

	try
	{
		m_Playback = game::RetargetParameter(
			m_Playback,
			node,
			m_Spaces[spaceIndex],
			m_Clips,
			parameter,
			nowSeconds,
			duration);
	}
	catch (const std::exception& e)
	{
		qWarning("AnimationPreview: failed to retarget a blend space: %s", e.what());
		return;
	}

	GetRenderer()->Invoke([&] {
		for (const AnimatedDraw& draw : m_AnimatedDraws)
		{
			try
			{
				GetPreviewViewRef()->SetSkinnedPlayback(draw.instance, m_Playback);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to write a retargeted space: %s", e.what());
			}
		}
	});
}

void
AnimationPreviewWindow::SetActiveClip(const uint32_t index, const float nowSeconds)
{
	if (m_Assets == nullptr || m_AnimatedDraws.empty() ||
	    index == editor::DominantNode(m_Playback, nowSeconds))
	{
		return;
	}

	// Destroy and recreate on both sources, and deliberately not a rewrite. A clip switch rewinds
	// the transport as well, so the pose and the clock both jump: there is no previous frame the
	// new record could stay honest about, and the temporal epoch a respawn moves is what correctly
	// drops the history rather than reprojecting through it. A rewrite is for a fade with a
	// duration, where the record does carry its own past -- which is the strip's job, not this one's.
	m_Playback = bgl::SkinnedPlaybackDesc::FromClip(index);

	GetRenderer()->Invoke([&] {
		for (AnimatedDraw& draw : m_AnimatedDraws)
		{
			try
			{
				m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				draw.instance = bgl::MeshInstanceHandle();
				draw.instance = SpawnAnimated(draw.geom, draw.world, index);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to switch an instance's clip: %s", e.what());
			}
		}
	});
}

void
AnimationPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (editor::IsMeshDrag(event->mimeData()) || !FirstEnvironmentUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
AnimationPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (editor::IsMeshDrag(event->mimeData()) || !FirstEnvironmentUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
AnimationPreviewWindow::dropEvent(QDropEvent* event)
{
	if (const QString environment = FirstEnvironmentUrl(event->mimeData()); !environment.isEmpty())
	{
		SetEnvironment(environment.toStdString());
		event->acceptProposedAction();
		return;
	}

	const editor::MeshDrop drop =
		editor::GetMeshDroppedOn(event->mimeData(), QString::fromStdWString(m_DataRoot.wstring()));
	if (drop.mesh.isEmpty())
	{
		editor::ReportUnresolved(window(), drop);
		return;
	}

	LoadMesh(std::filesystem::path(drop.mesh.toStdWString()));
	event->acceptProposedAction();
}

void
AnimationPreviewWindow::SetEnvironment(const std::string& benvPath)
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
			"AnimationPreview");
	});
}

void
AnimationPreviewWindow::RestoreConfiguredEnvironment()
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
			"AnimationPreview");
	});
}

void
AnimationPreviewWindow::resizeEvent(QResizeEvent* event)
{
	RenderTargetWindow::resizeEvent(event);
	UpdateCamera();
}

void
AnimationPreviewWindow::mousePressEvent(QMouseEvent* event)
{
	m_DragButton   = event->button();
	m_LastMousePos = event->position().toPoint();
}

void
AnimationPreviewWindow::mouseReleaseEvent(QMouseEvent*)
{
	m_DragButton = Qt::NoButton;
}

void
AnimationPreviewWindow::mouseMoveEvent(QMouseEvent* event)
{
	const QPoint pos   = event->position().toPoint();
	const QPoint delta = pos - m_LastMousePos;
	m_LastMousePos     = pos;

	if (m_DragButton == Qt::LeftButton)
		m_Orbit.Orbit(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	else if (m_DragButton == Qt::MiddleButton || m_DragButton == Qt::RightButton)
		m_Orbit.Pan(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
	else
		return;

	UpdateCamera();
}

void
AnimationPreviewWindow::wheelEvent(QWheelEvent* event)
{
	m_Orbit.Dolly(static_cast<float>(event->angleDelta().y()) / 120.0f);
	UpdateCamera();
	event->accept();
}

void
AnimationPreviewWindow::UpdateCamera()
{
	const float aspect =
		height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
	SetCamera(m_Orbit.GetCamera(aspect));
}

QStringList
AnimationPreviewWindow::GetHeldOpenPaths() const
{
	return editor::GetHeldOpenEnvironment(m_Environment);
}
