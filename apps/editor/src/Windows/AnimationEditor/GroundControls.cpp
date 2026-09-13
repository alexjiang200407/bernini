#include "GroundControls.h"

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/AnimationEditor/foot_ik_weights.h"

#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <qstringliteral.h>

GroundControls::GroundControls(AnimationPreviewWindow* preview, QWidget* parent) :
	QGroupBox(QStringLiteral("Plant feet"), parent), m_Preview(preview)
{
	// Off to begin with, so a panel just opened shows the clip as its author left it: the ground is
	// a thing to try, and a preview that silently moved a foot on the way in would be answering a
	// question nobody had asked yet.
	setCheckable(true);
	setChecked(false);

	// Flat, because the frame is the platform's and the column is not: every other control beside it
	// is a bare label over a Scrubber, so a border would be the only one in the panel -- and collapsed
	// it draws as an empty rounded sliver under the title, which reads as a stray rule.
	setFlat(true);
	setToolTip(QStringLiteral(
		"Stands the rig on a ground plane and solves each leg onto it. Off, there is no floor and "
		"the clip plays exactly as authored, which is the other half of judging the solve."));

	auto* box = new QVBoxLayout(this);
	box->setContentsMargins(0, 0, 0, 0);
	m_Body = new QWidget(this);
	box->addWidget(m_Body);

	auto* ground = new QVBoxLayout(m_Body);
	ground->setContentsMargins(0, 0, 0, 0);

	// Connected once the body it hides exists, so no future setChecked above can fire into a
	// half-built group.
	connect(this, &QGroupBox::toggled, this, [this] { Apply(); });

	m_SlopeLabel = new QLabel(QStringLiteral("Ground Slope: 0°"), m_Body);
	ground->addWidget(m_SlopeLabel);

	m_SlopeSlider = new Scrubber(m_Body);
	m_SlopeSlider->SetRange(-30, 30);
	m_SlopeSlider->SetValue(0);
	m_SlopeSlider->setToolTip(QStringLiteral(
		"Tilts the ground the rig stands on. A rig with an avatar plants its feet against it; one "
		"without stands through it. Rises toward +X."));

	// The label follows the thumb so the number is readable mid-drag; the ground follows the
	// release. A click or a keypress moves the thumb without a drag, and commits through the same
	// release path.
	connect(m_SlopeSlider, &Scrubber::ValueChanged, this, [this](int degrees) {
		m_SlopeLabel->setText(QStringLiteral("Ground Slope: %1°").arg(degrees));
	});
	connect(m_SlopeSlider, &Scrubber::Committed, this, [this](int degrees) {
		m_Preview->SetGroundSlope(static_cast<float>(degrees));
	});
	ground->addWidget(m_SlopeSlider);

	// Which way uphill points. Nothing in the path knows which way a rig moves -- the test coyote
	// runs along +Z -- so a person turns the hill to face the stride. Committed like the slope.
	m_HeadingLabel = new QLabel(QStringLiteral("Uphill Heading: 0°"), m_Body);
	ground->addWidget(m_HeadingLabel);

	m_HeadingSlider = new Scrubber(m_Body);
	m_HeadingSlider->SetRange(0, 359);
	m_HeadingSlider->SetValue(0);
	m_HeadingSlider->setToolTip(QStringLiteral(
		"Which way the ground rises, in degrees about the up axis from +X. Turn it to face the way "
		"the rig moves to see it climb the slope rather than cross it."));
	connect(m_HeadingSlider, &Scrubber::ValueChanged, this, [this](int degrees) {
		m_HeadingLabel->setText(QStringLiteral("Uphill Heading: %1°").arg(degrees));
	});
	connect(m_HeadingSlider, &Scrubber::Committed, this, [this](int degrees) {
		m_Preview->SetGroundHeading(static_cast<float>(degrees));
	});
	ground->addWidget(m_HeadingSlider);

	// One write carries both weights, so either slider's release commits the pair.
	const auto commitFootIK = [this] {
		m_Preview->SetFootIK(
			editor::FootIKForSliders(m_IKWeightSlider->GetValue(), m_SoleTurnSlider->GetValue()));
	};

	m_IKWeightLabel = new QLabel(QStringLiteral("IK Weight: 100%"), m_Body);
	ground->addWidget(m_IKWeightLabel);

	m_IKWeightSlider = new Scrubber(m_Body);
	m_IKWeightSlider->SetRange(0, 100);
	m_IKWeightSlider->SetValue(100);
	m_IKWeightSlider->setToolTip(QStringLiteral(
		"How far each foot is carried onto the ground, over what the clip baked. 0 leaves the "
		"ankle where the animation put it; 100 seats it on the plane."));
	connect(m_IKWeightSlider, &Scrubber::ValueChanged, this, [this](int percent) {
		m_IKWeightLabel->setText(QStringLiteral("IK Weight: %1%").arg(percent));
	});
	connect(m_IKWeightSlider, &Scrubber::Committed, this, commitFootIK);
	ground->addWidget(m_IKWeightSlider);

	m_SoleTurnLabel = new QLabel(QStringLiteral("Sole Turn: 100%"), m_Body);
	ground->addWidget(m_SoleTurnLabel);

	m_SoleTurnSlider = new Scrubber(m_Body);
	m_SoleTurnSlider->SetRange(0, 100);
	m_SoleTurnSlider->SetValue(100);
	m_SoleTurnSlider->setToolTip(QStringLiteral(
		"How far each sole turns onto the slope under it. 0 keeps the foot's authored tilt with "
		"its contact still on the ground; 100 lays it on the slope."));
	connect(m_SoleTurnSlider, &Scrubber::ValueChanged, this, [this](int percent) {
		m_SoleTurnLabel->setText(QStringLiteral("Sole Turn: %1%").arg(percent));
	});
	connect(m_SoleTurnSlider, &Scrubber::Committed, this, commitFootIK);
	ground->addWidget(m_SoleTurnSlider);
}

void
GroundControls::Apply()
{
	const bool planting = isChecked();

	m_Body->setVisible(planting);

	m_Preview->SetFloorVisible(planting);
	m_Preview->SetFootPlanting(planting);
}
