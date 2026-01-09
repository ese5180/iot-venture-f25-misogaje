#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mmc5983ma, LOG_LEVEL_INF);

#define MMC5983MA_ADDR 0x30

// 寄存器定义
#define MMC5983MA_REG_XOUT0 0x00
#define MMC5983MA_REG_XOUT1 0x01
#define MMC5983MA_REG_YOUT0 0x02
#define MMC5983MA_REG_YOUT1 0x03
#define MMC5983MA_REG_ZOUT0 0x04
#define MMC5983MA_REG_ZOUT1 0x05
#define MMC5983MA_REG_XYZOUT2 0x06
#define MMC5983MA_REG_TOUT 0x07
#define MMC5983MA_REG_STATUS 0x08
#define MMC5983MA_REG_CTRL0 0x09
#define MMC5983MA_REG_CTRL1 0x0A
#define MMC5983MA_REG_CTRL2 0x0B
#define MMC5983MA_REG_PRODUCT_ID 0x2F

// 控制寄存器位定义
#define MMC5983MA_CTRL0_TM 0x01       // 启动单次测量
#define MMC5983MA_CTRL0_SET 0x08      // SET操作
#define MMC5983MA_CTRL0_RESET 0x10    // RESET操作
#define MMC5983MA_CTRL1_BW_100HZ 0x00 // 带宽100Hz
#define MMC5983MA_CTRL2_CMM_EN 0x10   // 连续测量模式

// 初始化传感器
int mmc5983ma_init(const struct device *i2c_dev) {
  uint8_t product_id;
  uint8_t reg = MMC5983MA_REG_PRODUCT_ID;

  // 读取Product ID验证通信
  int ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, &product_id, 1);
  if (ret != 0) {
    LOG_ERR("Failed to read Product ID");
    return ret;
  }

  if (product_id != 0x30) {
    LOG_ERR("Invalid Product ID: 0x%02X", product_id);
    return -EIO;
  }

  LOG_INF("MMC5983MA detected, Product ID: 0x%02X", product_id);

  // 执行SET操作（校准）
  uint8_t cmd[2] = {MMC5983MA_REG_CTRL0, MMC5983MA_CTRL0_SET};
  ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
  if (ret != 0) {
    LOG_ERR("Failed to send SET command");
    return ret;
  }
  k_msleep(1);

  // 配置带宽
  cmd[0] = MMC5983MA_REG_CTRL1;
  cmd[1] = MMC5983MA_CTRL1_BW_100HZ;
  ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
  if (ret != 0) {
    LOG_ERR("Failed to configure bandwidth");
    return ret;
  }

  LOG_INF("MMC5983MA initialized successfully");
  return 0;
}

// 读取磁场数据
int mmc5983ma_read_mag(const struct device *i2c_dev, int32_t *x, int32_t *y,
                       int32_t *z) {
  uint8_t data[7];
  uint8_t reg = MMC5983MA_REG_XOUT0;

  // 启动单次测量
  uint8_t cmd[2] = {MMC5983MA_REG_CTRL0, MMC5983MA_CTRL0_TM};
  int ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
  if (ret != 0) {
    return ret;
  }

  // 等待测量完成（约8ms）
  k_msleep(10);

  // 读取7个字节数据（X0, X1, Y0, Y1, Z0, Z1, XYZ2）
  ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, data, 7);
  if (ret != 0) {
    return ret;
  }

  // 组合18位数据
  // X = X_OUT[17:0] = {XOUT1[7:0], XOUT0[7:0], XYZOUT2[7:6]}
  *x = ((uint32_t)data[0] << 10) | ((uint32_t)data[1] << 2) |
       ((data[6] >> 6) & 0x03);
  *y = ((uint32_t)data[2] << 10) | ((uint32_t)data[3] << 2) |
       ((data[6] >> 4) & 0x03);
  *z = ((uint32_t)data[4] << 10) | ((uint32_t)data[5] << 2) |
       ((data[6] >> 2) & 0x03);

  // 转换为有符号值（以131072为中心）
  *x -= 131072;
  *y -= 131072;
  *z -= 131072;

  return 0;
}

