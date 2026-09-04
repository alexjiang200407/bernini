import Ramp;

// One leg's runtime IK weights on one hero instance, 0 to 1 each: how far the solve carries the
// ankle onto the ground, and how far the sole turns onto the ground's slope. Each multiplies the
// weight the cook baked for the frame, so a foot the animator lifted stays lifted whatever a caller
// asks for.
public struct FootIKLeg
{
    public Ramp position;
    public Ramp rotation;
};
