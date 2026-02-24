// This file defines BUILD_REMOVED_API before any includes, mimicking Qt's
// removed_api.cpp pattern. If the PCH is force-included before this file's
// content, the macro ordering is wrong and removed_value() returns 0.
#define BUILD_REMOVED_API

#include "removed_api.h"

#ifdef BUILD_REMOVED_API
int removed_value() { return 42; }
#else
int removed_value() { return 0; }
#endif
