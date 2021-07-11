# pthread Grayscaler

Simple educational-purpose multi-threaded image grayscaler - pthread version

Currently, only jpeg images are supported using `libjpeg`.

## Usage

1. Configure

   You can configure

      - input image name (`INPUT_IMAGE_FILENAME`)
      - output images name (`OUTPUT_IMAGE_FILENAME`)
      - number of worker threads (`NUM_THREADS`)

   configuration parameters in [`config.h`](/config.h) file.

2. Build

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

   _Change `Release` to `Debug` in order to include debugg symbol files with output executable._

3. Run

   ```sh
   ./build/grayscale
   ```

## Development

You'll need CMake and a C compiler. I used CMake version `3.20.5` and clang version `12.0.1`.
If you want to use another compiler, set its path in `CMakeLists.txt` file (`CMAKE_C_COMPILER` configuration variable).
