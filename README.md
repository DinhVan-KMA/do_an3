## Features

### Arduino Controller

- Receive CAN messages from ESP32
- Control steering servo motor
- Control vehicle movement
- Execute driving commands
- Process emergency braking (AEB)

### Firmware (STM32)

- Read vehicle sensor data
- Measure wheel speed using encoder
- Measure battery voltage using ADC
- Measure obstacle distance using ultrasonic sensor
- Package sensor data into CAN frames
- Transmit data via CAN Bus

### Communication (ESP32)

- Receive CAN frames from STM32
- Communicate with Arduino through CAN Bus
- Connect to Wi-Fi
- Send telemetry data to Node.js server using WebSocket
- HMAC authentication with timestamp to prevent replay attacks

### Dashboard

- Display real-time speed
- Display battery percentage
- Display obstacle distance
- Display driving mode
- Display AEB status
- Estimate remaining driving range
