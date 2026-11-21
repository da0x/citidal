#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define CITADEL_PLATFORM_WINDOWS 1
    #define CITADEL_PLATFORM_LINUX 0
#elif defined(__linux__)
    #define CITADEL_PLATFORM_WINDOWS 0
    #define CITADEL_PLATFORM_LINUX 1
#else
    #error "citadel builds on Linux and Windows only"
#endif
