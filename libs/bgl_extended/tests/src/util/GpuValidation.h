#pragma once

namespace bgl::test
{
	/**
	 * Whether D3D12's GPU-based validation should be turned on -- that is, whether `--gpu-validation`
	 * was passed on the command line. It is **off by default**, and deliberately so.
	 *
	 * The layer patches every shader, and the whole cost lands on device creation, which every test does
	 * at least once: ~3s becomes ~18s. Over the suite that is 280s against 570s. Leaving it always on
	 * made a full run long enough that it stops being run, which costs more coverage than the layer
	 * buys. So it is opt-in, for a final verification pass:
	 *
	 *     just run bgl_extended_tests -- --gpu-validation
	 *
	 * Note this is *GPU-based* validation, not the D3D12 debug layer: the debug layer stays on either
	 * way, and it is what catches the ordinary API misuse. This only adds the shader-level checks.
	 */
	[[nodiscard]] bool
	GpuValidationEnabled() noexcept;

	/** Set once by main() from the parsed command line, before any test runs. */
	void
	SetGpuValidation(bool enabled) noexcept;

	/**
	 * Whether GPU validation is actually running, by whatever route turns it on -- the flag above on
	 * D3D12, or Metal's own environment variables, which the runtime reads before this process gets a
	 * say.
	 *
	 * For a test whose subject is incompatible with validation rather than merely slower under it.
	 * The shader cache's driver-pipeline layer is the case: it is not built while validation runs, so
	 * a test that asserts one was written has nothing to assert.
	 */
	[[nodiscard]] bool
	GpuValidationActive() noexcept;

	/**
	 * Skips the calling case while GPU validation is active: for a case that only re-runs, with other
	 * parameters, passes a kept case in its file already puts through the validator. Validation hunts
	 * API misuse -- a barrier, a descriptor, a teardown order -- which a pass shows in its first
	 * frames; it is not what measures a converged image, and paying its cost per sweep point is most
	 * of a validated run. Keep every case that owns a distinct pass, configuration or resource
	 * lifetime.
	 */
	void
	SkipUnderGpuValidation();
}
