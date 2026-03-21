#include <stdio.h>
#include <cstring>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"

#include "board_i2c.h"

#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"

static const char *TAG = "AXP2101";

static XPowersPMU power;
static i2c_master_dev_handle_t i2c_device;

static esp_err_t pmic_i2c_init(i2c_master_bus_handle_t bus_handle)
{
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = 0x34;
    cfg.scl_speed_hz = 400 * 1000;
    return i2c_master_bus_add_device(bus_handle, &cfg, &i2c_device);
}

static int pmu_register_read(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (len == 0) return 0;
    if (data == NULL) return -1;

    esp_err_t ret = ESP_FAIL;
    if (board_i2c_lock(0)) {
        ret = i2c_master_transmit_receive(i2c_device, (const uint8_t *)&regAddr, 1, data, len, pdMS_TO_TICKS(1000));
        board_i2c_unlock();
    }
    return (ret == ESP_OK) ? 0 : -1;
}

static int pmu_register_write_byte(uint8_t devAddr, uint8_t regAddr, uint8_t *data, uint8_t len)
{
    if (data == NULL) return ESP_FAIL;

    uint8_t *write_buffer = (uint8_t *)malloc(sizeof(uint8_t) * (len + 1));
    if (!write_buffer) return -1;

    write_buffer[0] = regAddr;
    memcpy(write_buffer + 1, data, len);

    esp_err_t ret = i2c_master_transmit(i2c_device, write_buffer, len + 1, -1);
    free(write_buffer);
    return ret == ESP_OK ? 0 : -1;
}

