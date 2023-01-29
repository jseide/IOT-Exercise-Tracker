# Prototype Workout Tracker

This project uses a Nordic NRF52840DK and a Bosch BNO055 IMU to create a small workout tracker 
that can be attached to any free weight. The tracker detects when the user is exercising via 
an onboard TFLite for Micro model, and streams data to Firestore via GCP IOT for further 
analysis such as workout type recognition and rep counting. 

Network connectivity is based on COAP over DTLS on top of Openthread, and a COAP/HTTPS Proxy.

## Hardware Requirements

[Nordic nrf52840dk SOC](https://www.nordicsemi.com/Products/Development-hardware/nrf52840-dk)  
[Bosch BNO055 IMU](https://www.bosch-sensortec.com/products/smart-sensors/bno055/)  
Openthread Border Router (see [DIY version](https://openthread.io/guides/border-router))  

## Setup
Build requires the [NRFConnect SDK and West](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/introduction.html)  
[GCP IOT Core Backend with HTTPS/COAP Proxy](https://infocenter.nordicsemi.com/index.jsp?topic=%2Fstruct_sdk%2Fstruct%2Fsdk_nrf5_latest.html)  

## Build

```bash
west build -b nrf52840dk_nrf52840
```

## Citations

This project was inspired by the [TinyML Magic Wand](https://github.com/tensorflow/tflite-micro/tree/main/tensorflow/lite/micro/examples/magic_wand) 
example showcased in the TFLite for Microcontrollers repository.  It builds upon that example by 
integrating with GCP IOT Core via COAP over DTLS on top of Openthread.
