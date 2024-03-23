#ifndef LoRaWAN_ESP32_h
#define LoRaWAN_ESP32_h

// Uncomment this is you do not want to compile in the storing/managing of provisioning data
// #define PERSIST_LOAD_SAVE_SESSION_ONLY

#define MAX_BAND_NAME_LEN   (10)

#include <RadioLib.h>

class NodePersistence {

  public:

    /**
     * @brief Restores session data and nonces as saved by saveSession.
     * 
     * Figures out whether this is a fresh boot or a wakeup from sleep and
     * restores the session information if sleep wakeup or at least the nonces
     * (to be able to start a new session) if fresh boot.
     * 
     * @param node Pointer to LoRaWANNode instance.
     * @return `true` if session data restored, `false` if this was a fresh boot.
    */
    bool loadSession(LoRaWANNode* node);

    /**
     * @brief Saves the session information of a LoRaWAN node.
     *
     * This saves the information needed to maintain your current session (which is
     * moved to the RTC clock's RAM to survive deep sleep), and the information
     * needed to start the next session, which is saved in NVS flash in case we lose
     * the RTC RAM (reset, power loss)
     *
     * @param node Pointer to LoRaWANNode instance.
     * @return `true` if successfully saved, `false` if there was a problem.
     */
    bool saveSession(LoRaWANNode* node);

#ifndef PERSIST_LOAD_SAVE_SESSION_ONLY 

    /**
     * @brief Sets the Stream device on which the provisioning dialog takes place.
     * 
     * Set this to USBserial if your board uses the ESP32s built-in USB serial
    */
    void setConsole(Stream& newConsole);

    /**
     * @brief Do we have a complete set of node provisioning information?
     * 
     * Also copies the provisioning information from flash to the data members
     * which can be considered valid if isProvisioned() returned true.
     * 
     * @return `true` is there is a complete set of provisioning information 
    */
    bool isProvisioned();

    /**
     * @brief Provides a pointer to a new and fully provisioned and (hopefully)
     *        joined LoRaWANNode instance.
     *
     * Use node->isJoined() to find out if joining succeeded.
     *
     * If no provisioning information is found in flash, the user is prompted to
     * enter this via de serial console. Since that essentially halts the system
     * if there's nobody at the serial port, use `isProvisioned()` before
     * calling `manage()` if that's not what you want.
     *
     * @param   phy       Pointer to PhysicalLayer instance to be used. (Usually
     *                    &radio)
     * @param   autoJoin  Whether to try joining or just return a node pointer
     *                    with the right band and subBand configured. (Default
     *                    is to try joining)
     * @return            Pointer to the newly created LoRaWANNode instance
     */
    LoRaWANNode* manage(PhysicalLayer* phy, bool autoJoin = true);

    /**
     * @brief Removes all provisioning and session information from flash
     */
    void wipe();

    /**
     * @brief Asks user for endpoint provisioning information on serial console
     *        and saves it to flash.
     *
     * The serial dialog will only accept valid parameters (e.g. for band).
     * Serial dialog is endless until properly completed: that is, questions
     * will be repeated until the answer is acceptable.
     *
     * @return Presently always returns `true`
     */
    bool provision();

    /**
     * @brief Saves the LoRaWAN endpoint provisioning data to NVS flash keys
     *
     * @param band The LoRaWAN band as text, so "EU868", not &EU868. 
     * @param subBand The sub-band within the LoRaWAN band to use, zero if none.
     * @param joinEUI The JoinEUI (Join Identifier) for the node.
     * @param devEUI The DevEUI (Device Identifier) for the node.
     * @param appKey The application key for the node.
     * @param nwkKey The network key for the node.
     * @return `false` if band does not specify a valid band, true otherwise.
     */
    bool provision(
      const char* band,
      const uint8_t subBand,
      const uint64_t joinEUI,
      const uint64_t devEUI,
      const uint8_t* appKey,
      const uint8_t* nwkKey
    );

    /*
      These will only hold valid data after you have called isProvisioned()
      and it has returned `true`
    */
    const char* getBand();
    const uint8_t getSubBand();
    const uint64_t getJoinEUI();
    const uint64_t getDevEUI();
    const uint8_t* getAppKey();
    const uint8_t* getNwkKey();

    /**
     * @brief Converts a textual representation of a LoRaWAN band into its
     *        pointer. Returns `nullptr` if not a valid band name.
     *
     * You could use this to check if band name is valid if your device provides an
     * alternative interface for provisioning.
     *
     * @param band    The band name, e.g. "EU868"
     * @return        Pointer to the LoRaWANBand_t struct with band information
     */
    const LoRaWANBand_t* bandToPtr(const char* band);

    /**
     * @brief Get the number of valid LoRaWAN bands currently supported
     *
     * Can be used in conjunction with bandName to get a list of band names, which
     * could be used to populate selection dialogs, etc. 
     *
     * @return The number of bands
    */
    uint16_t numberOfBands();

    /**
     * @brief Gets char* to the name of the LoRaWAN indicated by the number.
     * 
     * Number from zero to one less than the number returned by `numberOfBands()`
     * 
     * @return char* of name, or `nullptr` if number out of range
    */
    const char* bandName(uint16_t number);

  private:

    char band[MAX_BAND_NAME_LEN + 1];
    uint8_t subBand;
    uint64_t joinEUI;
    uint64_t devEUI;
    uint8_t appKey[16];
    uint8_t nwkKey[16];
    Stream* console = &Serial;
    bool parseHexString(int num_bytes, const char* input_str, uint8_t* data);
    bool parseHexToUint64(const char* input_str, uint64_t& result) ;

#endif  // PERSIST_LOAD_SAVE_SESSION_ONLY 

};

extern NodePersistence persist;

#endif
