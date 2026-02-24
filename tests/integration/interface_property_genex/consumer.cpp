// Only compiles if include_a is on the include path (via INTERFACE_ genex)
#include "from_a.h"

#ifndef FROM_PROVIDER
#error "FROM_PROVIDER should be defined via INTERFACE_COMPILE_DEFINITIONS genex"
#endif

void consumer_func() {}
