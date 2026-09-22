#include "sample.h"

#include <editor_api/IEditorPlugin.h>

#if defined(_WIN32)
#	define SAMPLE_EXPORT __declspec(dllexport)
#else
#	define SAMPLE_EXPORT __attribute__((visibility("default")))
#endif

extern "C"
{
	SAMPLE_EXPORT editor::IEditorPlugin*
				  BerniniCreateEditorPlugin()
	{
		return sample::CreateEditorPlugin().release();
	}
}
