# esp-main-sensors-board

This project is a part of combined system with another [ESP-WHO project](https://github.com/AlexKar0706/esp-who).

ESP32S3 programmed to gather distance from 3 different sensors: ToF sensor (VL53L1X), ToF sensor (VL53L1X), ultrasonic sensor (HC-SR04) and IR sensor (GP2Y0A41SK0F) with is implemented in components/sensors.

To communicate with another sensor, ESP32 utilize ESP-NOW protocol.

To transmit gatherd data, ESP32 utilize HTTP to which user can be connected via configured Wifi SoftAP to make "/data" GET-request.

## ESP-NOW Driver

ESPNOW driver implement both reciever and sender parts in components/espnow_driver. Current part of the project expects to have receiver part of the protocol, but it can be changed using sdkconfig.

## Quick Start

To deploy code to microcontroller, follow the instructions below.

### Set the target SOC and the default sdkconfig configuration file.
```
idf.py set-target esp32s3
```

Add "" if using powershell.
```
idf.py set-target "esp32s3"
```

### (Optional) Configure sdkconfig options
```
idf.py menuconfig
```

### Build, Flash and monitor

```
idf.py [-p port] flash monitor
```

> [!NOTE]
> - Here [-p port] is optional, if no port is specified, all ports will be scanned. 
> - [check port on linux/macos](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/establish-serial-connection.html#check-port-on-linux-and-macos)  
> - [check port on win](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/establish-serial-connection.html#check-port-on-windows)
