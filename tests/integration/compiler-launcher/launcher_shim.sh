#!/bin/bash
# Test shim that records each invocation (one line per compile) before
# delegating to the real compiler. The integration test asserts the side-effect
# file exists and contains at least one entry, proving kiln prepended this
# launcher to the compile argv.
set -e
echo "launched: $*" >> "$KILN_LAUNCHER_LOG"
exec "$@"
