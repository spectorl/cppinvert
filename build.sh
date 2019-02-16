#!/usr/bin/env bash

mkdir -p bin/Debug
pushd bin/Debug

cmake -DCMAKE_BUILD_TYPE=Debug ..

popd

