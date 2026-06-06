#!/bin/bash
set -e

echo "=== Installing build dependencies ==="
apt-get update -qq
apt-get install -y -qq cmake g++ qtbase5-dev 2>&1 | tail -5

# Optional packages - FetchContent will handle if not available
apt-get install -y -qq libspdlog-dev nlohmann-json3-dev libyaml-cpp-dev 2>&1 | tail -3 || echo "Optional packages unavailable, will use FetchContent"

echo "=== Configuring with CMake ==="
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON -DBUILD_SERVER=OFF -DBUILD_CLIENT=OFF

echo "=== Building auto-test ==="
make -j$(nproc 2>/dev/null || echo 4) voice-auto-test

echo "=== Running tests ==="
./test/auto-test/voice-auto-test -v1

echo "=== All CI checks passed! ==="
