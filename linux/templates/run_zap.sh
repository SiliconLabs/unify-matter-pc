#!/bin/bash
pushd ../third_party/connectedhomeip

output_dir=../../../linux/zap-generated/handlers

rm -rf "$output_dir"
mkdir -p "$output_dir"

./scripts/tools/zap/generate.py -t ../../../linux/templates/mpc-templates.json \
    -o "$output_dir" \
    ../../../unify-matter-pc-common/unify-matter-pc.zap

find "$output_dir" -type f -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.hpp" -o -name "*.inc" | xargs clang-format -i -style=WebKit

popd
