/*
 * Memfault Platform Configuration
 * SPDX-License-Identifier: Apache-2.0
 *
 * Custom Memfault configuration for MagNav/Misogate project
 */

#ifndef MEMFAULT_PLATFORM_CONFIG_H
#define MEMFAULT_PLATFORM_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Device identification is handled by Kconfig:
 * - CONFIG_MEMFAULT_NCS_DEVICE_ID_RUNTIME uses HW ID
 * - CONFIG_MEMFAULT_NCS_FW_TYPE and CONFIG_MEMFAULT_NCS_FW_VERSION set firmware
 * info
 */

/* Enable GNU Build ID for better symbol file matching */
#define MEMFAULT_USE_GNU_BUILD_ID 1

/* Coredump configuration - capture important RAM regions */
#define MEMFAULT_COREDUMP_COLLECT_LOG_REGIONS 1
#define MEMFAULT_COREDUMP_COLLECT_HEAP_STATS 1

/* Event storage size - larger for more buffered events */
#define MEMFAULT_EVENT_STORAGE_SIZE 1024

/* Log buffer size for captured logs */
#define MEMFAULT_LOG_EXPORT_CHUNK_MAX_LEN 256

#ifdef __cplusplus
}
#endif

#endif /* MEMFAULT_PLATFORM_CONFIG_H */
