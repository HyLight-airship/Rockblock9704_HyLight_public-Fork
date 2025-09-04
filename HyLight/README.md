# FTS 2.0 v1 - Flight Termination System with Iridium Communication

## Table of Contents
- [Overview](#overview)
- [Features](#features)
- [System States](#system-states)
- [Hardware Requirements](#hardware-requirements)
  - [Core Components](#core-components)
  - [Pin Configuration](#pin-configuration)
  - [Communication Interfaces](#communication-interfaces)
- [Software Architecture](#software-architecture)
  - [Core Components](#core-components-1)
  - [Key Libraries](#key-libraries)
- [Installation & Setup](#installation--setup)
  - [Prerequisites](#prerequisites)
  - [Build Instructions](#build-instructions)
  - [Hardware Setup](#hardware-setup)
- [Usage](#usage)
  - [Initialization](#initialization)
  - [Command Interface](#command-interface)
  - [Status Monitoring](#status-monitoring)
  - [Debug Output](#debug-output)
- [Configuration](#configuration)
  - [Timing Parameters](#timing-parameters)
  - [Communication Settings](#communication-settings)
- [Troubleshooting](#troubleshooting)
  - [Common Issues](#common-issues)
  - [Debug Information](#debug-information)
- [Safety Considerations](#safety-considerations)
- [Development](#development)
  - [Code Structure](#code-structure)
  - [Adding Features](#adding-features)
- [License](#license)
- [Version History](#version-history)

---

## Overview

The FTS 2.0 v1 is a professional-grade Flight Termination System designed for aerospace applications, featuring reliable satellite communication via the RockBLOCK 9704 Iridium modem. This system provides remote command and control capabilities with real-time status monitoring through the Iridium satellite network.

## Features

- **Remote Command Control**: Arm, disarm, and trigger the system via satellite commands
- **Real-time Status Monitoring**: Continuous status updates through Iridium network
- **State Machine Management**: Three operational states (DISARMED, ARMED, TRIGGERED)
- **Automatic Status Reporting**: Configurable reporting intervals based on system state
- **Satellite Communication**: RockBLOCK 9704 modem with global coverage
- **Hardware Abstraction**: Cross-platform compatibility (STM32, Linux, Windows)

## System States

|     State     |         Description         | Status Update Frequency |
|---------------|-----------------------------|-------------------------|
| **DISARMED**  | System inactive and safe    | Every 10 seconds        |
| **ARMED**     | System ready and monitoring | Every 30 seconds        |
| **TRIGGERED** | System activated            | Every 5 seconds         |

## Hardware Requirements

### Core Components
- **Microcontroller**: STM32G431RBT6 (Nucleo-64 board)
- **Satellite Modem**: RockBLOCK 9704 Iridium modem
- **Power Supply**: 5V regulated power supply

### Pin Configuration
| Function        | Pin  |        Description         |
|-----------------|------|----------------------------|
| **POWER_EN**    | PB9  | Modem power enable control |
| **IRIDIUM_EN**  | PB8  | Modem enable control       |
| **IRIDIUM_BTD** | PB4  | Modem boot detection input |
| **USART1_TX**   | PA9  | Connected to modem RX pin  |
| **USART1_RX**   | PA10 | Connected to modem TX pin  |
| **USART2_TX**   | PA2  | Debug console TX           |
| **USART2_RX**   | PA3  | Debug console RX           |

### Communication Interfaces
- **USART1**: 230400 baud for RockBLOCK modem communication
- **USART2**: 115200 baud for debug console output

## Software Architecture

### Core Components
- **Main Application**: FTS state machine and control logic
- **RockBLOCK Library**: JSPR protocol implementation for modem communication
- **Hardware Abstraction Layer**: Cross-platform GPIO and serial functions
- **Message Queue System**: Asynchronous message handling for MO/MT messages

### Key Libraries
- **STM32 HAL**: Hardware abstraction for STM32 microcontrollers
- **cJSON**: JSON parsing for modem responses
- **Cross-platform utilities**: Timing and string manipulation functions

## Installation & Setup

### Prerequisites
- STM32CubeIDE or compatible development environment
- STM32 HAL libraries
- RockBLOCK 9704 modem with active Iridium service

### Build Instructions
1. Clone the repository:
   ```bash
   git clone <url>
   cd FTS
   ```

2. Open the project in STM32CubeIDE:
   - Import existing project
   - Select the `FTS_2.0_v1` folder
   - Build the project (Project → Build All or CTRL+B)

3. Configure the modem:
   - Ensure RockBLOCK 9704 is properly powered
   - Verify Iridium service is active
   - Check antenna connection

### Hardware Setup
1. Connect the RockBLOCK modem to the STM32 board:
   - UART communication lines
   - GPIO control pins

2. Connect the RockBLOCK to a power suplly:
   - 5V

3. optionnal: Connect debug interface:
   - ST-LINK to USB port

## Usage

### Initialization
The system automatically initializes on power-up:
1. Hardware initialization (GPIO, UART, DMA)
2. Modem power-up sequence
3. RockBLOCK library initialization
4. Callback function registration
5. Main processing loop entry

### Command Interface
The system accepts remote commands via Iridium:

| Command | Action  |            Description             |
|---------|---------|------------------------------------|
| **1**   | ARM     | Arms the system (if not triggered) |
| **2**   | TRIGGER | Activates the system permanently   |

### Status Monitoring
- Real-time status updates via debug console
- Message transmission/reception counters
- Signal strength monitoring
- Error condition reporting

### Debug Output
The system provides comprehensive debug information:
- Initialization status
- Modem provisioning details
- Message transmission results
- System state changes
- Error conditions

## Configuration

### Timing Parameters
- **DISARMED**: 10-second status interval
- **ARMED**: 30-second status interval  
- **TRIGGERED**: 5-second status interval

### Communication Settings
- **Modem baud rate**: 230400 bps
- **Debug console**: 115200 bps
- **Timeout values**: 60-second modem initialization

## Troubleshooting

### Common Issues
1. **Modem initialization failure**:
   - Check power supply and connections
   - Verify GPIO pin configuration
   - Ensure antenna is connected and has a clear view of sky

2. **Communication errors**:
   - Check UART connections
   - Verify baud rate settings
   - Monitor signal strength

3. **Build errors**:
   - Ensure all dependencies are installed
   - Check STM32 HAL version compatibility
   - Verify project configuration

### Debug Information
- Monitor debug console output for status messages
- Check LED indicators for error conditions
- Use STM32CubeIDE debugger for step-by-step execution

## Safety Considerations

⚠️ **IMPORTANT**: This is a Flight Termination System designed for aerospace applications.

- **Never test on operational aircraft** without proper safety protocols
- **Verify all connections** before power-up
- **Test in controlled environment** before deployment
- **Follow aerospace safety standards** and regulations
- **Ensure proper redundancy** in production systems

## Development

### Code Structure
```
FTS_2.0_v1/
├── Core/
│   ├── Src/
│   │   ├── main.c               # Main application
│   │   └── ...
│   ├── Inc/
│   └── Rockblock/               # RockBLOCK library
├── Drivers/                     # STM32 HAL drivers
├── .mxproject                   # STM32CubeIDE configuration
└── README.md                    # This file
```

### Adding Features
- Follow existing code style and documentation standards
- Use Doxygen-compatible comments for new functions
- Test thoroughly before integration
- Update documentation for new features

## License

[HyLight]


## Version History

- **v1.0.0**: Initial release with basic FTS functionality
- Core state machine implementation
- RockBLOCK 9704 integration
- Cross-platform compatibility

---

**Note**: This system is designed for professional aerospace applications. Ensure compliance with all applicable safety standards and regulations before deployment.