extern "C" esp_err_t board_pmic_init(i2c_master_bus_handle_t bus_handle)
{
    pmic_i2c_init(bus_handle);

    ESP_LOGI(TAG, "Implemented using read and write callback methods");
    if (power.begin(AXP2101_SLAVE_ADDRESS, pmu_register_read, pmu_register_write_byte)) {
        ESP_LOGI(TAG, "Init PMU SUCCESS!");
    } else {
        ESP_LOGE(TAG, "Init PMU FAILED!");
        return ESP_FAIL;
    }

    printf("getID:0x%x\n", power.getChipID());

    power.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    power.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);

    uint16_t vol = power.getSysPowerDownVoltage();
    printf("->  getSysPowerDownVoltage:%u\n", vol);
    power.setSysPowerDownVoltage(2600);
    vol = power.getSysPowerDownVoltage();
    printf("->  getSysPowerDownVoltage:%u\n", vol);

    power.setDC1Voltage(3300);
    printf("DC1  : %s   Voltage:%u mV \n", power.isEnableDC1() ? "+" : "-", power.getDC1Voltage());
    power.setDC2Voltage(1000);
    printf("DC2  : %s   Voltage:%u mV \n", power.isEnableDC2() ? "+" : "-", power.getDC2Voltage());
    power.setDC3Voltage(3300);
    printf("DC3  : %s   Voltage:%u mV \n", power.isEnableDC3() ? "+" : "-", power.getDC3Voltage());
    power.setDC4Voltage(1000);
    printf("DC4  : %s   Voltage:%u mV \n", power.isEnableDC4() ? "+" : "-", power.getDC4Voltage());
    power.setDC5Voltage(3300);
    printf("DC5  : %s   Voltage:%u mV \n", power.isEnableDC5() ? "+" : "-", power.getDC5Voltage());

    power.setALDO1Voltage(3300);
    power.setALDO2Voltage(3300);
    power.setALDO3Voltage(3300);
    power.setALDO4Voltage(3300);
    power.setBLDO1Voltage(1500);
    power.setBLDO2Voltage(2800);
    power.setCPUSLDOVoltage(1000);
    power.setDLDO1Voltage(3300);
    power.setDLDO2Voltage(3300);

    power.enableDC2();
    power.enableDC3();
    power.enableDC4();
    power.enableDC5();
    power.enableALDO1();
    power.enableALDO2();
    power.enableALDO3();
    power.enableALDO4();
    power.enableBLDO1();
    power.enableBLDO2();
    power.enableCPUSLDO();
    power.enableDLDO1();
    power.enableDLDO2();

    printf("DCDC=======================================================================\n");
    printf("DC1  : %s   Voltage:%u mV \n", power.isEnableDC1() ? "+" : "-", power.getDC1Voltage());
    printf("DC2  : %s   Voltage:%u mV \n", power.isEnableDC2() ? "+" : "-", power.getDC2Voltage());
    printf("DC3  : %s   Voltage:%u mV \n", power.isEnableDC3() ? "+" : "-", power.getDC3Voltage());
    printf("DC4  : %s   Voltage:%u mV \n", power.isEnableDC4() ? "+" : "-", power.getDC4Voltage());
    printf("DC5  : %s   Voltage:%u mV \n", power.isEnableDC5() ? "+" : "-", power.getDC5Voltage());
    printf("ALDO=======================================================================\n");
    printf("ALDO1: %s   Voltage:%u mV\n", power.isEnableALDO1() ? "+" : "-", power.getALDO1Voltage());
    printf("ALDO2: %s   Voltage:%u mV\n", power.isEnableALDO2() ? "+" : "-", power.getALDO2Voltage());
    printf("ALDO3: %s   Voltage:%u mV\n", power.isEnableALDO3() ? "+" : "-", power.getALDO3Voltage());
    printf("ALDO4: %s   Voltage:%u mV\n", power.isEnableALDO4() ? "+" : "-", power.getALDO4Voltage());
    printf("BLDO=======================================================================\n");
    printf("BLDO1: %s   Voltage:%u mV\n", power.isEnableBLDO1() ? "+" : "-", power.getBLDO1Voltage());
    printf("BLDO2: %s   Voltage:%u mV\n", power.isEnableBLDO2() ? "+" : "-", power.getBLDO2Voltage());
    printf("CPUSLDO====================================================================\n");
    printf("CPUSLDO: %s Voltage:%u mV\n", power.isEnableCPUSLDO() ? "+" : "-", power.getCPUSLDOVoltage());
    printf("DLDO=======================================================================\n");
    printf("DLDO1: %s   Voltage:%u mV\n", power.isEnableDLDO1() ? "+" : "-", power.getDLDO1Voltage());
    printf("DLDO2: %s   Voltage:%u mV\n", power.isEnableDLDO2() ? "+" : "-", power.getDLDO2Voltage());
    printf("===========================================================================\n");

    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    uint8_t opt = power.getPowerKeyPressOffTime();
    printf("PowerKeyPressOffTime:");
    switch (opt) {
    case XPOWERS_POWEROFF_4S:  printf("4 Second\n"); break;
    case XPOWERS_POWEROFF_6S:  printf("6 Second\n"); break;
    case XPOWERS_POWEROFF_8S:  printf("8 Second\n"); break;
    case XPOWERS_POWEROFF_10S: printf("10 Second\n"); break;
    default: break;
    }

    power.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    opt = power.getPowerKeyPressOnTime();
    printf("PowerKeyPressOnTime:");
    switch (opt) {
    case XPOWERS_POWERON_128MS: printf("128 Ms\n"); break;
    case XPOWERS_POWERON_512MS: printf("512 Ms\n"); break;
    case XPOWERS_POWERON_1S:    printf("1 Second\n"); break;
    case XPOWERS_POWERON_2S:    printf("2 Second\n"); break;
    default: break;
    }

    printf("===========================================================================\n");

    bool en;
    en = power.getDCHighVoltagePowerDownEn();
    printf("getDCHighVoltagePowerDownEn:%s\n", en ? "ENABLE" : "DISABLE");
    en = power.getDC1LowVoltagePowerDownEn();
    printf("getDC1LowVoltagePowerDownEn:%s\n", en ? "ENABLE" : "DISABLE");
    en = power.getDC2LowVoltagePowerDownEn();
    printf("getDC2LowVoltagePowerDownEn:%s\n", en ? "ENABLE" : "DISABLE");
    en = power.getDC3LowVoltagePowerDownEn();
    printf("getDC3LowVoltagePowerDownEn:%s\n", en ? "ENABLE" : "DISABLE");
    en = power.getDC4LowVoltagePowerDownEn();
    printf("getDC4LowVoltagePowerDownEn:%s\n", en ? "ENABLE" : "DISABLE");
    en = power.getDC5LowVoltagePowerDownEn();
    printf("getDC5LowVoltagePowerDownEn:%s\n", en ? "ENABLE" : "DISABLE");

    power.disableTSPinMeasure();

    power.enableBattDetection();
    power.enableVbusVoltageMeasure();
    power.enableBattVoltageMeasure();
    power.enableSystemVoltageMeasure();

    power.setChargingLedMode(XPOWERS_CHG_LED_OFF);

    power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power.clearIrqStatus();
    power.enableIRQ(
        XPOWERS_AXP2101_BAT_INSERT_IRQ | XPOWERS_AXP2101_BAT_REMOVE_IRQ |
        XPOWERS_AXP2101_VBUS_INSERT_IRQ | XPOWERS_AXP2101_VBUS_REMOVE_IRQ |
        XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |
        XPOWERS_AXP2101_BAT_CHG_DONE_IRQ | XPOWERS_AXP2101_BAT_CHG_START_IRQ
    );

    power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_50MA);
    power.setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_200MA);
    power.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);
    power.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V1);

    power.setWatchdogConfig(XPOWERS_AXP2101_WDT_IRQ_TO_PIN);
    power.setWatchdogTimeout(XPOWERS_AXP2101_WDT_TIMEOUT_4S);
    power.enableWatchdog();

    power.enableButtonBatteryCharge();
    power.setButtonBatteryChargeVoltage(3300);

    return ESP_OK;
}

