#!/bin/bash
# Same as launcher_shim.sh, but writes to a separate log so the test can tell
# the cache-var path from the target-property override path.
set -e
echo "target-launched: $*" >> "$KILN_LAUNCHER_LOG_TARGET"
exec "$@"
