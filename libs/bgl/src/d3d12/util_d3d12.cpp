#include "util_d3d12.h"
#include "resource/Texture.h"
#include "types/Color.h"
#include "util/util.h"

namespace bgl
{
#define HANDLE_INVALID_DXGI_FORMAT_CASE(dxgiFormat) \
	case dxgiFormat:                                \
		gfatal("Unsupported DXGI format: {}", #dxgiFormat)

	Format
	ConvertFormat(DXGI_FORMAT dxgiFormat)
	{
		switch (dxgiFormat)
		{
		case DXGI_FORMAT_UNKNOWN:
			return Format::UNKNOWN;

		// R8
		case DXGI_FORMAT_R8_UINT:
			return Format::R8_UINT;
		case DXGI_FORMAT_R8_SINT:
			return Format::R8_SINT;
		case DXGI_FORMAT_R8_UNORM:
			return Format::R8_UNORM;
		case DXGI_FORMAT_R8_SNORM:
			return Format::R8_SNORM;

		// RG8
		case DXGI_FORMAT_R8G8_UINT:
			return Format::RG8_UINT;
		case DXGI_FORMAT_R8G8_SINT:
			return Format::RG8_SINT;
		case DXGI_FORMAT_R8G8_UNORM:
			return Format::RG8_UNORM;
		case DXGI_FORMAT_R8G8_SNORM:
			return Format::RG8_SNORM;

		// R16
		case DXGI_FORMAT_R16_UINT:
			return Format::R16_UINT;
		case DXGI_FORMAT_R16_SINT:
			return Format::R16_SINT;
		case DXGI_FORMAT_R16_UNORM:
			return Format::R16_UNORM;
		case DXGI_FORMAT_R16_SNORM:
			return Format::R16_SNORM;
		case DXGI_FORMAT_R16_FLOAT:
			return Format::R16_FLOAT;

		// Legacy / 16-bit Packed Packed
		case DXGI_FORMAT_B4G4R4A4_UNORM:
			return Format::BGRA4_UNORM;
		case DXGI_FORMAT_B5G6R5_UNORM:
			return Format::B5G6R5_UNORM;
		case DXGI_FORMAT_B5G5R5A1_UNORM:
			return Format::B5G5R5A1_UNORM;

		// RGBA8
		case DXGI_FORMAT_R8G8B8A8_UINT:
			return Format::RGBA8_UINT;
		case DXGI_FORMAT_R8G8B8A8_SINT:
			return Format::RGBA8_SINT;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
			return Format::RGBA8_UNORM;
		case DXGI_FORMAT_R8G8B8A8_SNORM:
			return Format::RGBA8_SNORM;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
			return Format::SRGBA8_UNORM;

		// BGRA8 / BGRX8
		case DXGI_FORMAT_B8G8R8A8_UNORM:
			return Format::BGRA8_UNORM;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
			return Format::SBGRA8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM:
			return Format::BGRX8_UNORM;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
			return Format::SBGRX8_UNORM;

		// Packed 32-bit
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return Format::R10G10B10A2_UNORM;
		case DXGI_FORMAT_R11G11B10_FLOAT:
			return Format::R11G11B10_FLOAT;

		// RG16
		case DXGI_FORMAT_R16G16_UINT:
			return Format::RG16_UINT;
		case DXGI_FORMAT_R16G16_SINT:
			return Format::RG16_SINT;
		case DXGI_FORMAT_R16G16_UNORM:
			return Format::RG16_UNORM;
		case DXGI_FORMAT_R16G16_SNORM:
			return Format::RG16_SNORM;
		case DXGI_FORMAT_R16G16_FLOAT:
			return Format::RG16_FLOAT;

		// R32
		case DXGI_FORMAT_R32_UINT:
			return Format::R32_UINT;
		case DXGI_FORMAT_R32_SINT:
			return Format::R32_SINT;
		case DXGI_FORMAT_R32_FLOAT:
			return Format::R32_FLOAT;

		// RGBA16
		case DXGI_FORMAT_R16G16B16A16_UINT:
			return Format::RGBA16_UINT;
		case DXGI_FORMAT_R16G16B16A16_SINT:
			return Format::RGBA16_SINT;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
			return Format::RGBA16_UNORM;
		case DXGI_FORMAT_R16G16B16A16_SNORM:
			return Format::RGBA16_SNORM;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return Format::RGBA16_FLOAT;

		// RG32
		case DXGI_FORMAT_R32G32_UINT:
			return Format::RG32_UINT;
		case DXGI_FORMAT_R32G32_SINT:
			return Format::RG32_SINT;
		case DXGI_FORMAT_R32G32_FLOAT:
			return Format::RG32_FLOAT;

		// RGB32
		case DXGI_FORMAT_R32G32B32_UINT:
			return Format::RGB32_UINT;
		case DXGI_FORMAT_R32G32B32_SINT:
			return Format::RGB32_SINT;
		case DXGI_FORMAT_R32G32B32_FLOAT:
			return Format::RGB32_FLOAT;

		// RGBA32
		case DXGI_FORMAT_R32G32B32A32_UINT:
			return Format::RGBA32_UINT;
		case DXGI_FORMAT_R32G32B32A32_SINT:
			return Format::RGBA32_SINT;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return Format::RGBA32_FLOAT;

		// Depth / Stencil
		case DXGI_FORMAT_D16_UNORM:
			return Format::D16;
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
			return Format::D24S8;
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return Format::X24G8_UINT;
		case DXGI_FORMAT_D32_FLOAT:
			return Format::D32;
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
			return Format::D32S8;
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return Format::X32G8_UINT;

		// Block Compression (BC)
		case DXGI_FORMAT_BC1_UNORM:
			return Format::BC1_UNORM;
		case DXGI_FORMAT_BC1_UNORM_SRGB:
			return Format::BC1_UNORM_SRGB;
		case DXGI_FORMAT_BC2_UNORM:
			return Format::BC2_UNORM;
		case DXGI_FORMAT_BC2_UNORM_SRGB:
			return Format::BC2_UNORM_SRGB;
		case DXGI_FORMAT_BC3_UNORM:
			return Format::BC3_UNORM;
		case DXGI_FORMAT_BC3_UNORM_SRGB:
			return Format::BC3_UNORM_SRGB;
		case DXGI_FORMAT_BC4_UNORM:
			return Format::BC4_UNORM;
		case DXGI_FORMAT_BC4_SNORM:
			return Format::BC4_SNORM;
		case DXGI_FORMAT_BC5_UNORM:
			return Format::BC5_UNORM;
		case DXGI_FORMAT_BC5_SNORM:
			return Format::BC5_SNORM;
		case DXGI_FORMAT_BC6H_UF16:
			return Format::BC6H_UFLOAT;
		case DXGI_FORMAT_BC6H_SF16:
			return Format::BC6H_SFLOAT;
		case DXGI_FORMAT_BC7_UNORM:
			return Format::BC7_UNORM;
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return Format::BC7_UNORM_SRGB;

			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R32G32B32A32_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R32G32_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R32G8X24_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R10G10B10A2_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R10G10B10A2_UINT);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R8G8B8A8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R32_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R24G8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R8G8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R16_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_A8_UNORM);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R1_UNORM);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R9G9B9E5_SHAREDEXP);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R8G8_B8G8_UNORM);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_G8R8_G8B8_UNORM);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC1_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC2_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC3_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC4_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC5_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_B8G8R8A8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_B8G8R8X8_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC6H_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_BC7_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_AYUV);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_Y410);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_Y416);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_NV12);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_P010);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_P016);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_420_OPAQUE);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_YUY2);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_Y210);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_Y216);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_NV11);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_AI44);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_IA44);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_P8);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_A8P8);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_P208);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_V208);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_V408);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_A4B4G4R4_UNORM);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_FORCE_UINT);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R32G32B32_TYPELESS);
			HANDLE_INVALID_DXGI_FORMAT_CASE(DXGI_FORMAT_R16G16B16A16_TYPELESS);
		default:
			gfatal("ConvertFormat Invalid format: {}", static_cast<int>(dxgiFormat));
		}
	}

	DXGI_FORMAT
	ConvertFormat(Format bglFormat)
	{
		switch (bglFormat)
		{
		case Format::UNKNOWN:
			return DXGI_FORMAT_UNKNOWN;

		// R8
		case Format::R8_UINT:
			return DXGI_FORMAT_R8_UINT;
		case Format::R8_SINT:
			return DXGI_FORMAT_R8_SINT;
		case Format::R8_UNORM:
			return DXGI_FORMAT_R8_UNORM;
		case Format::R8_SNORM:
			return DXGI_FORMAT_R8_SNORM;

		// RG8
		case Format::RG8_UINT:
			return DXGI_FORMAT_R8G8_UINT;
		case Format::RG8_SINT:
			return DXGI_FORMAT_R8G8_SINT;
		case Format::RG8_UNORM:
			return DXGI_FORMAT_R8G8_UNORM;
		case Format::RG8_SNORM:
			return DXGI_FORMAT_R8G8_SNORM;

		// R16
		case Format::R16_UINT:
			return DXGI_FORMAT_R16_UINT;
		case Format::R16_SINT:
			return DXGI_FORMAT_R16_SINT;
		case Format::R16_UNORM:
			return DXGI_FORMAT_R16_UNORM;
		case Format::R16_SNORM:
			return DXGI_FORMAT_R16_SNORM;
		case Format::R16_FLOAT:
			return DXGI_FORMAT_R16_FLOAT;

		// Legacy / 16-bit Packed Packed
		case Format::BGRA4_UNORM:
			return DXGI_FORMAT_B4G4R4A4_UNORM;
		case Format::B5G6R5_UNORM:
			return DXGI_FORMAT_B5G6R5_UNORM;
		case Format::B5G5R5A1_UNORM:
			return DXGI_FORMAT_B5G5R5A1_UNORM;

		// RGBA8
		case Format::RGBA8_UINT:
			return DXGI_FORMAT_R8G8B8A8_UINT;
		case Format::RGBA8_SINT:
			return DXGI_FORMAT_R8G8B8A8_SINT;
		case Format::RGBA8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		case Format::RGBA8_SNORM:
			return DXGI_FORMAT_R8G8B8A8_SNORM;
		case Format::SRGBA8_UNORM:
			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

		// BGRA8 / BGRX8
		case Format::BGRA8_UNORM:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case Format::SBGRA8_UNORM:
			return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		case Format::BGRX8_UNORM:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case Format::SBGRX8_UNORM:
			return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;

		// Packed 32-bit
		case Format::R10G10B10A2_UNORM:
			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case Format::R11G11B10_FLOAT:
			return DXGI_FORMAT_R11G11B10_FLOAT;

		// RG16
		case Format::RG16_UINT:
			return DXGI_FORMAT_R16G16_UINT;
		case Format::RG16_SINT:
			return DXGI_FORMAT_R16G16_SINT;
		case Format::RG16_UNORM:
			return DXGI_FORMAT_R16G16_UNORM;
		case Format::RG16_SNORM:
			return DXGI_FORMAT_R16G16_SNORM;
		case Format::RG16_FLOAT:
			return DXGI_FORMAT_R16G16_FLOAT;

		// R32
		case Format::R32_UINT:
			return DXGI_FORMAT_R32_UINT;
		case Format::R32_SINT:
			return DXGI_FORMAT_R32_SINT;
		case Format::R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;

		// RGBA16
		case Format::RGBA16_UINT:
			return DXGI_FORMAT_R16G16B16A16_UINT;
		case Format::RGBA16_SINT:
			return DXGI_FORMAT_R16G16B16A16_SINT;
		case Format::RGBA16_UNORM:
			return DXGI_FORMAT_R16G16B16A16_UNORM;
		case Format::RGBA16_SNORM:
			return DXGI_FORMAT_R16G16B16A16_SNORM;
		case Format::RGBA16_FLOAT:
			return DXGI_FORMAT_R16G16B16A16_FLOAT;

		// RG32
		case Format::RG32_UINT:
			return DXGI_FORMAT_R32G32_UINT;
		case Format::RG32_SINT:
			return DXGI_FORMAT_R32G32_SINT;
		case Format::RG32_FLOAT:
			return DXGI_FORMAT_R32G32_FLOAT;

		// RGB32
		case Format::RGB32_UINT:
			return DXGI_FORMAT_R32G32B32_UINT;
		case Format::RGB32_SINT:
			return DXGI_FORMAT_R32G32B32_SINT;
		case Format::RGB32_FLOAT:
			return DXGI_FORMAT_R32G32B32_FLOAT;

		// RGBA32
		case Format::RGBA32_UINT:
			return DXGI_FORMAT_R32G32B32A32_UINT;
		case Format::RGBA32_SINT:
			return DXGI_FORMAT_R32G32B32A32_SINT;
		case Format::RGBA32_FLOAT:
			return DXGI_FORMAT_R32G32B32A32_FLOAT;

		// Depth / Stencil
		case Format::D16:
			return DXGI_FORMAT_D16_UNORM;
		case Format::D24S8:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;
		case Format::X24G8_UINT:
			return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
		case Format::D32:
			return DXGI_FORMAT_D32_FLOAT;
		case Format::D32S8:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
		case Format::X32G8_UINT:
			return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;

		// Block Compression (BC)
		case Format::BC1_UNORM:
			return DXGI_FORMAT_BC1_UNORM;
		case Format::BC1_UNORM_SRGB:
			return DXGI_FORMAT_BC1_UNORM_SRGB;
		case Format::BC2_UNORM:
			return DXGI_FORMAT_BC2_UNORM;
		case Format::BC2_UNORM_SRGB:
			return DXGI_FORMAT_BC2_UNORM_SRGB;
		case Format::BC3_UNORM:
			return DXGI_FORMAT_BC3_UNORM;
		case Format::BC3_UNORM_SRGB:
			return DXGI_FORMAT_BC3_UNORM_SRGB;
		case Format::BC4_UNORM:
			return DXGI_FORMAT_BC4_UNORM;
		case Format::BC4_SNORM:
			return DXGI_FORMAT_BC4_SNORM;
		case Format::BC5_UNORM:
			return DXGI_FORMAT_BC5_UNORM;
		case Format::BC5_SNORM:
			return DXGI_FORMAT_BC5_SNORM;
		case Format::BC6H_UFLOAT:
			return DXGI_FORMAT_BC6H_UF16;
		case Format::BC6H_SFLOAT:
			return DXGI_FORMAT_BC6H_SF16;
		case Format::BC7_UNORM:
			return DXGI_FORMAT_BC7_UNORM;
		case Format::BC7_UNORM_SRGB:
			return DXGI_FORMAT_BC7_UNORM_SRGB;

		case Format::COUNT:
		default:
			gfatal("ConvertFormat Invalid format: {}", static_cast<int>(bglFormat));
		}
	}

	D3D12_RESOURCE_DIMENSION
	ConvertResourceDimension(TextureDimension dimension)
	{
		switch (dimension)
		{
		case TextureDimension::kTexture1D:
			return D3D12_RESOURCE_DIMENSION_TEXTURE1D;

		case TextureDimension::kTexture2D:
			return D3D12_RESOURCE_DIMENSION_TEXTURE2D;

		case TextureDimension::kTexture3D:
			return D3D12_RESOURCE_DIMENSION_TEXTURE3D;

		case TextureDimension::kUnknown:
		case TextureDimension::kTexture1DArray:
		case TextureDimension::kTexture2DArray:
		case TextureDimension::kTextureCube:
		case TextureDimension::kTextureCubeArray:
		case TextureDimension::kTexture2DMS:
		case TextureDimension::kTexture2DMSArray:
		default:
			gfatal(
				"ConvertTextureDimension Invalid texture dimension: {}",
				static_cast<int>(dimension));
		}
	}

	D3D12_RTV_DIMENSION
	ConvertRTVDimension(TextureDimension dimension)
	{
		switch (dimension)
		{
		case TextureDimension::kTexture1D:
			return D3D12_RTV_DIMENSION_TEXTURE1D;

		case TextureDimension::kTexture1DArray:
			return D3D12_RTV_DIMENSION_TEXTURE1DARRAY;

		case TextureDimension::kTexture2D:
			return D3D12_RTV_DIMENSION_TEXTURE2D;

		case TextureDimension::kTexture2DArray:
			return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;

		case TextureDimension::kTextureCube:
			return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;

		case TextureDimension::kTextureCubeArray:
			return D3D12_RTV_DIMENSION_TEXTURE2DARRAY;

		case TextureDimension::kTexture2DMS:
			return D3D12_RTV_DIMENSION_TEXTURE2DMS;

		case TextureDimension::kTexture2DMSArray:
			return D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;

		case TextureDimension::kTexture3D:
			return D3D12_RTV_DIMENSION_TEXTURE3D;

		case TextureDimension::kUnknown:
		default:
			gfatal(
				"ConvertRTVDimension Invalid texture dimension: {}",
				static_cast<int>(dimension));
		}
	}

	D3D12_DSV_DIMENSION
	ConvertDSVDimension(TextureDimension dimension)
	{
		switch (dimension)
		{
		case TextureDimension::kTexture1D:
			return D3D12_DSV_DIMENSION_TEXTURE1D;

		case TextureDimension::kTexture1DArray:
			return D3D12_DSV_DIMENSION_TEXTURE1DARRAY;

		case TextureDimension::kTexture2D:
			return D3D12_DSV_DIMENSION_TEXTURE2D;

		case TextureDimension::kTexture2DArray:
			return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;

		case TextureDimension::kTextureCube:
			return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;

		case TextureDimension::kTextureCubeArray:
			return D3D12_DSV_DIMENSION_TEXTURE2DARRAY;

		case TextureDimension::kTexture2DMS:
			return D3D12_DSV_DIMENSION_TEXTURE2DMS;

		case TextureDimension::kTexture2DMSArray:
			return D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;

		case TextureDimension::kTexture3D:
		case TextureDimension::kUnknown:
		default:
			gfatal(
				"ConvertDSVDimension Invalid or unsupported depth texture dimension: {}",
				static_cast<int>(dimension));
		}
	}

	D3D12_BARRIER_SYNC
	ConvertBarrierSync(BarrierSync sync)
	{
		if (sync == BarrierSyncFlag::kNone)
		{
			return D3D12_BARRIER_SYNC_NONE;
		}

		uint32_t result = D3D12_BARRIER_SYNC_NONE;

		if (sync & BarrierSyncFlag::kAllCommands)
			result |= D3D12_BARRIER_SYNC_ALL;
		if (sync & BarrierSyncFlag::kCopy)
			result |= D3D12_BARRIER_SYNC_COPY;
		if (sync & BarrierSyncFlag::kResolve)
			result |= D3D12_BARRIER_SYNC_RESOLVE;
		if (sync & BarrierSyncFlag::kInputAssembler)
			result |= D3D12_BARRIER_SYNC_INDEX_INPUT;
		if (sync & BarrierSyncFlag::kVertexShader)
			result |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
		if (sync & BarrierSyncFlag::kPixelShader)
			result |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
		if (sync & BarrierSyncFlag::kComputeShader)
			result |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
		if (sync & BarrierSyncFlag::kRenderTarget)
			result |= D3D12_BARRIER_SYNC_RENDER_TARGET;
		if (sync & BarrierSyncFlag::kDepthStencil)
			result |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
		if (sync & BarrierSyncFlag::kRayTracing)
			result |= D3D12_BARRIER_SYNC_RAYTRACING;
		if (sync & BarrierSyncFlag::kIndirectArgument)
			result |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;

		return static_cast<D3D12_BARRIER_SYNC>(result);
	}

	// 2. Mapping BarrierAccess to D3D12_BARRIER_ACCESS
	D3D12_BARRIER_ACCESS
	ConvertBarrierAccess(BarrierAccess access)
	{
		if (access == BarrierAccessFlag::kNone)
		{
			return D3D12_BARRIER_ACCESS_NO_ACCESS;
		}

		uint32_t result = 0;

		if (access & BarrierAccessFlag::kIndexBuffer)
			result |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		if (access & BarrierAccessFlag::kVertexBuffer)
			result |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		if (access & BarrierAccessFlag::kConstantBuffer)
			result |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
		if (access & BarrierAccessFlag::kShaderResource)
			result |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
		if (access & BarrierAccessFlag::kUnorderedAccess)
			result |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
		if (access & BarrierAccessFlag::kRenderTarget)
			result |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
		if (access & BarrierAccessFlag::kDepthWrite)
			result |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
		if (access & BarrierAccessFlag::kDepthRead)
			result |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
		if (access & BarrierAccessFlag::kIndirectArgument)
			result |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
		if (access & BarrierAccessFlag::kCopySource)
			result |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
		if (access & BarrierAccessFlag::kCopyDest)
			result |= D3D12_BARRIER_ACCESS_COPY_DEST;
		if (access & BarrierAccessFlag::kAccelStructRead)
			result |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
		if (access & BarrierAccessFlag::kAccelStructWrite)
			result |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;

		return static_cast<D3D12_BARRIER_ACCESS>(result);
	}

	D3D12_BARRIER_LAYOUT
	ConvertBarrierLayout(BarrierLayout layout)
	{
		switch (layout)
		{
		case BarrierLayout::kUndefined:
			return D3D12_BARRIER_LAYOUT_UNDEFINED;
		case BarrierLayout::kCommon:
			return D3D12_BARRIER_LAYOUT_COMMON;
		case BarrierLayout::kPresent:
			return D3D12_BARRIER_LAYOUT_PRESENT;
		case BarrierLayout::kGenericRead:
			return D3D12_BARRIER_LAYOUT_GENERIC_READ;
		case BarrierLayout::kShaderResource:
			return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		case BarrierLayout::kUnorderedAccess:
			return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
		case BarrierLayout::kRenderTarget:
			return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
		case BarrierLayout::kDepthWrite:
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
		case BarrierLayout::kDepthRead:
			return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
		case BarrierLayout::kCopySource:
			return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
		case BarrierLayout::kCopyDest:
			return D3D12_BARRIER_LAYOUT_COPY_DEST;
		default:
			gfatal("Unknown BarrierLayout enum passed to conversion utility.");
		}
	}

	D3D12_COMMAND_LIST_TYPE
	ConvertQueueType(QueueType queueType)
	{
		switch (queueType)
		{
		case QueueType::kGraphics:
			return D3D12_COMMAND_LIST_TYPE_DIRECT;
		case QueueType::kCompute:
			return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case QueueType::kCopy:
			return D3D12_COMMAND_LIST_TYPE_COPY;
		}

		return D3D12_COMMAND_LIST_TYPE_NONE;
	}

	D3D12_BLEND
	ConvertBlendValue(BlendFactor value)
	{
		switch (value)
		{
		case BlendFactor::kZero:
			return D3D12_BLEND_ZERO;
		case BlendFactor::kOne:
			return D3D12_BLEND_ONE;
		case BlendFactor::kSrcColor:
			return D3D12_BLEND_SRC_COLOR;
		case BlendFactor::kInvSrcColor:
			return D3D12_BLEND_INV_SRC_COLOR;
		case BlendFactor::kSrcAlpha:
			return D3D12_BLEND_SRC_ALPHA;
		case BlendFactor::kInvSrcAlpha:
			return D3D12_BLEND_INV_SRC_ALPHA;
		case BlendFactor::kDstAlpha:
			return D3D12_BLEND_DEST_ALPHA;
		case BlendFactor::kInvDstAlpha:
			return D3D12_BLEND_INV_DEST_ALPHA;
		case BlendFactor::kDstColor:
			return D3D12_BLEND_DEST_COLOR;
		case BlendFactor::kInvDstColor:
			return D3D12_BLEND_INV_DEST_COLOR;
		case BlendFactor::kSrcAlphaSaturate:
			return D3D12_BLEND_SRC_ALPHA_SAT;
		case BlendFactor::kConstantColor:
			return D3D12_BLEND_BLEND_FACTOR;
		case BlendFactor::kInvConstantColor:
			return D3D12_BLEND_INV_BLEND_FACTOR;
		case BlendFactor::kSrc1Color:
			return D3D12_BLEND_SRC1_COLOR;
		case BlendFactor::kInvSrc1Color:
			return D3D12_BLEND_INV_SRC1_COLOR;
		case BlendFactor::kSrc1Alpha:
			return D3D12_BLEND_SRC1_ALPHA;
		case BlendFactor::kInvSrc1Alpha:
			return D3D12_BLEND_INV_SRC1_ALPHA;
		default:
			gfatal("Unknown BlendFactor value");
		}
	}

	D3D12_BLEND_OP
	ConvertBlendOp(BlendOp value)
	{
		switch (value)
		{
		case BlendOp::kAdd:
			return D3D12_BLEND_OP_ADD;
		case BlendOp::kSubtract:
			return D3D12_BLEND_OP_SUBTRACT;
		case BlendOp::kReverseSubtract:
			return D3D12_BLEND_OP_REV_SUBTRACT;
		case BlendOp::kMin:
			return D3D12_BLEND_OP_MIN;
		case BlendOp::kMax:
			return D3D12_BLEND_OP_MAX;
		default:
			gfatal("Unknown BlendOp value");
		}
	}

	D3D12_BLEND_DESC
	ConvertBlendState(BlendState inState)
	{
		D3D12_BLEND_DESC outState{};
		outState.AlphaToCoverageEnable  = inState.alphaToCoverageEnable;
		outState.IndependentBlendEnable = true;

		for (uint32_t i = 0; i < c_MaxRenderTargets; i++)
		{
			const BlendState::RenderTarget& src = inState.targets[i];
			D3D12_RENDER_TARGET_BLEND_DESC& dst = outState.RenderTarget[i];

			dst.BlendEnable           = src.blendEnable ? TRUE : FALSE;
			dst.SrcBlend              = ConvertBlendValue(src.srcBlend);
			dst.DestBlend             = ConvertBlendValue(src.destBlend);
			dst.BlendOp               = ConvertBlendOp(src.blendOp);
			dst.SrcBlendAlpha         = ConvertBlendValue(src.srcBlendAlpha);
			dst.DestBlendAlpha        = ConvertBlendValue(src.destBlendAlpha);
			dst.BlendOpAlpha          = ConvertBlendOp(src.blendOpAlpha);
			dst.RenderTargetWriteMask = (D3D12_COLOR_WRITE_ENABLE)src.colorWriteMask;
		}

		return outState;
	}

	D3D12_DEPTH_STENCIL_DESC
	ConvertDepthStencilState(DepthStencilState inState)
	{
		D3D12_DEPTH_STENCIL_DESC outState{};

		outState.DepthEnable = inState.depthTestEnable ? TRUE : FALSE;
		outState.DepthWriteMask =
			inState.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
		outState.DepthFunc               = ConvertComparisonFunc(inState.depthFunc);
		outState.StencilEnable           = inState.stencilEnable ? TRUE : FALSE;
		outState.StencilReadMask         = (UINT8)inState.stencilReadMask;
		outState.StencilWriteMask        = (UINT8)inState.stencilWriteMask;
		outState.FrontFace.StencilFailOp = ConvertStencilOp(inState.frontFaceStencil.failOp);
		outState.FrontFace.StencilDepthFailOp =
			ConvertStencilOp(inState.frontFaceStencil.depthFailOp);
		outState.FrontFace.StencilPassOp = ConvertStencilOp(inState.frontFaceStencil.passOp);
		outState.FrontFace.StencilFunc =
			ConvertComparisonFunc(inState.frontFaceStencil.stencilFunc);
		outState.BackFace.StencilFailOp = ConvertStencilOp(inState.backFaceStencil.failOp);
		outState.BackFace.StencilDepthFailOp =
			ConvertStencilOp(inState.backFaceStencil.depthFailOp);
		outState.BackFace.StencilPassOp = ConvertStencilOp(inState.backFaceStencil.passOp);
		outState.BackFace.StencilFunc = ConvertComparisonFunc(inState.backFaceStencil.stencilFunc);

		return outState;
	}

	D3D12_RASTERIZER_DESC
	ConvertRasterState(RasterState inState)
	{
		D3D12_RASTERIZER_DESC outState{};

		switch (inState.fillMode)
		{
		case RasterFillMode::kSolid:
			outState.FillMode = D3D12_FILL_MODE_SOLID;
			break;
		case RasterFillMode::kWireframe:
			outState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			break;
		default:
			gfatal("Unknown RasterFillMode value");
			break;
		}

		switch (inState.cullMode)
		{
		case RasterCullMode::kBack:
			outState.CullMode = D3D12_CULL_MODE_BACK;
			break;
		case RasterCullMode::kFront:
			outState.CullMode = D3D12_CULL_MODE_FRONT;
			break;
		case RasterCullMode::kNone:
			outState.CullMode = D3D12_CULL_MODE_NONE;
			break;
		default:
			gfatal("Unknown RasterCullMode value");
			break;
		}

		outState.FrontCounterClockwise = inState.frontCounterClockwise ? TRUE : FALSE;
		outState.DepthBias             = inState.depthBias;
		outState.DepthBiasClamp        = inState.depthBiasClamp;
		outState.SlopeScaledDepthBias  = inState.slopeScaledDepthBias;
		outState.DepthClipEnable       = inState.depthClipEnable ? TRUE : FALSE;
		outState.MultisampleEnable     = inState.multisampleEnable ? TRUE : FALSE;
		outState.AntialiasedLineEnable = inState.antialiasedLineEnable ? TRUE : FALSE;
		outState.ConservativeRaster    = inState.conservativeRasterEnable ?
		                                     D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON :
		                                     D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		outState.ForcedSampleCount     = inState.forcedSampleCount;

		return outState;
	}

	D3D12_STENCIL_OP
	ConvertStencilOp(StencilOp value)
	{
		switch (value)
		{
		case StencilOp::Keep:
			return D3D12_STENCIL_OP_KEEP;
		case StencilOp::Zero:
			return D3D12_STENCIL_OP_ZERO;
		case StencilOp::Replace:
			return D3D12_STENCIL_OP_REPLACE;
		case StencilOp::IncrementAndClamp:
			return D3D12_STENCIL_OP_INCR_SAT;
		case StencilOp::DecrementAndClamp:
			return D3D12_STENCIL_OP_DECR_SAT;
		case StencilOp::Invert:
			return D3D12_STENCIL_OP_INVERT;
		case StencilOp::IncrementAndWrap:
			return D3D12_STENCIL_OP_INCR;
		case StencilOp::DecrementAndWrap:
			return D3D12_STENCIL_OP_DECR;
		default:
			gfatal("Unknown StencilOp value");
		}
	}

	D3D12_COMPARISON_FUNC
	ConvertComparisonFunc(ComparisonFunc value)
	{
		switch (value)
		{
		case ComparisonFunc::Never:
			return D3D12_COMPARISON_FUNC_NEVER;
		case ComparisonFunc::Less:
			return D3D12_COMPARISON_FUNC_LESS;
		case ComparisonFunc::Equal:
			return D3D12_COMPARISON_FUNC_EQUAL;
		case ComparisonFunc::LessOrEqual:
			return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case ComparisonFunc::Greater:
			return D3D12_COMPARISON_FUNC_GREATER;
		case ComparisonFunc::NotEqual:
			return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case ComparisonFunc::GreaterOrEqual:
			return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case ComparisonFunc::Always:
			return D3D12_COMPARISON_FUNC_ALWAYS;
		default:
			gfatal("Unknown ComparisonFunc value");
		}
	}

	D3D12_CLEAR_VALUE
	ConvertClearValue(Format format, ClearValue clearValue)
	{
		auto d3d12ClearValue = D3D12_CLEAR_VALUE();
		auto formatInfo      = GetFormatInfo(format);

		if (formatInfo.hasDepth || formatInfo.hasStencil)
		{
			gassert(clearValue.IsDepthStencil(), "Clear Value depth stencil expected");

			auto& depthStencil                   = clearValue.GetDepthStencil();
			d3d12ClearValue.DepthStencil.Depth   = depthStencil.depth;
			d3d12ClearValue.DepthStencil.Stencil = depthStencil.stencil;
		}
		else
		{
			gassert(clearValue.IsColor(), "Clear Value color expected");

			auto& color              = clearValue.GetColor();
			d3d12ClearValue.Color[0] = color.r;
			d3d12ClearValue.Color[1] = color.g;
			d3d12ClearValue.Color[2] = color.b;
			d3d12ClearValue.Color[3] = color.a;
		}

		d3d12ClearValue.Format = ConvertFormat(format);

		return d3d12ClearValue;
	}
}
