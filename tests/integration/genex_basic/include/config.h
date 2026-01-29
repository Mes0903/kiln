#pragma once

#ifdef DEBUG_BUILD
#define BUILD_TYPE "Debug"
#elif defined(RELEASE_BUILD)
#define BUILD_TYPE "Release"
#else
#define BUILD_TYPE "Unknown"
#endif