void main(void) {
  LOG_INF("=== MMC5983MA Magnetometer Test ===");

  const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
  if (!device_is_ready(i2c_dev)) {
    LOG_ERR("I2C device not ready");
    return;
  }

  // 初始化传感器
  if (mmc5983ma_init(i2c_dev) != 0) {
    LOG_ERR("Failed to initialize MMC5983MA");
    return;
  }

  // 主循环：读取磁场数据
  while (1) {
    int32_t x, y, z;

    if (mmc5983ma_read_mag(i2c_dev, &x, &y, &z) == 0) {
      // 转换为高斯（Gauss）
      // MMC5983MA: 1 LSB = 0.0625 mG = 0.0000625 G
      float x_gauss = x * 0.0000625f;
      float y_gauss = y * 0.0000625f;
      float z_gauss = z * 0.0000625f;

      LOG_INF("Mag [G]: X=%.4f, Y=%.4f, Z=%.4f", (double)x_gauss,
              (double)y_gauss, (double)z_gauss);

      // 计算磁场强度
      float magnitude =
          sqrtf(x_gauss * x_gauss + y_gauss * y_gauss + z_gauss * z_gauss);
      LOG_INF("Magnitude: %.4f G", (double)magnitude);
    } else {
      LOG_ERR("Failed to read magnetometer data");
    }

    k_msleep(1000); // 每秒读取一次
  }
}

// #include <zephyr/drivers/i2c.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/kernel.h>

// LOG_MODULE_REGISTER(i2c_test, LOG_LEVEL_INF);

// #define MMC5983MA_ADDR          0x30
// #define MMC5983MA_REG_PRODUCT_ID 0x2F

// void main(void)
// {
//     printk("=== Booting I2C test ===\n");

//     const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
//     if (!device_is_ready(i2c_dev)) {
//         LOG_ERR("I2C device not ready");
//         return;
//     }

//     uint8_t reg = MMC5983MA_REG_PRODUCT_ID;
//     uint8_t id = 0;

//     while (1) {
//         int ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR,
//                                  &reg, 1, &id, 1);

//         if (ret == 0) {
//             LOG_INF("MMC5983MA Product ID: 0x%02X", id);
//         } else {
//             LOG_ERR("I2C communication failed, ret=%d", ret);
//         }

//         k_msleep(500);
//     }
// }

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

// #include <zephyr/kernel.h>
// #include <zephyr/drivers/i2c.h>
// #include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(i2c_scan, LOG_LEVEL_INF);

// void main(void)
// {
//     const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
//     if (!device_is_ready(i2c_dev)) {
//         printk("I2C1 not ready!\n");
//         return;
//     }

// 	k_msleep(20);

//     printk("=== Scanning I2C Bus ===\n");

//     // for (uint8_t addr = 1; addr < 0x7F; addr++) {
//     //     uint8_t reg = 0;
//     //     if (i2c_write_read(i2c_dev, addr, &reg, 1, NULL, 0) == 0) {
//     //         printk("Found device at 0x%02X\n", addr);
//     //     }
//     //     k_msleep(10);
//     // }

// 	for (uint8_t addr = 1; addr < 0x7F; addr++) {
//     uint8_t reg = 0;
//     int ret = i2c_write_read(i2c_dev, addr, &reg, 1, NULL, 0);
//     if (ret == 0) {
//         printk("✅ Found device at 0x%02X\n", addr);
//     } else {
//         // Uncomment this line to debug
//         printk("Addr 0x%02X no ack (%d)\n", addr, ret);
//     }
//     k_msleep(10);
// }

//     printk("=== Scan Done ===\n");
// }

// #include <zephyr/drivers/i2c.h>
// #include <zephyr/logging/log.h>
// LOG_MODULE_REGISTER(i2c_scan, LOG_LEVEL_INF);

// void main(void)
// {
//     const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
//     if (!device_is_ready(i2c_dev)) {
//         LOG_ERR("I2C device not ready");
//         return;
//     }

//     LOG_INF("Scanning I2C bus...");
//     for (uint8_t addr = 1; addr < 0x7F; addr++) {
//         uint8_t dummy;
//         int ret = i2c_read(i2c_dev, &dummy, 1, addr);
//         if (ret == 0) {
//             LOG_INF("Found device at address 0x%02X", addr);
//         }
//     }
// }
