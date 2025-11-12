// #include <zephyr/drivers/i2c.h>
// #include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(i2c_test, LOG_LEVEL_INF);

// #define MMC5983MA_ADDR 0x30
// #define MMC5983MA_REG_PRODUCT_ID 0x2F

// void main(void)
// {
// 	printk("=== Booting I2C test ===\n");

//     const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
//     if (!device_is_ready(i2c_dev)) {
//         LOG_ERR("I2C device not ready");
//         return;
//     }

//     uint8_t reg = MMC5983MA_REG_PRODUCT_ID;
//     uint8_t id = 0;

//     if (i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, &id, 1) == 0) {
//         LOG_INF("MMC5983MA Product ID: 0x%02X", id);
//     } else {
//         LOG_ERR("I2C communication failed");
//     }
// }

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(i2c_scan, LOG_LEVEL_INF);

void main(void)
{
    const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    if (!device_is_ready(i2c_dev)) {
        printk("I2C1 not ready!\n");
        return;
    }

	k_msleep(20);

    printk("=== Scanning I2C Bus ===\n");

    // for (uint8_t addr = 1; addr < 0x7F; addr++) {
    //     uint8_t reg = 0;
    //     if (i2c_write_read(i2c_dev, addr, &reg, 1, NULL, 0) == 0) {
    //         printk("Found device at 0x%02X\n", addr);
    //     }
    //     k_msleep(10);
    // }


	for (uint8_t addr = 1; addr < 0x7F; addr++) {
    uint8_t reg = 0;
    int ret = i2c_write_read(i2c_dev, addr, &reg, 1, NULL, 0);
    if (ret == 0) {
        printk("✅ Found device at 0x%02X\n", addr);
    } else {
        // Uncomment this line to debug
         printk("Addr 0x%02X no ack (%d)\n", addr, ret);
    }
    k_msleep(10);
}

    printk("=== Scan Done ===\n");
}

