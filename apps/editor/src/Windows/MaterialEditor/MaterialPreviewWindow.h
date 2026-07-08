#pragma once

#include "Windows/RenderTarget/RenderTargetWindow.h"

#include <bgl/GeomHandle.h>
#include <bgl/MaterialHandle.h>

struct MaterialPreviewEnv
{
	std::string skybox;
	std::string irradiance;
	std::string prefilter;
	std::string brdfLut;
};

class MaterialPreviewWindow : public RenderTargetWindow
{
	Q_OBJECT

public:
	MaterialPreviewWindow(QWidget* parent, RenderTargetWindowDesc rt, MaterialPreviewEnv env);

	void
	SetPreviewMaterial(bgl::MaterialHandle material);

protected:
	void
	resizeEvent(QResizeEvent* event) override;

private:
	void
	UpdateCamera();

	bgl::GeomHandle m_Geom;
	uint32_t        m_SubmeshCount = 1;
};
