#!/bin/sh
set -eu
PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
git -C "$PROJECT_ROOT" submodule update --init --recursive
