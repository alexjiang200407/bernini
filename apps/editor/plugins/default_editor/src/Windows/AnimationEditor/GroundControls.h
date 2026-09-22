#pragma once

#include <QGroupBox>
#include <qtmetamacros.h>

class AnimationPreviewWindow;
class QCheckBox;
class QLabel;
class QWidget;
class Scrubber;

/**
 * The *Plant feet* group: one switch that stands a preview's rig on a ground plane and solves each
 * leg onto it, titling the two shadow toggles and the four sliders that shape the result -- slope,
 * uphill heading, IK weight and sole turn.
 *
 * One switch rather than a floor and a solve separately, because neither half is worth anything
 * alone -- an empty floor shows nothing, and there is nothing to plant against without one. Off, the
 * group collapses to its title and the sliders keep their values, so turning it back on restores
 * what was set.
 *
 * Every slider commits on release, not per tick: the ground is a rebind that moves the temporal
 * epoch, and a drag committing every tick would keep the preview unaccumulated for the gesture.
 */
class GroundControls : public QGroupBox
{
	Q_OBJECT

public:
	/** `preview` must outlive the group; it is what every control writes to. */
	GroundControls(AnimationPreviewWindow* preview, QWidget* parent);

	/**
	 * Pushes the switch into the preview and shows or hides the sliders it governs.
	 *
	 * Not run by the constructor: a host sizes its column from the group's hint first, and the hint
	 * of a collapsed group leaves out the widest slider label. The group holds the state and this is
	 * the one place that pushes it, so the host's first call is what puts the preview on it.
	 */
	void
	Apply();

private:
	AnimationPreviewWindow* m_Preview = nullptr;

	// The four sliders sit in one body so the group collapses as a unit; hiding them one by one
	// would leave the box's own height behind and whatever sits under it would not move up.
	QWidget* m_Body = nullptr;

	// The contact disc under the rig and the shadow under each foot, drawn only while the group is
	// on: without the floor there is nothing for either to land on.
	QCheckBox* m_BlobShadowCheck = nullptr;
	QCheckBox* m_FootShadowCheck = nullptr;

	// The ground's tilt and which way uphill points, in whole degrees.
	Scrubber* m_SlopeSlider   = nullptr;
	QLabel*   m_SlopeLabel    = nullptr;
	Scrubber* m_HeadingSlider = nullptr;
	QLabel*   m_HeadingLabel  = nullptr;

	// The instance's own IK weights, in percent, committed together as one write.
	Scrubber* m_IKWeightSlider = nullptr;
	QLabel*   m_IKWeightLabel  = nullptr;
	Scrubber* m_SoleTurnSlider = nullptr;
	QLabel*   m_SoleTurnLabel  = nullptr;
};
