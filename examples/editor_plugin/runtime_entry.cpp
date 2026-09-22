#include "runtime.h"

#include <assetlib/IAssetPlugin.h>

#if defined(_WIN32)
#	define SAMPLE_EXPORT __declspec(dllexport)
#else
#	define SAMPLE_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
	SAMPLE_EXPORT assetlib::IAssetPlugin*
				  BerniniCreateAssetPlugin()
	{
		return sample::CreateAssetPlugin().release();
	}
}
