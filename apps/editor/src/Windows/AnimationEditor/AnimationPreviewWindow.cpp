#include "AnimationPreviewWindow.h"
#include "Mesh/mesh_load.h"

#include "Async/BackgroundTask.h"
#include "Mesh/BMeshUtil.h"
#include "Render/Renderer.h"
#include "Windows/AnimationEditor/animation_bindings.h"
#include "Windows/AnimationEditor/animation_draws.h"
#include "Windows/MaterialEditor/material_io.h"
#include "util/mime_files.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QWheelEvent>

#include <assetlib/AssetStore.h>
#include <assetlib/skinning.h>
#include <assetlib/vat_bake.h>
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/Bounds.h>
#include <gamelib/AssetManager.h>
#include <gamelib/vat_freshness.h>

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
		editor::BindEnvironment(
			GetPreviewScene(),
			GetPreviewView(),
			m_Environment,
			m_Environment.configured.environmentMap,
			m_Environment.configured.dataRoot,
			"AnimationPreview");
	});

	UpdateCamera();
}

AnimationPreviewWindow::~AnimationPreviewWindow() { ClearGeometry(); }

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

	// What "a mesh is shown" is read off -- SetAnimationSource takes an empty path as nothing to
	// swap. Left set, a load that failed and cleared would make the next tier switch re-attempt the
	// mesh that just failed instead of simply remembering the preference.
	m_MeshPath.clear();
	m_Animations.clear();

	Q_EMIT MeshChanged(QString());
	Q_EMIT AnimationSourcesChanged(QStringList(), -1);
	Q_EMIT ClipsChanged({});
}

void
AnimationPreviewWindow::ClearGeometry()
{
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
}

void
AnimationPreviewWindow::LoadMesh(
	const std::filesystem::path& absolutePath,
	const std::string&           animationsRelPath)
{
	LoadMeshAs(absolutePath, animationsRelPath, m_Source);
}

