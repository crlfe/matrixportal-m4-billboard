# Matrix Portal M4 Billboard

The Adafruit Matrix Portal is a nifty embedded board designed to control
RGB LED matrix panels that use the 5v HUB75 interface. These panels are often
available at low cost because their included electronics are extremely simple.
The drawback to this simplicity is that the controller needs to flicker
individual LEDs on and off very quickly to produce different levels of
brightness, even when just displaying a static image.

The Matrix Portal is available in two versions. The M4, which I am using,
includes both a Microchip SAMD51 processor (for application code) and an
ESP32-WROOM-32E processor (for Wi-Fi). The S3 combines both tasks on a single
ESP32 processor. My impression is that the M4 is older, but as of early 2026
it is still available from all my usual sources, while the S3 is out of stock.

Unfortunately, following CircuitPython tutorials for the Matrix Portal only
gave me 3 bits per color channel, which did not reproduce images well. Plus,
IIRC, the mDNS support was documented as absent. So, C++ it is. After some
tinkering in the Arduino IDE and individually installing dependencies, I
switched to [PlatformIO](https://platformio.org/) as a more comfortable
environment that lets other people reproduce my project.

The current code works, for me, most of the time, but it is definitely
not great. Please consider it an example of something that someone hacked
together to use for your inspiration, rather than trying to actually
deploy it yourself.

- FAT32 filesystem is available over USB for initial configuration.
- Connects to the local WiFi and serves a webapp for easy configuration.
- Displays a static 64x64 image during the night.

The display I have installed at my house stops responding to WiFi requests
eventually, which can be worked around by resetting the board. In early 2026,
I got a second Matrix Portal and a few more panels to look at improving things:

- Clean up the enclosure OpenSCAD code and extend it to support other layouts.
- Figure out whether the WiFi problem is my code or a driver problem.
- Explore driving the HUB75 pins from the DMA controller.
- Experiment with different refresh patterns for the HUB75 panels.
- Evaluate bringing the Matrix Portal up on the
  <a href="https://docs.zephyrproject.org/latest/introduction/index.html">Zephyr</a>
  RTOS.

## Hardware

**_Warning_**: The MatrixPortal must never be connected to both USB and a 5V power supply at the same time.

- [Adafruit MatrixPortal M4](https://www.adafruit.com/product/4745)
- 2x [Adafruit 64x32 RGB LED Matrix - 4mm pitch](https://www.adafruit.com/product/2278)
- [Adafruit 5V 10A switching power supply](https://www.adafruit.com/product/658)
- [Adafruit Female DC Power adapter - 2.1mm jack to screw terminal block](https://www.adafruit.com/product/368)

The 'enclosure.scad' file contains the housing that I 3d printed to use with my
particular hardware. Apparently the position of the screw holes on the LED panels
has changed at least once, so be sure to check measurements against your hardware.

## Building

This project is designed for [Visual Studio Code](https://code.visualstudio.com/)
with the [PlatformIO](https://platformio.org/) extension, and only tested on Linux.

## Configuration

When the board boots successfully it will appear as a USB mass storage device.
The flash must be formatted as FAT32, so please check and reformat it if needed.
The main configuration file is `config.json`:

```json
{
  "wifi": {
    "ssid": "your-wifi-ssid",
    "pass": "your-wifi-password",
    "name": "billboard"
  },
  "http": {
    "user": "web-admin-user",
    "pass": "web-admin-password"
  }
}
```

When you save changes to this file they should be applied immediately.
I recommend unmounting the FAT32 drive to ensure everything is flushed properly.

Assuming everything is working correctly, the web configuration interface
should now be available on your local WiFi by browsing to `http://billboard.local`
(where `billboard` is the name supplied in `config.json`).

The `image.bin` file is a raw (RGB565) image to be displayed on the LED matrix.
This is usually uploaded through the web configuration interface, which will
automatically handle resizing, conversion, and gamma correction.

# License and Warranty Disclaimer

    Copyright 2025 Chris Wolfe (https://crlfe.ca/)

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
