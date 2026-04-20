# esp-main-sensors-board

This project is a part of combined system with another [ESP-WHO project](https://github.com/AlexKar0706/esp-who).
ESP32S3 programmed to gather distance from 3 different sensors: ToF sensor (VL53L1X), ToF sensor (VL53L1X), ultrasonic sensor (HC-SR04) and IR sensor (GP2Y0A41SK0F) with is implemented in components/sensors
To communicate with another sensor, ESP32 utilize ESP-NOW protocol.
To transmit gatherd data, ESP32 utilize HTTP to which user can be connected via configured Wifi SoftAP to make "/data" GET-request.
