#include "vl53l1_platform.h"
#include "vl53l1_api.h"
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "esp_bit_defs.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"

/* I2C communication related */
#define I2C_TIMEOUT_MS          (1000)
#define VL53L1X_SDA             (GPIO_NUM_12)
#define VL53L1X_SCL             (GPIO_NUM_10)
#define VL53L1X_PORT            (I2C_NUM_1)

#define VL53L1_ESP_ERROR_MALLOC		(VL53L1_ERROR_PLATFORM_SPECIFIC_START - 0)

static char *TAG = "vl53l1_i2c";
static i2c_master_bus_handle_t i2c_handle;

VL53L1_Error VL53L1_CommsInitialise(VL53L1_Dev_t *pdev, uint8_t comms_type, uint16_t comms_speed_khz) 
{
	// Initialize new i2c master bus
    i2c_master_bus_config_t i2c_config = {
        .i2c_port = VL53L1X_PORT,
        .sda_io_num = VL53L1X_SDA,
        .scl_io_num = VL53L1X_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
		.flags.enable_internal_pullup = true,
    };

    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_handle));

	// Add new I2C device
    esp_err_t ret = ESP_OK;
    const i2c_device_config_t i2c_dev_cfg = {
		.device_address = pdev->I2cDevAddr,
        .scl_speed_hz = ((uint32_t)comms_speed_khz * 1000),
    };
	pdev->comms_speed_khz = comms_speed_khz;
	pdev->comms_type = comms_type;
	pdev->timeout_ms = I2C_TIMEOUT_MS;

	i2c_master_dev_handle_t* esp_i2c_handle = (i2c_master_dev_handle_t *)calloc(1, sizeof(i2c_master_dev_handle_t));
	if (esp_i2c_handle == NULL) {
		ESP_LOGE(TAG, "Malloc failed");
		return VL53L1_ESP_ERROR_MALLOC;
	}

    ESP_GOTO_ON_ERROR(i2c_master_bus_add_device(i2c_handle, &i2c_dev_cfg, esp_i2c_handle), err, TAG,
                      "Add new I2C device failed");

	pdev->I2cHandle = (void *)esp_i2c_handle;

    return VL53L1_ERROR_NONE;
err:
    i2c_master_bus_rm_device(*esp_i2c_handle);
	free(esp_i2c_handle);
    return VL53L1_ERROR_CONTROL_INTERFACE;
}

