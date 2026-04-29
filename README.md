# Raspberry Pi Pico LAN Tester

Portable LAN tester based on a Raspberry Pi Pico, W5500 Ethernet controller, SSD1306 OLED display and LiPo battery charging.

This repository contains the firmware and KiCad hardware files for the project.

## Main Features

- W5500 Ethernet over SPI
- SSD1306 OLED display
- One-button LAN test
- Link, DHCP and internet check
- IP / Gateway / DNS display
- Battery voltage and charging status
- Custom PCB with Raspberry Pi Pico
- 3D printed enclosure

## Files

The main project folder is:

```text
LANTester/
├── Code/
└── KiCad/
Code contains the Raspberry Pi Pico firmware.
KiCad contains the schematic, PCB and manufacturing files.

Full Build Guide

The complete step-by-step build guide is available on Instructables:

INSTRUCTABLES-LINK-HERE

3D Printed Case

The 3D printed case files are available on Printables:

PRINTABLES-LINK-HERE

Flashing

Use the ready-built UF2 file if included.

Hold BOOTSEL on the Raspberry Pi Pico.
Plug it into USB.
Copy the .uf2 file to the Pico drive.
The Pico reboots and starts the firmware.