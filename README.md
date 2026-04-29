<div align="center">

# Raspberry Pi Pico LAN Tester

### Portable Ethernet diagnostic tool with `W5500`, `OLED`, battery charging and a custom PCB

<img src="docs/overview.png" alt="Raspberry Pi Pico LAN Tester" width="750">

</div>

---

## Overview

This is a compact handheld LAN tester based on a **Raspberry Pi Pico**, a **W5500 Ethernet controller**, an **SSD1306 OLED display** and **LiPo battery charging**.

It can quickly check whether an Ethernet port is actually working. The tester checks the physical link, requests an IP address with DHCP, shows IP / gateway / DNS information and performs a simple internet connectivity test.

The full build guide is published on Instructables.

---

## Main Features

- `W5500` Ethernet controller over SPI
- `SSD1306` 128x64 OLED display
- One-button LAN test
- Link, DHCP and internet check
- IP / Gateway / DNS display
- Battery voltage and charging status
- Custom PCB with Raspberry Pi Pico
- 3D printed enclosure

---

## Project Files

```text
LANTester/
├── Code/
└── KiCad/

Code contains the Raspberry Pi Pico firmware.
KiCad contains the schematic, PCB and manufacturing files.

Flashing

If the ready-built LANTester.uf2 file is included, flashing is simple:

Hold BOOTSEL on the Raspberry Pi Pico.
Plug it into USB.
Copy LANTester.uf2 to the Pico drive.
The Pico reboots and starts the firmware.

No special programmer is needed.

Full Build Guide

Instructables: INSTRUCTABLES-LINK-HERE

3D Printed Case

Printables: PRINTABLES-LINK-HERE

Notes

This repository contains the firmware and hardware files for the project.
Photos, build steps and assembly instructions are documented in the Instructables guide.