VL53L1_Error VL53L1_CommsClose(VL53L1_Dev_t *pdev) 
{
	i2c_master_dev_handle_t* esp_i2c_handle = (i2c_master_dev_handle_t*)pdev->I2cHandle;
	ESP_ERROR_CHECK(i2c_master_bus_rm_device(*esp_i2c_handle));
	free(esp_i2c_handle);
	return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_WriteMulti(VL53L1_DEV Dev, uint16_t index, uint8_t *pdata, uint32_t count) 
{
	i2c_master_dev_handle_t* esp_i2c_handle = (i2c_master_dev_handle_t*)Dev->I2cHandle;
	uint8_t* i2c_data = (uint8_t*)malloc(count + sizeof(index));
	
	if (i2c_data == NULL) {
		ESP_LOGE(TAG, "Malloc failed");
		return VL53L1_ESP_ERROR_MALLOC;
	}

	VL53L1_Error err = VL53L1_ERROR_NONE;
	i2c_data[0] = ((index & 0xFF00) >> 8);
	i2c_data[1] = ((index & 0x00FF) >> 0);
	memcpy(&i2c_data[2], pdata, count);

	if (i2c_master_transmit(*esp_i2c_handle, i2c_data, count + sizeof(index), Dev->timeout_ms) != ESP_OK) {
		ESP_LOGE(TAG, "Write register failed");
		err = VL53L1_ERROR_CONTROL_INTERFACE;
	}

	free(i2c_data);

	return err;
}

VL53L1_Error VL53L1_ReadMulti(VL53L1_DEV Dev, uint16_t index, uint8_t *pdata, uint32_t count) 
{
	i2c_master_dev_handle_t* esp_i2c_handle = (i2c_master_dev_handle_t*)Dev->I2cHandle;
	uint8_t i2c_reg_data[] = { ((index & 0xFF00) >> 8), ((index & 0x00FF) >> 0) };
	esp_err_t ret = ESP_OK;

	ESP_GOTO_ON_ERROR(i2c_master_transmit_receive(*esp_i2c_handle, 
					  i2c_reg_data, sizeof(i2c_reg_data), 
					  pdata, count, Dev->timeout_ms), err, TAG, "Read register failed");

	return VL53L1_ERROR_NONE;
err:
	return VL53L1_ERROR_CONTROL_INTERFACE;
}

VL53L1_Error VL53L1_WrByte(VL53L1_DEV Dev, uint16_t index, uint8_t data) 
{
	return VL53L1_WriteMulti(Dev, index, &data, 1);
}

VL53L1_Error VL53L1_WrWord(VL53L1_DEV Dev, uint16_t index, uint16_t data) 
{
	uint8_t write_data[] = { ((data & 0xFF00) >> 8), ((data & 0x00FF) >> 0) };
	return VL53L1_WriteMulti(Dev, index, write_data, 2);
}

VL53L1_Error VL53L1_WrDWord(VL53L1_DEV Dev, uint16_t index, uint32_t data) 
{
	uint8_t write_data[] = { ((data & 0xFF000000) >> 24), ((data & 0x00FF0000) >> 16),((data & 0x0000FF00) >> 8), ((data & 0x000000FF) >> 0) };
	return VL53L1_WriteMulti(Dev, index, write_data, 4);
}

VL53L1_Error VL53L1_UpdateByte(VL53L1_DEV Dev, uint16_t index, uint8_t AndData, uint8_t OrData) 
{
	return VL53L1_ERROR_NOT_IMPLEMENTED;
}

VL53L1_Error VL53L1_RdByte(VL53L1_DEV Dev, uint16_t index, uint8_t *data) 
{
	return VL53L1_ReadMulti(Dev, index, data, 1);
}

VL53L1_Error VL53L1_RdWord(VL53L1_DEV Dev, uint16_t index, uint16_t *data) 
{
	VL53L1_Error err;
	uint8_t read_data[] = { 0, 0 };
	err = VL53L1_ReadMulti(Dev, index, read_data, 2);
	if (err == VL53L1_ERROR_NONE) {
		*data = (((uint16_t)read_data[0] << 8) | ((uint16_t)read_data[1] << 0));
	}
	return err;
}

VL53L1_Error VL53L1_RdDWord(VL53L1_DEV Dev, uint16_t index, uint32_t *data) 
{
	VL53L1_Error err;
	uint8_t read_data[] = { 0, 0, 0, 0 };
	err = VL53L1_ReadMulti(Dev, index, read_data, 4);
	if (err == VL53L1_ERROR_NONE) {
		*data = (((uint16_t)read_data[0] << 24) | ((uint16_t)read_data[1] << 16) | 
				 ((uint16_t)read_data[2] <<  8) | ((uint16_t)read_data[3] <<  0));
	}
	return err;
}

VL53L1_Error VL53L1_GetTickCount(uint32_t *ptick_count_ms)
{
	*ptick_count_ms = esp_timer_get_time() / 1000;
	return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_GetTimerFrequency(int32_t *ptimer_freq_hz)
{
	VL53L1_Error status  = VL53L1_ERROR_NONE;
	return status;
}

VL53L1_Error VL53L1_WaitMs(VL53L1_Dev_t *pdev, int32_t wait_ms) 
{
	usleep(wait_ms * 1000);
	return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_WaitUs(VL53L1_Dev_t *pdev, int32_t wait_us) 
{
	usleep(wait_us);
	return VL53L1_ERROR_NONE;
}

VL53L1_Error VL53L1_WaitValueMaskEx(
	VL53L1_Dev_t *pdev,
	uint32_t      timeout_ms,
	uint16_t      index,
	uint8_t       value,
	uint8_t       mask,
	uint32_t      poll_delay_ms)
{
	uint8_t  register_value = 0;

	VL53L1_Error status  = VL53L1_ERROR_NONE;

	int32_t attempts = timeout_ms / poll_delay_ms;

	for(int32_t x = 0; x < attempts; x++){
		status = VL53L1_RdByte(
					pdev,
					index,
					&register_value);
		if (status == VL53L1_ERROR_NONE && (register_value & mask) == value) {
			return VL53L1_ERROR_NONE;
		}
		usleep(poll_delay_ms * 1000);
	}

	return VL53L1_ERROR_TIME_OUT;
}
