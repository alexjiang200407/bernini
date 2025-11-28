#pragma once

#ifdef _WIN32
    #ifdef RENDERER_EXPORTS
        #define RENDERER_API __declspec(dllexport)
    #else
        #define RENDERER_API __declspec(dllimport)
    #endif
#else
    #define RENDERER_API
#endif

extern "C" RENDERER_API const char* DummyExport();