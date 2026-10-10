#pragma once
#include <cstdint>
#include <utility>

namespace terrain
{
	/**
	 * How a generated field is eroded: particle hydraulic erosion, droplets that run downhill
	 * carrying what they wear off and laying it down where they slow, then thermal erosion, which
	 * slides what is left steeper than the talus angle down to its neighbours. The two alternate
	 * over `passes`; each pass also remembers where water ran, so later droplets follow the
	 * channels earlier ones cut and carry more down them (SimpleHydrology's discharge and
	 * momentum maps). Every length is in world units, so one desc erodes any shape at any cell size
	 * alike. The default erodes nothing.
	 */
	struct ErosionDesc
	{
		// The deepest cut, in metres, joining a closed hollow to the field's edge; 0 cuts none.
		// Runs alongside droplets or thermal erosion, never alone (docs/terrain.md, Erosion).
		float breachDepth = 20.0f;

		// Droplets released over the whole run, per sample of the field; 0 runs no hydraulic erosion.
		float dropletsPerSample = 0.0f;

		// Rounds of hydraulic then thermal erosion the droplets are spread over.
		uint32_t passes = 4;

		// Cells a droplet travels, one a step, before it stops and lays down what it carries.
		uint32_t maxSteps = 64;

		// The share of a droplet's direction kept from its last step, in [0, 1): the rest turns it
		// down the slope under it.
		float inertia = 0.3f;

		// Metres of ground a droplet carries, at equilibrium, per metre it drops in a step with all
		// its water: SimpleHydrology's equilibrium concentration, which does not depend on the scale.
		float capacity = 1.0f;

		// The slope, rise over run, below which a droplet carries as if the ground were this steep:
		// what lets a gentle valley floor still move some sediment.
		float minSlope = 0.01f;

		// Metres around a droplet it wears ground from, weighted toward its centre: wider than a cell,
		// so a droplet cuts a channel rather than a pit.
		float erosionRadius = 6.0f;

		// Shares, in (0, 1], of the gap to its capacity a droplet wears off or lays down a step.
		float erosionRate    = 0.3f;
		float depositionRate = 0.3f;

		// The share of a droplet's water lost a step, in [0, 1).
		float evaporation = 0.02f;

		// How much more a droplet carries where water has run often before: its capacity is scaled
		// by 1 plus this times how worn the channel under it is, in [0, 1].
		float channelErosion = 1.0f;

		// How strongly a droplet is steered along the way water ran before it, in [0, 1].
		float channelSteering = 0.5f;

		// The steepest slope loose ground keeps, in degrees: thermal erosion moves the excess down.
		float talusDegrees = 35.0f;

		// Thermal relaxations per pass; 0 runs no thermal erosion.
		uint32_t thermalIterations = 0;

		// The share of a sample's excess over the talus angle moved an iteration, in (0, 1].
		float thermalRate = 0.5f;

		[[nodiscard]] bool
		Erodes() const noexcept
		{
			return dropletsPerSample > 0.0f || thermalIterations > 0;
		}

		template <typename Self>
		Self&&
		SetBreachDepth(this Self&& self, float value) noexcept
		{
			self.breachDepth = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDropletsPerSample(this Self&& self, float value) noexcept
		{
			self.dropletsPerSample = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPasses(this Self&& self, uint32_t value) noexcept
		{
			self.passes = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaxSteps(this Self&& self, uint32_t value) noexcept
		{
			self.maxSteps = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetInertia(this Self&& self, float value) noexcept
		{
			self.inertia = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetCapacity(this Self&& self, float value) noexcept
		{
			self.capacity = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMinSlope(this Self&& self, float value) noexcept
		{
			self.minSlope = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetErosionRadius(this Self&& self, float value) noexcept
		{
			self.erosionRadius = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetErosionRate(this Self&& self, float value) noexcept
		{
			self.erosionRate = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDepositionRate(this Self&& self, float value) noexcept
		{
			self.depositionRate = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetEvaporation(this Self&& self, float value) noexcept
		{
			self.evaporation = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetChannelErosion(this Self&& self, float value) noexcept
		{
			self.channelErosion = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetChannelSteering(this Self&& self, float value) noexcept
		{
			self.channelSteering = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetTalusDegrees(this Self&& self, float value) noexcept
		{
			self.talusDegrees = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetThermalIterations(this Self&& self, uint32_t value) noexcept
		{
			self.thermalIterations = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetThermalRate(this Self&& self, float value) noexcept
		{
			self.thermalRate = value;
			return std::forward<Self>(self);
		}
	};
}
