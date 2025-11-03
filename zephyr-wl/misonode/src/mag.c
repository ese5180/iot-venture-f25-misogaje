#include "mag.h"

void mag_read(struct mag_sample *out)
{
    // Pretend raw 18-bit values straight from sensor:
    // Pick numbers in range ~0..16384 for ~±0.5 G-ish.
    out->raw_x_counts = 8000;
    out->raw_y_counts = 1000;
    out->raw_z_counts = 7000;

    // Convert fake raw counts -> microtesla *1000.
    // Math we'll actually do in real code later:
    // 1 count ≈ 0.006103515625 µT (from datasheet scaling logic)
    // So uT = counts * 0.006103515625
    // uT*1000 = counts * 6.103515625
    out->x_uT_milli = (uint32_t)(out->raw_x_counts * 6.1035f); // ~48828
    out->y_uT_milli = (uint32_t)(out->raw_y_counts * 6.1035f); // ~6103
    out->z_uT_milli = (uint32_t)(out->raw_z_counts * 6.1035f); // ~42724

    // Fake temperature: 24.5 C
    out->temp_c_times10 = 245;
}
