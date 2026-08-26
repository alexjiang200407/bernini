#pragma once

#include "Render/OrbitCamera.h"
#include "Render/environment.h"
#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <assetlib_structs/BEnv.h>
#include <bgl/GeomHandle.h>
#include <bgl/MaterialHandle.h>
#include <bgl/MeshInstanceHandle.h>

class QDragEnterEvent;
class QDropEvent;
class QMouseEvent;
class QWheelEvent;

/**
 * The environment editor's subject: a sphere, or a `.bmesh` dropped onto it, standing in the
 * environment being authored and catching all of its rim light.
 *
 * A full share here whatever the mesh's own `.bimport` asked for. This viewport exists to judge the
 * environment, and an environment whose rim could not be seen because the sphere caught none of it
 * would be judged as having none.
 */
class EnvironmentPreviewWindow : public RenderTargetWindow
{
	Q_OBJECT

public:
	EnvironmentPreviewWindow(QWidget* parent, RenderTargetWindowDesc rt);

	/** Frees the geometry and the environment maps this viewport holds. */
	void
	Release();

	/**
	 * Binds the `.benv` at `benvPath` -- its maps, its sky and its rim -- releasing what it
	 * displaced. `dataRoot` is what the paths inside it resolve against.
	 */
	void
	Bind(const std::string& benvPath, const std::filesystem::path& dataRoot);

	/**
	 * Shows an unsaved edit of the document that is bound, without touching a texture: the rim, the
	 * exposure, and how the backdrop presents the sky it already holds.
	 *
	 * A pixel-level change -- a different `.bsky`, a different `.benvl` -- is not this: rebind for
	 * that.
	 */
	void
	ApplyEdits(const assetlib::BEnv& env);

	/** The exposure the bound environment's lighting derived, which an unset override falls to. */
	[[nodiscard]] float
	GetDerivedExposure() const noexcept
	{
		return m_DerivedExposure;
	}

Q_SIGNALS:
	/** A `.benv` was dropped onto the viewport; the window opens it. */
	void
	EnvironmentDropped(const QString& path);

protected:
	void
	dragEnterEvent(QDragEnterEvent* event) override;
	void
	dropEvent(QDropEvent* event) override;
	void
	mousePressEvent(QMouseEvent* event) override;
	void
	mouseMoveEvent(QMouseEvent* event) override;
	void
	wheelEvent(QWheelEvent* event) override;
	void
	resizeEvent(QResizeEvent* event) override;

private:
	void
	ShowSphere();

	void
	ShowMesh(const std::filesystem::path& path);

	void
	ClearGeometry();

	/** Places `geom` catching the whole rim, and records it so Release can take it back down. */
	void
	Place(bgl::GeomHandle geom, const glm::mat4& transform);

	void
	PushCamera();

	editor::EnvironmentBinding m_Environment;
	std::filesystem::path      m_DataRoot;
	float                      m_DerivedExposure = 1.0f;

	bgl::MaterialHandle                  m_Material;
	std::vector<bgl::GeomHandle>         m_Geoms;
	std::vector<bgl::MeshInstanceHandle> m_Instances;

	editor::OrbitCamera m_Camera;
	QPoint              m_LastMouse;
};
