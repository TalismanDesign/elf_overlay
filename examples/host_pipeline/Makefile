# Regenerate the Xtensa C test fixture and overlay blob.
#
#   make                # rebuild everything
#   make blob           # rebuild only .relk / .elfo from the existing .elf
#   make check          # fail if regenerated artifacts differ from committed

CROSS   ?= xtensa-esp32s3-elf-
CC      := $(CROSS)gcc
RELINK  ?= elf_overlay_relink
EMIT    ?= elf_overlay_emit

TEXT_ADDR := 0x40000000
ENTRY     := overlay_add

CFLAGS  := -Os -ffunction-sections
LDFLAGS := -nostdlib -Wl,-Ttext=$(TEXT_ADDR) -Wl,-e,$(ENTRY)

.PHONY: all blob check clean
all: fixture.elfo

fixture.o: src/fixture.c
	$(CC) -c $(CFLAGS) -o $@ $<

fixture.elf: fixture.o
	$(CC) $(LDFLAGS) -o $@ $<

blob: fixture.elfo

fixture.relk: fixture.elf
	$(RELINK) $< $@

fixture.elfo: fixture.relk
	$(EMIT) $< $@

check: fixture.elf
	$(RELINK) fixture.elf /tmp/fixture.check.relk
	$(EMIT) /tmp/fixture.check.relk /tmp/fixture.check.elfo
	cmp fixture.relk /tmp/fixture.check.relk
	cmp fixture.elfo /tmp/fixture.check.elfo
	@echo "fixture artifacts match committed vectors"

clean:
	rm -f fixture.o fixture.elf fixture.relk fixture.elfo