void
AnimationPreviewWindow::LoadMeshAs(
	const std::filesystem::path&  absolutePath,
	const std::string&            animationsRelPath,
	const editor::AnimationSource source)
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

	// The box every pose of every clip falls in: what the camera frames, and what the skinned geom
	// culls by. A bind-pose box is not it -- a clip carrying root motion walks the rig clean out of
	// it, so the camera frames the wrong place and the mesh culls away as soon as it travels.
	//
	// VAT reads the box its bake already closed over, one for the file. The skinned tier reads
	// its own bake off the .banim, one per animated mesh entry -- the box is that geom's culling
	// volume, and a .bmesh may hold two separately rigged meshes. Only a pairing the cook never
	// measured is walked here, off the UI and render threads.
	auto posedMin   = glm::vec3(0.0f);
	auto posedMax   = glm::vec3(0.0f);
	bool posedKnown = false;

	auto skinnedBounds = std::unordered_map<uint32_t, assetlib::Bounds>();

	// Planned inside the task below so the measurement knows which mesh entries animate; the empty
	// case is judged out here, where it reads as a refusal rather than a failed load.
	auto plan = editor::AnimationDrawPlan();

	// One plan for the whole load, so the three tier-dependent decisions cannot drift apart. Filled
	// inside the task below, where `animations` is final -- it may still be resolved from the
	// bindings scan.
	auto steps = editor::AnimationLoadSteps();

	// Only meaningful on the VAT tier; kFresh so the skinned tier never reads it as a refusal.
	auto bakeState = game::VatBakeState::kFresh;

	// The mesh read, the candidate scan and -- the expensive part -- a stale rig's re-bake, all
	// off the UI and render threads. AcquireVatMesh afterwards finds the .bvat fresh and only
	// uploads.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QString("Loading %1").arg(name),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Reading mesh...");
			mesh = editor::LoadMeshThroughSeam(m_DataRoot, absolutePath);
			if (mesh.meshes.empty())
				throw std::runtime_error("mesh contains no meshes");

			progress.Report(0, 0, "Resolving animations...");
			bindings = editor::ResolveAnimationBindings(m_DataRoot, mesh.skeleton);
			if (animations.empty() && !bindings.animations.empty())
				animations = bindings.animations.front();

			steps = editor::PlanAnimationLoad(source, !animations.empty());
			plan  = editor::PlanAnimationDraws(mesh);

			if (steps.needsFreshBake)
			{
				progress.Report(0, 0, "Checking the bake...");

				// Asked, not enforced: a bake is seconds of CPU skinning, so a load that made one
				// behind the user would be the panel deciding to spend their time. The answer is
				// carried out to the offer below. Reading it here also *is* the load -- VatFreshness
				// hands back what it parsed, so the fresh path costs one read and no bake.
				auto vat = assetlib::BVat();
				bakeState =
					game::VatFreshness(assetlib::AssetStore(m_DataRoot), rel, animations, &vat);

				if (bakeState == game::VatBakeState::kFresh)
				{
					posedMin   = vat.boundsMin;
					posedMax   = vat.boundsMax;
					posedKnown = true;
				}
			}

			// The VAT tier already has this box from its bake; the skinned tier reads its own.
			if (!steps.framedByBake && !animations.empty())
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

	// Nothing to draw from, so the load stops here rather than showing a bind pose that reads as
	// the tier working badly. Baking is offered, and taking it re-enters this function -- which
	// finds the bake fresh and goes on. Declining leaves whatever was already on screen, m_Source
	// included, so a refused switch to VAT does not silently become a switch.
	if (bakeState != game::VatBakeState::kFresh)
	{
		OfferBakeForTier(absolutePath, animations, name, bakeState);
		return;
	}

	m_MeshPath   = absolutePath;
	m_Animations = animations;

	try
	{
		struct Loaded
		{
			glm::vec3                   center;
			float                       radius;
			std::vector<game::ClipInfo> clips;

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
						// and so does one the VAT pipeline refuses (an unbaked or loose material): a
						// mesh standing still beats a viewport cleared to nothing, and the refusal is
						// surfaced once the load completes.
						if (animations.empty())
						{
							acquireStatic(placement);
							continue;
						}

						// The box this placement poses in: measured above for the skinned tier, read off
						// the bake for VAT. It is the geom's culling volume as well as the camera's frame,
						// so it is resolved per mesh entry rather than shared across the file.
						const std::optional<assetlib::Bounds> posed = [&] {
							if (source != editor::AnimationSource::kSkinned)
								return posedKnown ?
							               std::optional(assetlib::Bounds{ posedMin, posedMax }) :
							               std::nullopt;

							const auto it = skinnedBounds.find(placement.meshIndex);
							return it != skinnedBounds.end() ? std::optional(it->second) :
						                                       std::nullopt;
						}();

						try
						{
							// The two tiers differ only in which door the acquire takes: both hand back a
							// geom and the same clip table, and both spawn on {clip 0, phase 0, rate 1}.
							bgl::GeomHandle             geom;
							std::vector<game::ClipInfo> clips;

							if (source == editor::AnimationSource::kSkinned)
							{
								// Handed over rather than measured again: this runs on the render thread,
								// and posedBounds is seconds on a dense rig. Absent only if the measurement
								// was skipped, and then the acquire makes it -- a stall beats culling the
								// mesh by a box of nothing.
								const game::AssetManager::SkinnedMesh skinned =
									m_Assets->AcquireSkinnedMesh(
										rel,
										animations,
										placement.meshIndex,
										posed);
								geom  = skinned.geom;
								clips = std::move(skinned.clips);
							}
							else
							{
								game::AssetManager::VatMesh vat =
									m_Assets->AcquireVatMesh(rel, animations, placement.meshIndex);
								geom  = vat.geom;
								clips = std::move(vat.clips);
							}

							m_Geoms.push_back(geom);
							m_AnimatedDraws.push_back(
								{ geom,
						          placement.world,
						          SpawnAnimated(geom, placement.world, 0, source) });
							out.clips    = std::move(clips);
							m_ActiveClip = 0;
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

		Q_EMIT MeshChanged(QString::fromStdString(rel));
		Q_EMIT AnimationSourcesChanged(candidates, active < candidates.size() ? active : -1);
		Q_EMIT ClipsChanged(editor::ToClipInfos(loaded.clips));

		// The load stood something up, so the tier it was loaded through is now what is on screen.
		m_Source = source;

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
	QString
	BakeStateReason(const game::VatBakeState state)
	{
		switch (state)
		{
		case game::VatBakeState::kStale:
			return QStringLiteral(
				"its bake is out of date -- the mesh, the rig or the clips have moved since "
				"(a git pull makes every bake stale here)");
		case game::VatBakeState::kOtherClips:
			return QStringLiteral("its bake was made from a different clip set");
		case game::VatBakeState::kMissing:
		case game::VatBakeState::kFresh:
			break;
		}
		return QStringLiteral("it has not been baked yet");
	}

	/** What a bake would write, or why it will not run -- `size` is meaningless when it will not. */
	struct VatCost
	{
		assetlib::VatSize size{};
		QString           refusal;
	};

	/**
	 * The bake's size without the bake. Loading the three inputs is all it costs, and the panel has
	 * just loaded them to draw what is on screen, so the read is warm.
	 */
	VatCost
	MeasureVatBake(
		const std::filesystem::path& dataRoot,
		const std::filesystem::path& absolutePath,
		const std::string&           animations)
	{
		try
		{
			const auto rel =
				absolutePath.lexically_relative(dataRoot).lexically_normal().generic_string();

			return { assetlib::AssetStore(dataRoot).VatBakeSize(
						 assetlib::VatBakeDesc{ rel, animations }),
				     {} };
		}
		catch (const std::exception& e)
		{
			return { {}, QString::fromUtf8(e.what()) };
		}
	}

	/** "about 412.5 MB (18,204 vertex columns x 1,841 frame rows, 3 clips)" */
	QString
	VatCostLine(const assetlib::VatSize& size)
	{
		return QStringLiteral("about %1 (%2 vertex columns x %3 frame rows, %4)")
		    .arg(QLocale().formattedDataSize(static_cast<qint64>(size.bytes)))
		    .arg(QLocale().toString(size.width))
		    .arg(QLocale().toString(size.height))
		    .arg(
				size.clipCount == 1 ? QStringLiteral("1 clip") :
									  QStringLiteral("%1 clips").arg(size.clipCount));
	}
}

bool
AnimationPreviewWindow::BakeVat(
	const std::filesystem::path& absolutePath,
	const std::string&           animations)
{
	const QString name = QString::fromStdString(absolutePath.filename().string());

	const auto rel =
		absolutePath.lexically_relative(m_DataRoot).lexically_normal().generic_string();

	// Off the UI thread: a bake is CPU skinning of every vertex at every frame, seconds on a dense
	// rig. EnsureVatBaked is pure assetlib, which is what lets it run here at all.
	const background::TaskResult result = background::RunWithLoadingScreen(
		this,
		QStringLiteral("Baking %1").arg(name),
		[&](background::Progress& progress) {
			progress.Report(0, 0, "Baking animation textures...");
			(void)game::EnsureVatBaked(assetlib::AssetStore(m_DataRoot), rel, animations);
		});

	if (result.Cancelled())
		return false;

	if (result.Failed())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake VAT"),
			QStringLiteral("Could not bake '%1':\n\n%2").arg(name, result.error));
		return false;
	}

	return true;
}

