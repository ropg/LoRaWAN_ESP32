# LoRaWAN_ESP32

#### ESP32 persistence handling for RadioLib LoRaWAN endpoints

&nbsp;

## Introduction

This is for those wanting to use RadioLib's `LoRaWANNode` in combination with the ESP32's deep sleep. Unlike some other embedded systems, the ESP32 loses RAM contents during deep sleep, except for 8kB of RAM connected to the the built-in Real-Time Clock. RadioLib LoRaWAN offers the ability to take the session state and save it somewhere for next time you want to send a packet. In its simplest form, this library provides that somewhere, putting that which can be safely lost (the session state) in RTC RAM, and that which you don't want to lose (the information needed to create the next session), in the ESP32's NVS flash partition.

> [!WARNING]
> This library interacts with experimental RadioLib / LoRaWANNode functions that are not in any released versions yet, so for the time being this is only for those using RadioLib fresh off GitHub. This API – along with LoRaWAN functionality in RadioLib more generally – is in beta and subject to change. Do not use in production.

A more advanced use of this library is to have it manage the LoRaWAN endpoint provisioning information also. There's a way to provide the provisioning data from your own code, for you to use if you are building a LoRaWAN device with a web interface or using an app and Bluetooth, or if you keep your provisioning data on 8" floppy, or whatever. If your code starts `persist.manage` with no provisioning information in flash, the node will start a serial dialogue to obtain the information. There you can just paste in the information, e.g. from The Things Network registration screen.

```
Temperature: 32.40 °C
Radio init
No provisioning data found in flash.
Please enter the provisioning information needed to join the LoRaWAN network.
Enter LoRaWAN band (e.g. EU868 or US915)  [EU868]
Enter subband for your frequency plan, if applicable. Otherwise just press Enter.  []
Enter joinEUI (64 bits, 16 hex characters.) Press enter to use all zeroes.  [0000000000000000]
Enter devEUI (64 bits, 16 hex characters)  [70B3D57ED0066298]
Enter appKey (128 bits, 32 hex characters)  [4ED1AB3EA409E132A7A537198604AEB0]
Enter nwkKey (128 bits, 32 hex characters)  [83A69393B3D2BBB64307B260EB6BA1EB]
Thank you. Provisioning information saved to flash.
Now joining network.
Message sent
Going to deep sleep now
Next TX in 1194 s
```

On all subsequent starts of your node, the information will be retrieved from flash and the node will be joined and ready when `persist.manage` returns.

&nbsp;

## Pointers

All the functions here handle pointers to the `LoRaWANNode` instance instead of references. Reason is that the node can only be created once the LoRaWAN band is known. In the simple use case of just using `persist` for storing and retrieving the session state, all that means is that you provide `&node` if your code has a global instance. In the case where `persist` manages the provisioning information, it means you can't have a global node instance, only a global pointer, and that you interact with `LoRaWANNode` with `node->someFunction()` instead of `node.someFunction()`.

&nbsp;

## Saving and restoring LoRaWAN session state

```cpp
/**
 * @brief persist.loadSession restores session data and nonces as saved by
 *        saveSession.
 *
 * Figures out whether this is a fresh boot or a wakeup from sleep and
 * restores the session information if sleep wakeup or at least the nonces
 * (to be able to start a new session) if fresh boot.
 *
 * @param node Pointer to LoRaWANNode instance.
 * @return `true` if session data restored, `false` if this was a fresh
 * boot.
*/
bool loadSession(LoRaWANNode* node);

/**
 * @brief persist.saveSession saves session information of a LoRaWAN node.
 *
 * This saves the information needed to maintain your current session (which
 * is moved to the RTC clock's RAM to survive deep sleep), as well as the
 * information needed to start the next session, which is saved in NVS flash
 * in case we lose the RTC RAM (reset, power loss)
 *
 * @param node Pointer to LoRaWANNode instance.
 * @return `true` if successfully saved, `false` if there was a problem.
 */
bool saveSession(LoRaWANNode* node);
```

To use these functions:

* Take any RadioLib example file, and add `#include <LoRaWAN_ESP32.h>` at the top. This will create a `persist` instance that your code can interact with.

* Then put `persist.loadSession(&node)` somewhere after the node instance is created. This will return true if a previous session was saved and there was an existing session in RTC RAM, i.e. when your code is waking up from deep sleep. When `loadSession` returns `false`, one of two things might have happened: either there was no saved LoRaWAN data found at all, or only the 'nonces' were retrieved from flash. In the latter case, the nonces allow you to re-join the network in a secure way, without any messages about the devNonce being too small. 

