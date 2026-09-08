#!/bin/bash
set -eu
PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
exec bash "$PROJECT_ROOT/third_party/nle-sdk/scripts/build_skia.sh" "$@"
