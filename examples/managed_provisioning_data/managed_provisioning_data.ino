/**
 * This code will send a two-byte LoRaWAN message every 15 minutes. The first
 * byte is a simple 8-bit counter, the second is the ESP32 chip temperature
 * directly after waking up from its 15 minute sleep in degrees celsius + 100.
 *
 * If your NVS partition does not have stored TTN / LoRaWAN provisioning
 * information in it yet, you will be prompted for them on the serial port and
 * they will be stored for subsequent use.
 *
 * See https://github.com/ropg/LoRaWAN_ESP32
*/


// Pause between sends in seconds, so this is every 15 minutes. (Delay will be
// longer if regulatory or TTN Fair Use Policy requires it.)
#define MINIMUM_DELAY 900 


#include <RadioLib.h>
#include <LoRaWAN_ESP32.h>

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #include "driver/temperature_sensor.h"
#else
  #include "driver/temp_sensor.h"
#endif

// These are the radio and pins for my heltec v3 boards
// Yours might well be different.
SX1262 radio = new Module(8, 14, 12, 13);

LoRaWANNode* node;

RTC_DATA_ATTR uint8_t count = 0;

void setup() {
  Serial.begin(115200);

  // Obtain directly after deep sleep
  // May or may not reflect room temperature, sort of. 
  float temp = ESP32_temp();
  Serial.printf("Temperature: %.2f °C\n", temp);

  // initialize radio
  Serial.println("Radio init");
  int16_t state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.println("Radio did not initialize. We'll try again later.");
    goToSleep();
  }

  node = persist.manage(&radio);

  if (!node->isActivated()) {
    Serial.println("Could not join network. We'll try again later.");
    goToSleep();
  }

  // If we're still here, it means we joined, and we can send something


  // Manages uplink intervals to the TTN Fair Use Policy
  node->setDutyCycle(true, 1250);

  uint8_t uplinkData[2];
  uplinkData[0] = count++;
  uplinkData[1] = temp + 100;

  uint8_t downlinkData[256];
  size_t lenDown = sizeof(downlinkData);

  state = node->sendReceive(uplinkData, sizeof(uplinkData), 1, downlinkData, &lenDown);

  if(state == RADIOLIB_ERR_NONE || state == RADIOLIB_LORAWAN_NO_DOWNLINK) {
    Serial.println("Message sent");
  } else {
    Serial.printf("sendReceive returned error %d, we'll try again later.\n", state);
  }

  goToSleep();    // Does not return, program starts over next round

}

void loop() {
  // This is never called. There is no repetition: we always go back to
  // deep sleep one way or the other at the end of setup()
}

void goToSleep() {
  Serial.println("Going to deep sleep now");
  // allows recall of the session after deepsleep
  persist.saveSession(node);
  // Calculate minimum duty cycle delay (per FUP & law!)
  uint32_t interval = node->timeUntilUplink();
  // And then pick it or our MINIMUM_DELAY, whichever is greater
  uint32_t delayMs = max(interval, (uint32_t)MINIMUM_DELAY * 1000);
  Serial.printf("Next TX in %i s\n", delayMs/1000);
  delay(100);  // So message prints

  esp_sleep_enable_timer_wakeup((int64_t)delayMs * 1000);

  // INSERT WHATEVER ELSE YOU NEED TO DO TO MINIMIZE POWER USAGE DURING SLEEP
  
  // and off to bed we go
  esp_deep_sleep_start();
}



/**
 * @brief Measures esp32 chip temperature
 * 
 * This measures chip temperature. It's a lot of code because it picks the appropriate range 
 * to measure, and because the API for this is different between ESP32 Arduino versions, this
 * supports 2.x and 3.x
 * 
 * @return float with temperature in degrees celsius.
*/
float ESP32_temp() {
  float result = 0;

  // If temperature for given n below this value,
  // then this is the best measurement we have.
  int cutoffs[5] = { -30, -10, 80, 100, 2500 };
  
  #if ESP_ARDUINO_VERSION_MAJOR >= 3

    int range_start[] = { -40, -30, -10,  20,  50 };
    int range_end[]   = {  20,  50,  80, 100, 125 };
    temperature_sensor_handle_t temp_handle = NULL;
    for (int n = 0; n < 5; n++) {
      temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(range_start[n], range_end[n]);
      ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
      ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
      ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &result));
      ESP_ERROR_CHECK(temperature_sensor_disable(temp_handle));
      ESP_ERROR_CHECK(temperature_sensor_uninstall(temp_handle));
      if (result <= cutoffs[n]) break;
    }

  #else

    // We start with the coldest range, because those temps get spoiled 
    // the quickest by heat of processor waking up. 
    temp_sensor_dac_offset_t offsets[5] = {
      TSENS_DAC_L4,   // (-40°C ~  20°C, err <3°C)
      TSENS_DAC_L3,   // (-30°C ~  50°C, err <2°C)
      TSENS_DAC_L2,   // (-10°C ~  80°C, err <1°C)
      TSENS_DAC_L1,   // ( 20°C ~ 100°C, err <2°C)
      TSENS_DAC_L0    // ( 50°C ~ 125°C, err <3°C)
    };
    for (int n = 0; n < 5; n++) {
      temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
      temp_sensor.dac_offset = offsets[n];
      temp_sensor_set_config(temp_sensor);
      temp_sensor_start();
      temp_sensor_read_celsius(&result);
      temp_sensor_stop();
      if (result <= cutoffs[n]) break;
    }

  #endif

  return result;
}