extern "C" void board_pmic_isr_handler(void)
{
    power.getIrqStatus();

    if (power.isDropWarningLevel2Irq())         ESP_LOGI(TAG, "isDropWarningLevel2");
    if (power.isDropWarningLevel1Irq())         ESP_LOGI(TAG, "isDropWarningLevel1");
    if (power.isGaugeWdtTimeoutIrq())           ESP_LOGI(TAG, "isWdtTimeout");
    if (power.isBatChargerOverTemperatureIrq())  ESP_LOGI(TAG, "isBatChargeOverTemperature");
    if (power.isBatWorkOverTemperatureIrq())     ESP_LOGI(TAG, "isBatWorkOverTemperature");
    if (power.isBatWorkUnderTemperatureIrq())    ESP_LOGI(TAG, "isBatWorkUnderTemperature");
    if (power.isVbusInsertIrq())                ESP_LOGI(TAG, "isVbusInsert");
    if (power.isVbusRemoveIrq())                ESP_LOGI(TAG, "isVbusRemove");
    if (power.isBatInsertIrq())                 ESP_LOGI(TAG, "isBatInsert");
    if (power.isBatRemoveIrq())                 ESP_LOGI(TAG, "isBatRemove");
    if (power.isPekeyShortPressIrq())           ESP_LOGI(TAG, "isPekeyShortPress");
    if (power.isPekeyLongPressIrq())            ESP_LOGI(TAG, "isPekeyLongPress");
    if (power.isPekeyNegativeIrq())             ESP_LOGI(TAG, "isPekeyNegative");
    if (power.isPekeyPositiveIrq())             ESP_LOGI(TAG, "isPekeyPositive");
    if (power.isWdtExpireIrq())                 ESP_LOGI(TAG, "isWdtExpire");
    if (power.isLdoOverCurrentIrq())            ESP_LOGI(TAG, "isLdoOverCurrentIrq");
    if (power.isBatfetOverCurrentIrq())         ESP_LOGI(TAG, "isBatfetOverCurrentIrq");
    if (power.isBatChargeDoneIrq())             ESP_LOGI(TAG, "isBatChargeDone");
    if (power.isBatChargeStartIrq())            ESP_LOGI(TAG, "isBatChargeStart");
    if (power.isBatDieOverTemperatureIrq())     ESP_LOGI(TAG, "isBatDieOverTemperature");
    if (power.isChargeOverTimeoutIrq())         ESP_LOGI(TAG, "isChargeOverTimeout");
    if (power.isBatOverVoltageIrq())            ESP_LOGI(TAG, "isBatOverVoltage");

    power.clearIrqStatus();
}
