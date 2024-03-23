#include "LoRaWAN_ESP32.h"

// The Arduino Prefeences library is used to store data in NVS, Espressif's key-value flash store
#include "Preferences.h"
Preferences nvs;

// Storage in RTC RAM
RTC_DATA_ATTR uint8_t lorawan_nonces[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
RTC_DATA_ATTR uint8_t lorawan_session[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
RTC_DATA_ATTR int bootcount = 0;

bool NodePersistence::loadSession(LoRaWANNode* node) {
  if (bootcount++ == 0) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] bootcount == 0");
    // We woke up fresh, so restore only the nonces from flash
    nvs.begin("lorawan");
    if (nvs.getBytes("nonces", lorawan_nonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE) == RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
      RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Nonces restored from NVS");
      node->setBufferNonces(lorawan_nonces);
    } else {
      RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] No nonces found in NVS");
    }
    nvs.end();
    return false;
  } else {
    // If this is a repeated boot, restore nonces and session data from RTC RAM
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Nonces and session data restored from RTC RAM");
    node->setBufferNonces(lorawan_nonces);
    node->setBufferSession(lorawan_session);
    return true;
  }
}

bool NodePersistence::saveSession(LoRaWANNode* node) {
  // Get the persistence data from RadioLib and copy to RTC RAM
  memcpy(lorawan_nonces, node->getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  memcpy(lorawan_session, node->getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Nonces and session data saved to RTC RAM");
  // Store the nonces in flash
  nvs.begin("lorawan");
  if (nvs.putBytes("nonces", lorawan_nonces, RADIOLIB_LORAWAN_NONCES_BUF_SIZE) == RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Nonces saved to NVS. (Only actually written if changed.)");
    nvs.end();
    return true;
  } else {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] There was a problem saving nonces to NVS");
    nvs.end();
    return false;
  }
}

#ifndef PERSIST_LOAD_SAVE_SESSION_ONLY 

void NodePersistence::setConsole(Stream& newConsole) {
  this->console = &newConsole;
}

bool NodePersistence::isProvisioned() {

  // Magic word supplied as default to NVS read functions to signal they are empty 
  const uint64_t empty = 0x230420AADEADBEEF;

  nvs.begin("lorawan");

  RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Reading from NVS");
  nvs.getString("band", this->band, MAX_BAND_NAME_LEN);
  if (this->band != "") {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist]      band: %s", this->band);
  }
  this->subBand = nvs.getUChar("subBand");
  if (this->subBand) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist]   subband: %d", this->subBand);
  }
  this->joinEUI = nvs.getULong64("joinEUI", empty);
  if (this->joinEUI != empty) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist]   joinEUI: %016"PRIx64, this->joinEUI);
  }
  this->devEUI = nvs.getULong64("devEUI", empty);
  if (this->devEUI != empty) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist]    devEUI: %016"PRIx64, this->devEUI);
  }
  int lenAppKey = nvs.getBytes("appKey", this->appKey, 16);
  if (lenAppKey == 16) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist]    appKey: ");
    RADIOLIB_DEBUG_PROTOCOL_HEXDUMP(this->appKey, 16);
  }
  int lenNwkKey = nvs.getBytes("nwkKey", this->nwkKey, 16);
  if (lenNwkKey == 16) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist]    nwkKey: ");
    RADIOLIB_DEBUG_PROTOCOL_HEXDUMP(this->nwkKey, 16);
  }

  nvs.end();
  
  // If everything seems OK, return true
  if (
      bandToPtr(this->band) != nullptr && 
      this->joinEUI != empty && 
      this->devEUI != empty && 
      lenAppKey == 16 && 
      lenNwkKey == 16) {
    return true;
  }
  return false;
  
}

