#include "ClockBoost_d3d12.h"
#include <nvapi_lite_common.h>
#include <spdlog/spdlog.h>
#include <string>

// nvapi.h declares its D3D entry points only once d3d12.h has been seen.
#include <directx/d3d12.h>
#include <nvapi.h>

namespace bgpu
{
	namespace
	{
		[[nodiscard]] std::string
		Describe(const NvAPI_Status status)
		{
			NvAPI_ShortString message = {};
			if (NvAPI_GetErrorMessage(status, message) != NVAPI_OK)
				return std::to_string(static_cast<int>(status));
			return message;
		}
	}

	D3d12ClockBoost::D3d12ClockBoost(ID3D12Device* const device) noexcept
	{
		const NvAPI_Status initialized = NvAPI_Initialize();
		if (initialized != NVAPI_OK)
		{
			spdlog::warn(
				"maximum GPU performance unavailable: no NVIDIA driver to ask ({})",
				Describe(initialized));
			return;
		}
		m_Loaded = true;

		// Reflex's sleep mode with nothing on but the boost: no latency mode, no frame limit.
		auto params             = NV_SET_SLEEP_MODE_PARAMS();
		params.version          = NV_SET_SLEEP_MODE_PARAMS_VER;
		params.bLowLatencyMode  = false;
		params.bLowLatencyBoost = true;
		const NvAPI_Status set  = NvAPI_D3D_SetSleepMode(device, &params);
		if (set != NVAPI_OK)
		{
			spdlog::warn(
				"maximum GPU performance unavailable: the device or its driver has no Reflex boost "
				"({})",
				Describe(set));
			return;
		}

		m_Granted = true;
		spdlog::info("maximum GPU performance: requested from the NVIDIA driver (Reflex boost)");
	}

	D3d12ClockBoost::~D3d12ClockBoost() noexcept
	{
		if (m_Loaded)
			NvAPI_Unload();
	}
}