void
AnimationPreviewWindow::BakeShownVat()
{
	if (!CanBakeVat())
		return;

	const QString name = QString::fromStdString(m_MeshPath.filename().string());
	const VatCost cost = MeasureVatBake(m_DataRoot, m_MeshPath, m_Animations);

	if (!cost.refusal.isEmpty())
	{
		QMessageBox::warning(
			window(),
			QStringLiteral("Bake VAT"),
			QStringLiteral("Could not bake '%1':\n\n%2").arg(name, cost.refusal));
		return;
	}

	auto ask = QMessageBox(window());
	ask.setIcon(QMessageBox::Question);
	ask.setWindowTitle(QStringLiteral("Bake VAT"));
	ask.setText(QStringLiteral("Bake VAT for '%1'?").arg(name));
	ask.setInformativeText(
		QStringLiteral(
			"This writes %1 beside the mesh, and takes a few seconds. The bake is a build "
			"product: deleting it later costs only the time to make it again.")
			.arg(VatCostLine(cost.size)));

	QPushButton* bakeButton = ask.addButton(QStringLiteral("Bake"), QMessageBox::AcceptRole);
	ask.addButton(QMessageBox::Cancel);
	ask.setDefaultButton(QMessageBox::Cancel);
	ask.exec();

	if (ask.clickedButton() != bakeButton)
		return;

	if (!BakeVat(m_MeshPath, m_Animations))
		return;

	// Only the VAT tier draws from what just changed. Reloading the skinned tier would re-upload a
	// rig for a file it never samples.
	if (m_Source == editor::AnimationSource::kVat)
		LoadMeshAs(m_MeshPath, m_Animations, editor::AnimationSource::kVat);
}

