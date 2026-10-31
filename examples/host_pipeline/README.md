## Host pipeline example

Walks a single Xtensa C function from source to a loaded, callable overlay on
the host. It is both a readable tour of the toolchain and the `fixtures` CI job:
`make check` fails if the current tools drift from the committed test vectors.

### The pipeline

```
src/fixture.c            C source (overlay_add / overlay_sub)
   | xtensa-esp32s3-elf-gcc
fixture.o                ELF32 Xtensa object
   | link (-Ttext, -e overlay_add)
fixture.elf              positioned executable image
   | elf_overlay_relink
fixture.relk             intermediate: .text body + patch sites (RELK magic)
   | elf_overlay_emit
fixture.elfo             packed overlay blob (ELFO v1, ISA-tagged)
   | elf_overlay_load + enter/leave   (exercised by tests/host/test_fixture.c)
runtime
```

The `.elf`, `.o`, `.relk`, and `.elfo` files are committed as deterministic test
vectors so the suite can run without a cross toolchain.

### Building it yourself

This needs `xtensa-esp32s3-elf-gcc` on `PATH` and the host tools built. From the
repository root, run `cmake -S . -B build && cmake --build build`, then put
`build/` on `PATH`.

```bash
make           # src -> .o -> .elf -> .relk -> .elfo
make blob      # re-run only relink and emit from the existing .elf
make check     # regenerate and compare against the committed vectors
make clean     # remove generated files
```

The entry symbol (`overlay_add`) and the `.text` load address (`0x40000000`) are
set at the top of the `Makefile`.
