#pragma once
#include <stdint.h>

struct mag_sample {
    // Magnetic field in microtesla *1000 (so 42.000 µT => 42000)
    uint32_t x_uT_milli;
    uint32_t y_uT_milli;
    uint32_t z_uT_milli;

    // Temperature in degC *10 (so 24.5 C => 245)
    int16_t  temp_c_times10;

    // (Optional future: raw 18-bit counts if we ever want to debug calibration)
    uint32_t raw_x_counts;
    uint32_t raw_y_counts;
    uint32_t raw_z_counts;
};

// Called by main.
// Fills out with "realistic" data for now.
// Later we replace the internals with actual I2C to MMC5983MA.
void mag_read(struct mag_sample *out);
