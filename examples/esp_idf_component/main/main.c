#include "elf_overlay/esp_elf_integration.h"
#include "elf_overlay/overlay_loader.h"

void app_main(void) {
    elf_overlay_loader_t *loader = NULL;
    (void)elf_overlay_esp_elf_set_section_map(loader, NULL);
}
