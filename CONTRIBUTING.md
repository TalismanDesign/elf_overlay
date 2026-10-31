# Contributing

Thanks for your interest in `elf_overlay`. This document covers how to build,
test, and format the project before opening a pull request.

## Building and testing on the host

The host build compiles the library, the tools, and the test suite:

```bash
cmake -S . -B build -DELF_OVERLAY_WERROR=ON
cmake --build build
ctest --test-dir build
```

Run the suite under AddressSanitizer and UBSan before submitting changes that
touch the runtime or the host tools:

```bash
cmake -S . -B build-asan -DELF_OVERLAY_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan
```

## Regenerating the fixture

`examples/host_pipeline` holds a committed Xtensa fixture and its overlay
blobs. If a change alters the output of the host tools, regenerate and verify the
vectors with `xtensa-esp32s3-elf-gcc` and the built tools on `PATH`:

```bash
export PATH="$PWD/build:$PATH"
make -C examples/host_pipeline check
```

`make check` fails if the regenerated artifacts differ from the committed ones.
Commit the updated vectors together with the code change that produced them.

## Building the device component

The ESP-IDF example proves the on-device path compiles and links:

```bash
idf.py -C examples/esp_idf_component \
  -DELF_LOADER_DIR=/path/to/elf_loader set-target esp32s3 build
```

## Formatting

`.clang-format` defines the C style (LLVM base, 4-space indent, 100 columns,
`type *name` pointers, attached braces). Format C sources before committing:

```bash
find src tools include tests \( -name '*.c' -o -name '*.h' \) \
  -print0 | xargs -0 clang-format -i
```

The `format` CI job rejects any file that `clang-format` would change.

## Pull requests

Keep each pull request focused on a single change. Add or update tests for any
behavior change, update `CHANGELOG.md` under an `## Unreleased` heading, and make
sure all CI jobs pass. Describe what the change does and why in the pull request
body.

## License

By contributing, you agree that your contributions are licensed under the
Apache-2.0 license that covers this project.
