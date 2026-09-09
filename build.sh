#!/bin/bash
# Single native build implementation lives in the host repository.
set -eu
cd "$(dirname "$0")"
exec node scripts/build_native.js "${1:-Release}"