LoRaWANNode* NodePersistence::manage(PhysicalLayer* phy, bool autoJoin) {

  if (!this->isProvisioned()) { 
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] No or incomplete provisioning. Getting from console.");
    this->provision();
    if (autoJoin) {
      console->println("Now joining network.");
    }
  }

  // We can now assume we have all the needed data

  LoRaWANNode* node = new LoRaWANNode(phy, bandToPtr(this->band), this->subBand);

  bool restored = this->loadSession(node);

  if (!autoJoin) {
    return node;
  }

  int16_t state = RADIOLIB_ERR_UNKNOWN;
  if (restored) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Session data found, doing beginOTAA.");
    state = node->beginOTAA(this->joinEUI, this->devEUI, this->nwkKey, this->appKey);
  }  
  if (!restored || !node->isJoined()) {
    RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] No session data or beginOTAA failed: join forced.");
    node->beginOTAA(this->joinEUI, this->devEUI, this->nwkKey, this->appKey, true);
  }

  return node;

}

void NodePersistence::wipe() {
  RADIOLIB_DEBUG_PROTOCOL_PRINTLN("[persist] Wiping all LoRaWAN parameters from flash.");
  nvs.begin("lorawan");
  nvs.clear();
  nvs.end();
}

bool NodePersistence::provision() {
  console->flush();
  console->setTimeout(100000);
  console->println("Please enter the provisioning information needed to join the LoRaWAN network.\n");

  // Band
  String band;
  while (true) {
    console->print("Enter LoRaWAN band (e.g. EU868 or US915)  ");
    band = console->readStringUntil('\n');
    band.toUpperCase();
    if (bandToPtr(band.c_str()) != nullptr) break;
    console->printf("\nError: '%s' is not a supported band.\n", band.c_str());
  }
  console->printf("[%s]\n", band.c_str());

  // subBand
  int subBand;
  String subBand_str;
  while (true) {
    console->print("Enter subband for your frequency plan, if applicable. Otherwise just press Enter.  ");
    subBand_str = console->readStringUntil('\n');
    if (subBand_str == "") {
      subBand = 0;
      break;
    }
    subBand = subBand_str.toInt();
    if (subBand > 0 and subBand < 255) break;
    console->printf("\nError: '%s' is not a valid subband.\n", subBand_str.c_str());
  }
  console->printf("[%s]\n", subBand_str.c_str());

  // joinEUI
  uint64_t joinEUI;
  String joinEUI_str;
  while (true) {
    console->print("Enter joinEUI (64 bits, 16 hex characters.) Press enter to use all zeroes.  ");
    joinEUI_str = console->readStringUntil('\n');
    if (joinEUI_str == "") {
      joinEUI_str = "0000000000000000";
    }
    if (parseHexToUint64(joinEUI_str.c_str(), joinEUI)) break;
    console->printf("\nError: '%s' is not a valid joinEUI.\n", joinEUI_str.c_str());
  }
  console->printf("[%s]\n", joinEUI_str.c_str());

  // devEUI
  uint64_t devEUI;
  String devEUI_str;
  while (true) {
    console->print("Enter devEUI (64 bits, 16 hex characters)  ");
    devEUI_str = console->readStringUntil('\n');
    if (parseHexToUint64(devEUI_str.c_str(), devEUI)) break;
    console->printf("\nError: '%s' is not a valid devEUI.\n", devEUI_str.c_str());
  }
  console->printf("[%s]\n", devEUI_str.c_str());

  // appKey
  uint8_t appKey[16];
  String appKey_str;
  while (true) {
    console->print("Enter appKey (128 bits, 32 hex characters)  ");
    appKey_str = console->readStringUntil('\n');
    if (parseHexString(16, appKey_str.c_str(), appKey)) break;
    console->printf("\nError: '%s' is not a valid appKey.\n", appKey_str.c_str());
  }
  console->printf("[%s]\n", appKey_str.c_str());

  // nwkKey
  uint8_t nwkKey[16];
  String nwkKey_str;
  while (true) {
    console->print("Enter nwkKey (128 bits, 32 hex characters)  ");
    nwkKey_str = console->readStringUntil('\n');
    if (parseHexString(16, nwkKey_str.c_str(), nwkKey)) break;
    console->printf("\nError: '%s' is not a valid nwkKey.\n", nwkKey_str.c_str());
  }
  console->printf("[%s]\n", nwkKey_str.c_str());

  this->provision(band.c_str(), subBand, joinEUI, devEUI, appKey, nwkKey);
  console->println("Thank you. Provisioning information saved to flash.");
  return true;
}

