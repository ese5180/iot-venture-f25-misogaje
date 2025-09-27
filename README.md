[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/9GQ6o4cu)

# IoT Venture Pitch

## ESE5180: IoT Wireless, Security, & Scaling

**Team Name:**

| Team Member Name | Email Address             |
| ---------------- | ------------------------- |
| jefferson ding   | tyding@seas.upenn.edu     |
| sophia fu        | sophiafu@seas.upenn.edu   |
| gabriel zhang    | jgzhang@wharton.upenn.edu |
| mia wang         | wxm@seas.upenn.edu        |

**GitHub Repository URL:** https://github.com/ese5180/iot-venture-f25-misogaje

## Concept Development


### Product Function

The system provides continuous micro tunnel boring machine (TBM) navigation underground without relying on noisy internal IMUs or vulnerable electronics inside the machine, which also removes excessive wiring and single points of failure. Surface nodes detect the magnetic field of the TBM-mounted magnet, and the gateway triangulates position and heading using a dipole solver. This data is transmitted via LoRa from nodes to the gateway and then via Wi-Fi MQTT to operators and remote servers.

### Target Market & Demographics


### Stakeholders

Penn Hyperloop – immediate competition application in Not-A-Boring Competition as a field deployment test.

Other Microtunnelling companies, including Robbins, Herrenknecht AG, or Komatsu.

Municipal contractors / utilities – long-term potential users.

The Boring Company & other full size TBM constructors

### System-Level Diagrams

### Security Requirements Specification

### Hardware Requirements Specification

HR-01: TBM-mounted magnet (N52, ≥50×20 mm) shall generate ≥1 μT signal at 1.5 m depth.

HR-02: Surface nodes shall include Nordic nRF7002 with SAMD21Pro RF / Other Lora shield, MMC5983MA magnetometer, LoRa antenna, and IP65 housing.

HR-03: Gateway shall be an nRF7002 DK with Wi-Fi capability for MQTT uplink.

HR-04: System shall support ≥10 nodes with 0.1 Hz updates.

### Software Requirements Specification

SRS-01: Firmware shall run Zephyr RTOS on Nordic MCUs.

SRS-02: Nodes shall transmit magnetometer readings at 0.1–1 Hz via LoRa.

SRS-03: Gateway shall fuse ≥3 node readings into TBM position & heading using dipole solver + EKF.

SRS-04: Gateway shall publish telemetry via MQTT in JSON format required  (chainage, easting, northing, elevation, heading:

{ “team”: <string-formatted team name>, “timestamp”: <UNIX timestamp>, “mining”: <boolean mining flag>, “chainage”: <float-formatted chainage in m>, “easting”: <float-formatted easting in m>, “northing”: <float-formatted northing in m>, “elevation”: <float-formatted elevation in m>, “roll”: <float-formatted roll in radians>, “pitch”: <float-formatted pitch in radians>, “heading”: <float-formatted heading in radians>, “extra”: { “optionalSensor”: <data>, “otherOptionalSensor”: <data>, }
