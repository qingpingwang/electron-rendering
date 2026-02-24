#!/bin/sh
workdir=$(cd $(dirname $0); pwd)
cd ${workdir}

echo "[start] submodule init at `date`"
git submodule update --init
if [[ $? -ne 0 ]]; then
    echo "ERROR: Failed to initialize submodule"
    exit -1
fi
echo "[end] submodule init at `date`"