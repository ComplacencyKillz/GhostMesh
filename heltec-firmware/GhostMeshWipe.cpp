#include "GhostMeshWipe.h"
#include "configuration.h"
#include <Arduino.h>

#ifdef ARCH_ESP32
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs_flash.h"
#endif

// The complete wipe. Ordering is chosen so the board still ends up "blank in download mode" even if
// we fault or a watchdog fires partway through — because the very first thing erased is the
// bootloader + partition table, after which ANY reset drops the ROM into USB download mode.
void ghostmesh_complete_wipe()
{
#ifdef ARCH_ESP32
    LOG_WARN("GhostMesh: COMPLETE WIPE - erasing NVS + filesystem + FIRMWARE. Board -> USB download "
             "mode; reflash firmware.factory.bin to recover.");
    delay(300); // let the log line flush over serial before the flash disappears

    esp_flash_t *chip = esp_flash_default_chip;
    const esp_partition_t *running = esp_ota_get_running_partition();

    // 1. Bootloader (0x0) + partition table (0x8000). Do this FIRST: once it's gone, every possible
    //    exit from here (our own reset, a cache-miss fault, a watchdog during the long app erase)
    //    lands the chip in ROM download mode. That makes the wipe fail-safe, not fail-open.
    esp_flash_erase_region(chip, 0x0, 0x9000);

    // 2. Every DATA partition — NVS (device key, persistent vars, BLE bonds), otadata, the LittleFS
    //    volume holding config + channel PSKs, coredump, etc. (nvs_flash_erase() too, for good measure.)
    nvs_flash_erase();
    for (esp_partition_iterator_t it =
             esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NULL);
         it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        esp_flash_erase_region(chip, p->address, p->size);
    }

    // 3. Every APP partition EXCEPT the one we're executing from.
    for (esp_partition_iterator_t it =
             esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
         it != NULL; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p != running)
            esp_flash_erase_region(chip, p->address, p->size);
    }

    // 4. ...then the running app partition LAST. We execute from it via the flash cache, so the
    //    instruction fetch after this erase returns faults -> reset -> download mode. No return.
    if (running)
        esp_flash_erase_region(chip, running->address, running->size);

    esp_restart(); // in case the fault somehow doesn't reset us
#endif
}
