// Install libraries required for building
bash setup_external.sh

// Build it
cmake -S . -B build -G "your generator"
cmake --build build --config Release