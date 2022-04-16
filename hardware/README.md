# Receiver Hardware

This folder contains code to receive and playback audio over WiFi LAN with
an ESP32 and an UDA1334A DAC.

A wiring diagram can be found in [wiring.drawio.xml].

## Code Structure

The code is split into modules. Each module declares its public API in
`include/<module>.hpp` and its sources live in `src/<module>.hpp`. The main
methond in `src/main.cpp` calls `<module>_initialize()` for all modules,
giving them a chance to do setup work and register FreeRTOS tasks to do
work in the long run.

### Module `led`

It queries the other modules for their status and indicates it through
the RGB LED.

### Module `config`

Responsible for
* storing and retrieving the device config to/from flash
* allowing the configuration to be changed via BLE upon user request (by pushing the button)

### Module `network`

Responsible for
* keeping the connection to the LAN alive (automatic reconnect, roaming, ...)
* announcing the devices presence on the network (discovery protocol)
* accepting a connection from an audio source and while connected...
  * accept audio data and hand it to the playback module
  * keep a clock in sync with the audio source so playback is spot-on accurate across multiple receivers
  * notify the audio source if the connection is too bad (so the source device can maybe reduce audio quality for more stable playback)

### Module `playback`

Responsible for
* accepting audio data through buffers owned by the playback modules
* playing the audio data back accurately
* keeping playback in sync with the clock provided by the network module
