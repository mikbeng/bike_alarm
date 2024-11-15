



// Initialize I2C for MPU6886
static esp_err_t i2c_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = 21,        // SDA pin for M5StickC
        .scl_io_num = 22,        // SCL pin for M5StickC
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000
    };
    
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) {
        return err;
    }
    
    return i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);
}

// Write to MPU6886 register
static esp_err_t mpu6886_write_reg(uint8_t reg, uint8_t data) {
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_NUM_0, MPU6886_ADDR, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}

// Read from MPU6886 register
static esp_err_t mpu6886_read_reg(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_NUM_0, MPU6886_ADDR, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

// Initialize MPU6886
static esp_err_t mpu6886_init(void) {
    esp_err_t err;

    // Wake up MPU6886
    err = mpu6886_write_reg(MPU6886_PWR_MGMT_1, 0x00);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));

    // Set accelerometer range to ±4g
    err = mpu6886_write_reg(MPU6886_ACCEL_CONFIG, 0x08);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(1));

    return ESP_OK;
}

// Read acceleration data
static void read_acceleration(float *ax, float *ay, float *az) {
    uint8_t data[6];
    if (mpu6886_read_reg(MPU6886_ACCEL_XOUT_H, data, 6) == ESP_OK) {
        int16_t raw_ax = (data[0] << 8) | data[1];
        int16_t raw_ay = (data[2] << 8) | data[3];
        int16_t raw_az = (data[4] << 8) | data[5];
        
        // Convert to g (±4g range)
        *ax = (float)raw_ax / 8192.0f;
        *ay = (float)raw_ay / 8192.0f;
        *az = (float)raw_az / 8192.0f;
    } else {
        // In case of error, set values to 0
        *ax = *ay = *az = 0.0f;
        ESP_LOGE(TAG, "Failed to read acceleration data");
    }
}