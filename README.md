# MatrixPortal M4 Billboard

The Adafruit MatrixPortal is a nifty Arduino-compatible board intended for
driving RGB LED arrays. The M4 is a now-obsolete version that used both an
ARM Cortex M4 processor (for application code) and an ESP32 WIFI processor
(for wifi).

Unfortunately, using CircuitPython with my two 64x32 RGB LED arrays only
allowed 3 bits per channel, which the response curve of the LEDs reduced
to basically off, dim, and bright. Plus, IIRC, the mDNS support was broken.
So, C++ it is. After some tinkering with the Arduino IDE, I settled on
[PlatformIO](https://platformio.org/) as a more comfortable environment.

The resulting code works, for me, most of the time. But it is definitely
not great. Please consider it an example of something that someone hacked
together and use for your inspiration, rather than trying to actually
deploy it yourself.

- Displays a 64x64 image from 9pm to 6am (UTC-4).
- Connects to the local WiFi and serves a webapp for easy configuration.
  - BUG: WiFi tends to disconnect and not recover; reset and wait a minute.
- Filesystem is available over USB for configuration and to modify webapp.
  - BUG: Writes tend to corrupt files; unmounting immediately seems to help.

I'm not optimistic about being able to fix all of the problems without
bringing the board up on a more complete RTOS
([Zephyr](https://docs.zephyrproject.org/latest/introduction/index.html) is intriguing).
However, with the MatrixPortal M4 being obsolete and unavailable, it seems
a better use of my time to poke at other platforms.

## Hardware

**_Warning_**: The MatrixPortal must never be connected to both USB and a 5V power supply at the same time.

- [Adafruit MatrixPortal M4 (obsolete)](https://www.adafruit.com/product/4745)
- 2x [Adafruit 64x32 RGB LED Matrix - 4mm pitch](https://www.adafruit.com/product/2278)
- [Adafruit 5V 10A switching power supply](https://www.adafruit.com/product/658)
- [Adafruit Female DC Power adapter - 2.1mm jack to screw terminal block](https://www.adafruit.com/product/368)

## Building

This project is designed for [Visual Studio Code](https://code.visualstudio.com/)
with the [PlatformIO](https://platformio.org/) extension, and only tested on Linux.

## Configuration

When the board boots successfully, it will appear as a USB mass storage device
formatted in FAT32. The main configuration file is `config.json`:

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