* After joining (`beginOTAA`) and before your code tells the ESP32 to go to deep sleep, call `persist.saveSession(&node)` to save the session state. The session state lives in RTC RAM, which does have limited-lifetime issues, and when the 'nonces' (the intra-session state) change, you want to write them to flash. Nothing is ever written if it hasn't changed, so you effectively cannot call `persist.saveSession` too often. 

> *Best to call* `beginOTAA` *with the* `force` *argument set to* `true` *if* `loadSession` *returns* `false`*, because you already know that the network will need to be re-joined as there is no session information anymore.*

&nbsp;

## Managing provisioning data

```cpp
/**
 * @brief persist.setConsole selects the Stream device on which the
 *        provisioning dialog takes place.
 *
 * Set this to USBserial if your board uses the ESP32s built-in USB serial.
*/
void setConsole(Stream& newConsole);

/**
 * @brief persist.isProvisioned tells whether or not there's stored
 *        provisioning information?
 *
 * Also copies the provisioning information from flash to the internal
 * buffer, where it can be retrieved by the getXXX functions. The data these
 * return can only be considered valid if persist.isProvisioned() returned
 * true.
 *
 * @return `true` is there is a complete set of provisioning information 
*/
bool isProvisioned();

/**
 * @brief persist.manage is the workhorse of the state persistence. It
 *        returns a pointer to a new, fully provisioned and (hopefully)
 *        joined LoRaWANNode instance.
 *
 * Use node->isJoined() to find out if joining succeeded.
 *
 * If no provisioning information is found in flash, the user is prompted to
 * enter this via de serial console. Since that essentially halts the system
 * if there's nobody at the serial port, use `persist.isProvisioned()`
 * before calling `persist.manage()` if that's not what you want.
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
 * @brief persist.wipe wipes all the stored LoRaWAN provisioning and session
 *        information from flash.
 */
void wipe();

/**
 * @brief persist.provision without any arguments asks user for endpoint
 *        provisioning information on serial console and saves it to flash.
 *
 * The serial dialog will only accept valid parameters (e.g. for band).
 * Serial dialog is endless until properly completed: that is, questions
 * will be repeated until the answer is acceptable.
 *
 * @return Presently always returns `true`
 */
bool provision();

/**
 * @brief persist.provision can also take the provisioning data as arguments
 *        and store it in flash.
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
 * @brief persist.bandToPtr turns a textual representation of a LoRaWAN band
 *        into its pointer. Returns `nullptr` if not a valid band name.
 *
 * You could use this to check if band name is valid if your device provides
 * an alternative interface for provisioning.
 *
 * @param band    The band name, e.g. "EU868"
 * @return        Pointer to the LoRaWANBand_t struct with band information
 */
const LoRaWANBand_t* bandToPtr(const char* band);

/**
 * @brief persist.numberOfBands returns the number of LoRaWAN bands
 *        currently supported.
 *
 * Can be used in conjunction with bandName to get a list of band names,
 * which could be used to populate selection dialogs, etc. 
 *
 * @return The number of bands
*/
uint16_t numberOfBands();

/**
 * @brief persist.bandName returns char* for the name of the LoRaWAN band
 *        indicated by the number.
 *
 * Number from zero to one less than the number returned by
 * `numberOfBands()`
 *
 * @return char* of name, or `nullptr` if number out of range
*/
const char* bandName(uint16_t number);
```

&nbsp;

#### Usage

Much of the documentation above will be self-explanatory. The [managed_provisioning_data example](https://github.com/ropg/LoRaWAN_ESP32/blob/main/examples/managed_provisioning_data/managed_provisioning_data.ino) with this library shows a very simple LoRaWAN device that will prompt for its provisioning data via the serial port on first run and then will happily provide ESP32 chip temperature at deep-sleep wakeup every 15 minutes. In fact, it's the example that produced the dialog at the beginning of this README.

* When using `persist.manage`, you still need to `persist.saveSession` before deep sleep.

* Use `setConsole()` if you are not using `Serial`, e.g. when you are using the ESP32 built-in USB serial "chip" via `USBSerial`. This determines where the dialog happens if there's no provisioning data.

* `numberOfBands()` and `bandName()` will let your code populate a dropdown of valid bands.

* If you want to see what's going on inside `persist` and inside the LoRaWANNode in general, simply turn the protocol debugging on in RadioLib's `BuildOpt.h`. This library's messages are prefixed with `[persist]`.

#### Starting over

If you're developing and want to test provisioning with new data, or you lost the 'nonces' (the intra-session state) because of a crash/reset at an inopportune moment; the easiest way to forget your provisioning data (on Arduino IDE) is to enable the "Tools / Erase all flash before sketch upload" option. Don't forget to turn that back off or you'll be wiping your provisioning and session data each time. If you're developing something for other people to use, you could maybe detect a very long button press to execute `persist.wipe()` and then reboot to restore to unprovisioned state.
