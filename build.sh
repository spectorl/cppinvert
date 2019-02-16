#!/usr/bin/env bash

set -eou pipefail

mkdir -p bin/Debug
pushd bin/Debug

cmake -DCMAKE_BUILD_TYPE=Debug ../.. && make -j8

bin/cppinvert_test

popd

