#ifndef FLASH_H
#define FLASH_H

#include <stddef.h>
#include <stdint.h>

// Store a measurement struct into internal flash (16kB block)
// Returns 0 on success, -1 on failure.
int flash_write_measurement(const uint8_t *data, size_t len, uint32_t offset);
int flash_read_measurement(uint8_t *data, size_t len, uint32_t offset);

#endif