bool NodePersistence::provision(
  const char* band,
  const uint8_t subBand,
  const uint64_t joinEUI,
  const uint64_t devEUI,
  const uint8_t* appKey,
  const uint8_t* nwkKey
) {
  if (bandToPtr(band) == nullptr) {
    return false;
  }
  // Write everything to flash
  nvs.begin("lorawan");
  nvs.putString("band", band);
  nvs.putUChar("subBand", subBand);
  nvs.putULong64("joinEUI", joinEUI);
  nvs.putULong64("devEUI", devEUI);
  nvs.putBytes("appKey", appKey, 16);
  nvs.putBytes("nwkKey", nwkKey, 16);
  nvs.end();
  return this->isProvisioned();
}

const char* NodePersistence::getBand() {
  return this->band;
}

const uint8_t NodePersistence::getSubBand() {
  return this->subBand;
}

const uint64_t NodePersistence::getJoinEUI() {
  return this->joinEUI;
}

const uint64_t NodePersistence::getDevEUI() {
  return this->devEUI;
}

const uint8_t* NodePersistence::getAppKey() {
  return this->appKey;
}

const uint8_t* NodePersistence::getNwkKey() {
  return this->nwkKey;
}




const char* band_names[] = {
  "EU868",
  "US915",
  "CN780",
  "EU433",
  "AU915",
  "CN500",
  "AS923",
  "KR920",
  "IN865",
};

const LoRaWANBand_t* band_pointers[] = {
  &EU868,
  &US915,
  &CN780,
  &EU433,
  &AU915,
  &CN500,
  &AS923,
  &KR920,
  &IN865,
};

#define NUM_BANDS (sizeof(band_pointers) / sizeof(band_pointers[0]))

const LoRaWANBand_t* NodePersistence::bandToPtr(const char* band) {

  for (int n = 0; n < NUM_BANDS; n++) {
    if (strcmp(band_names[n], band) == 0) {
      return band_pointers[n];
    }
  }
  return nullptr;

}

uint16_t NodePersistence::numberOfBands() {
  return NUM_BANDS;
}

const char* NodePersistence::bandName(uint16_t number) {
  if (number >= NUM_BANDS) {
    return nullptr;
  }
  return band_names[number];
}

/**
 * @brief Parses a set-length hexadecimal string and converts it into a byte array.
 *
 * @param num_bytes input_str must have this many bytes, thus twice as many chars.
 * @param input_str The hexadecimal string to be parsed.
 * @param data      Pointer to byte array to store the parsed bytes.
 * @return          True if the parsing was successful, false otherwise.
 */
bool NodePersistence::parseHexString(int num_bytes, const char* input_str, uint8_t* data) {
  if (strlen(input_str) != num_bytes * 2) return false;
  for (int i = 0; i < num_bytes * 2; i++) {
    if (!isHexadecimalDigit(input_str[i])) return false;
    int value = input_str[i] - (input_str[i] < 58 ? 48 : (input_str[i] < 97 ? 55 : 87));
    if (i & 1) data[i >> 1] |= value;
    else data[i >> 1] = value << 4;
  }
  return true;
}

/**
 * @brief Parses a hexadecimal string into a 64-bit unsigned integer.
 *
 * @param input_str The 16-character hexadecimal string to be parsed.
 * @param result    uint64_t variable where the parsed value will be stored.
 * @return          `true` if the parsing was successful, `false` otherwise.
 */
bool NodePersistence::parseHexToUint64(const char* input_str, uint64_t& result) {
  if (strlen(input_str) != 16) return false; // Expecting 16 hex characters for 64 bits
  result = 0; // Initialize result
  for (int i = 0; i < 16; i++) {
    if (!isHexadecimalDigit(input_str[i])) return false; // Validate hex digit
    uint64_t value = input_str[i] - (input_str[i] < 58 ? 48 : (input_str[i] < 97 ? 55 : 87));
    result = (result << 4) | value;
  }
  return true;
}

#endif  // PERSIST_LOAD_SAVE_SESSION_ONLY


NodePersistence persist;
