#include "GroundControls.h"

#include "Windows/AnimationEditor/AnimationPreviewWindow.h"
#include "Windows/AnimationEditor/Scrubber.h"
#include "Windows/AnimationEditor/foot_ik_weights.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>
#include <editor_plugin_api/localize.h>

GroundControls::GroundControls(
	AnimationPreviewWindow*          preview,
	const editor::ILanguageResolver& resolver,
	QWidget*                         parent) :
	QGroupBox(editor::Localize(resolver, "bernini.animation_ground.title", "Plant feet"), parent),
	m_Preview(preview), m_Resolver(resolver)
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
	setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.tooltip",
			"Stands the rig on a ground plane and solves each leg onto it. Off, there is no floor "
			"and "
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

	// On by default: the disc is the cheap read of whether a foot is grounded, which is most of
	// why the floor is on at all. In the body, so no floor means no orphaned disc to offer.
	m_BlobShadowCheck = new QCheckBox(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.blob_shadow_checkbox",
			"Blob shadow"),
		m_Body);
	m_BlobShadowCheck->setChecked(true);
	m_BlobShadowCheck->setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.blob_shadow_tooltip",
			"Draws a soft contact disc on the ground under the rig, shrinking and fading as it "
			"rises. Off when judging the pose's own silhouette."));
	connect(m_BlobShadowCheck, &QCheckBox::toggled, this, [this](bool on) {
		m_Preview->SetBlobShadow(isChecked() && on);
	});
	ground->addWidget(m_BlobShadowCheck);

	// Off by default: the disc already answers whether the rig is grounded, and this is the finer
	// question of which foot is.
	m_FootShadowCheck = new QCheckBox(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.foot_shadows_checkbox",
			"Foot shadows"),
		m_Body);
	m_FootShadowCheck->setChecked(false);
	m_FootShadowCheck->setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.foot_shadows_tooltip",
			"Draws a shadow under each foot of a rig with an avatar, dark where the foot stands "
			"and "
			"fading as it lifts. A crowd-tier preview has no pose of its own, so it keeps the disc "
			"alone."));
	connect(m_FootShadowCheck, &QCheckBox::toggled, this, [this](bool on) {
		m_Preview->SetFootShadows(isChecked() && on);
	});
	ground->addWidget(m_FootShadowCheck);

	m_SlopeLabel = new QLabel(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.slope_label",
			{ 0 },
			"Ground Slope: {0}°"),
		m_Body);
	ground->addWidget(m_SlopeLabel);

	m_SlopeSlider = new Scrubber(m_Body);
	m_SlopeSlider->SetRange(-30, 30);
	m_SlopeSlider->SetValue(0);
	m_SlopeSlider->setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.slope_tooltip",
			"Tilts the ground the rig stands on. A rig with an avatar plants its feet against it; "
			"one "
			"without stands through it. Rises toward +X."));

	// The label follows the thumb so the number is readable mid-drag; the ground follows the
	// release. A click or a keypress moves the thumb without a drag, and commits through the same
	// release path.
	connect(m_SlopeSlider, &Scrubber::ValueChanged, this, [this](int degrees) {
		m_SlopeLabel->setText(
			editor::Localize(
				m_Resolver,
				"bernini.animation_ground.slope_label",
				{ degrees },
				"Ground Slope: {0}°"));
	});
	connect(m_SlopeSlider, &Scrubber::Committed, this, [this](int degrees) {
		m_Preview->SetGroundSlope(static_cast<float>(degrees));
	});
	ground->addWidget(m_SlopeSlider);

	// Which way uphill points. Nothing in the path knows which way a rig moves -- the test coyote
	// runs along +Z -- so a person turns the hill to face the stride. Committed like the slope.
	m_HeadingLabel = new QLabel(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.heading_label",
			{ 0 },
			"Uphill Heading: {0}°"),
		m_Body);
	ground->addWidget(m_HeadingLabel);

	m_HeadingSlider = new Scrubber(m_Body);
	m_HeadingSlider->SetRange(0, 359);
	m_HeadingSlider->SetValue(0);
	m_HeadingSlider->setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.heading_tooltip",
			"Which way the ground rises, in degrees about the up axis from +X. Turn it to face the "
			"way "
			"the rig moves to see it climb the slope rather than cross it."));
	connect(m_HeadingSlider, &Scrubber::ValueChanged, this, [this](int degrees) {
		m_HeadingLabel->setText(
			editor::Localize(
				m_Resolver,
				"bernini.animation_ground.heading_label",
				{ degrees },
				"Uphill Heading: {0}°"));
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

	m_IKWeightLabel = new QLabel(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.ik_weight_label",
			{ 100 },
			"IK Weight: {0}%"),
		m_Body);
	ground->addWidget(m_IKWeightLabel);

	m_IKWeightSlider = new Scrubber(m_Body);
	m_IKWeightSlider->SetRange(0, 100);
	m_IKWeightSlider->SetValue(100);
	m_IKWeightSlider->setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.ik_weight_tooltip",
			"How far each foot is carried onto the ground, over what the clip baked. 0 leaves the "
			"ankle where the animation put it; 100 seats it on the plane."));
	connect(m_IKWeightSlider, &Scrubber::ValueChanged, this, [this](int percent) {
		m_IKWeightLabel->setText(
			editor::Localize(
				m_Resolver,
				"bernini.animation_ground.ik_weight_label",
				{ percent },
				"IK Weight: {0}%"));
	});
	connect(m_IKWeightSlider, &Scrubber::Committed, this, commitFootIK);
	ground->addWidget(m_IKWeightSlider);

	m_SoleTurnLabel = new QLabel(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.sole_turn_label",
			{ 100 },
			"Sole Turn: {0}%"),
		m_Body);
	ground->addWidget(m_SoleTurnLabel);

	m_SoleTurnSlider = new Scrubber(m_Body);
	m_SoleTurnSlider->SetRange(0, 100);
	m_SoleTurnSlider->SetValue(100);
	m_SoleTurnSlider->setToolTip(
		editor::Localize(
			m_Resolver,
			"bernini.animation_ground.sole_turn_tooltip",
			"How far each sole turns onto the slope under it. 0 keeps the foot's authored tilt "
			"with "
			"its contact still on the ground; 100 lays it on the slope."));
	connect(m_SoleTurnSlider, &Scrubber::ValueChanged, this, [this](int percent) {
		m_SoleTurnLabel->setText(
			editor::Localize(
				m_Resolver,
				"bernini.animation_ground.sole_turn_label",
				{ percent },
				"Sole Turn: {0}%"));
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
	m_Preview->SetBlobShadow(planting && m_BlobShadowCheck->isChecked());
	m_Preview->SetFootShadows(planting && m_FootShadowCheck->isChecked());
}