void
AnimationPreviewWindow::OfferBakeForTier(
	const std::filesystem::path& absolutePath,
	const std::string&           animations,
	const QString&               name,
	const game::VatBakeState     state)
{
	const VatCost cost = MeasureVatBake(m_DataRoot, absolutePath, animations);

	auto box = QMessageBox(window());
	box.setIcon(QMessageBox::Information);
	box.setWindowTitle(QStringLiteral("Preview as VAT"));
	box.setText(
		QStringLiteral("'%1' cannot be previewed as VAT: %2.").arg(name, BakeStateReason(state)));

	// Nothing to offer: the bake this dialog's button would start is one already refused, and
	// hearing that now beats hearing it after agreeing to pay for it.
	if (!cost.refusal.isEmpty())
	{
		box.setInformativeText(
			QStringLiteral("It cannot be baked either:\n\n%1").arg(cost.refusal));
		box.exec();
		return;
	}

	box.setInformativeText(
		QStringLiteral(
			"Baking skins every vertex of every frame into a texture pair: %1, written "
			"beside the mesh, a few seconds' work. The Skinned tier needs no bake.")
			.arg(VatCostLine(cost.size)));

	QPushButton* bakeButton = box.addButton(QStringLiteral("Bake Now"), QMessageBox::AcceptRole);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(QMessageBox::Cancel);
	box.exec();

	if (box.clickedButton() != bakeButton)
		return;

	if (BakeVat(absolutePath, animations))
		LoadMeshAs(absolutePath, animations, editor::AnimationSource::kVat);
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
	const bgl::GeomHandle         geom,
	const glm::mat4&              world,
	const uint32_t                clip,
	const editor::AnimationSource source)
{
	// The two descs carry the same three fields deliberately, so this is the whole of what the tiers
	// differ by at spawn time. Phase 0 and rate 1: the panel's transport is the clock.
	if (source == editor::AnimationSource::kSkinned)
	{
		return m_Assets->CreateSkinnedInstance(
			GetPreviewViewRef(),
			geom,
			world,
			bgl::SkinnedInstanceDesc{ clip, 0.0f, 1.0f });
	}

	return m_Assets->CreateVatInstance(
		GetPreviewViewRef(),
		geom,
		world,
		bgl::VatInstanceDesc{ clip, 0.0f, 1.0f });
}

void
AnimationPreviewWindow::SetAnimationSource(const editor::AnimationSource source)
{
	if (source == m_Source)
		return;

	// With nothing shown there is no upload to swap, so the tier is just remembered for the next
	// load.
	if (m_MeshPath.empty())
	{
		m_Source = source;
		Q_EMIT PreviewSourceChanged(m_Source);
		return;
	}

	// A re-load, not a re-spawn: the tiers are different uploads, so there is no instance to move
	// between them. LoadMeshAs commits m_Source only once it has stood something up, so a load that
	// fails leaves the panel -- and the signal below -- describing what is still on screen.
	LoadMeshAs(m_MeshPath, m_Animations, source);

	Q_EMIT PreviewSourceChanged(m_Source);
}

void
AnimationPreviewWindow::SetActiveClip(const uint32_t index)
{
	if (m_Assets == nullptr || m_AnimatedDraws.empty() || index == m_ActiveClip)
		return;

	// There is no mutate-instance API by design: a clip switch is destroy + recreate, and the
	// caller rewinds its transport so the new clip starts from its first frame.
	GetRenderer()->Invoke([&] {
		for (AnimatedDraw& draw : m_AnimatedDraws)
		{
			try
			{
				m_Assets->DestroyInstance(GetPreviewViewRef(), draw.instance);
				draw.instance = bgl::MeshInstanceHandle();
				draw.instance = SpawnAnimated(draw.geom, draw.world, index, m_Source);
			}
			catch (const std::exception& e)
			{
				qWarning("AnimationPreview: failed to switch an instance's clip: %s", e.what());
			}
		}
	});

	m_ActiveClip = index;
}

void
AnimationPreviewWindow::dragEnterEvent(QDragEnterEvent* event)
{
	if (!FirstMeshUrl(event->mimeData()).isEmpty() ||
	    !FirstEnvironmentUrl(event->mimeData()).isEmpty())
		event->acceptProposedAction();
}

void
AnimationPreviewWindow::dragMoveEvent(QDragMoveEvent* event)
{
	// The accept decision doesn't depend on position, so mirror dragEnterEvent.
	if (!FirstMeshUrl(event->mimeData()).isEmpty() ||
	    !FirstEnvironmentUrl(event->mimeData()).isEmpty())
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

	const QString file = FirstMeshUrl(event->mimeData());
	if (file.isEmpty())
		return;

	LoadMesh(std::filesystem::path(file.toStdWString()));
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
