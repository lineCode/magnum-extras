#!/bin/bash
set -ev

# Corrade
git clone --depth 1 git://github.com/mosra/corrade.git
cd corrade
mkdir build && cd build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=$HOME/deps \
    -DCMAKE_INSTALL_RPATH=$HOME/deps/lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_INTERCONNECT=OFF
make -j install
cd ../..

# Magnum
git clone --depth 1 git://github.com/mosra/magnum.git
cd magnum
mkdir build && cd build
cmake .. \
    -DCMAKE_INSTALL_PREFIX=$HOME/deps \
    -DCMAKE_INSTALL_RPATH=$HOME/deps/lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DTARGET_GLES=ON \
    -DTARGET_GLES2=$TARGET_GLES2 \
    -DTARGET_DESKTOP_GLES=ON \
    -DWITH_AUDIO=OFF \
    -DWITH_DEBUGTOOLS=OFF \
    -DWITH_PRIMITIVES=OFF \
    -DWITH_SCENEGRAPH=OFF \
    -DWITH_SHADERS=OFF \
    -DWITH_SHAPES=OFF \
    -DWITH_TEXT=OFF \
    -DWITH_TEXTURETOOLS=OFF \
    -DWITH_WINDOWLESS${PLATFORM_GL_API}APPLICATION=ON
make -j install
cd ../..

mkdir build && cd build
cmake .. \
    -DCMAKE_PREFIX_PATH="$HOME/deps" \
    -DCMAKE_INSTALL_PREFIX=$HOME/deps \
    -DCMAKE_INSTALL_RPATH=$HOME/deps/lib \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DBUILD_GL_TESTS=ON
make -j${JOBS_LIMIT}
CORRADE_TEST_COLOR=ON ctest -V -E GLTest
