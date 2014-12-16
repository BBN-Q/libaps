#!/bin/sh

mkdir release
mkdir release/build
mkdir release/src

# uncomment for the appropriate platform
cp build/*.dll release/build/
#cp build/*.dylib release/build/
#cp build/*.so release/build/

cp -R bitfiles release/
cp -R examples release/
cp -R src/python release/src/
cp -R src/matlab release/src/
