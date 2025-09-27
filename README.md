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

#### Who will be using the product?

HDD and microtunneling site engineersTBM operators who need continuous bearing of their machine, but lasers are too expensive, and other methods like IMU are not accurate enough and are failure-prone.

#### Who will be purchasing the product?

Trenchless contractors (HDD, MTBM, pipe jacking), municipal utility owners/EPCs, and rental fleets; college design teams (e.g., Penn Hyperloop) as pilots.

#### Where will you deploy it?

North America & EU urban/suburban jobs with ~1–3 m cover (street right-of-way, utility corridors).

First field test: Bastrop, TX.

#### How large is the market you’re targeting (USD)?

Beachhead market: Microtunneling (MTBM)

Recent reports place the global MTBM/microtunneling machine market roughly in the $1.1–1.5B (2024) range. Using a realistic 1–3% “instrumentation/guidance add-on” instead of a full rig, our TAM ≈ $11–45M per year. Regionally, microtunneling is concentrated in NA + EU ≈ ~60% of spend.

Expanded opportunity, after MTBM sucess

Adding adjacent install categories: HDD $9.90B (2024), Pipe Jacking ≈ $1.2B (2024), Horizontal Auger Boring ≈ $0.33–0.41B (2024–31). Summed with MTBM yields ≈ $12.5–13.0B of trenchless install equipment spend. Applying the same 1–3% attach rate ⇒ extended TAM ≈ $126–379M per year.

#### How much of that market do you expect to capture (USD)?

SAM: Use NA + EU ≈ 60% of the MTBM TAM ⇒ SAM ≈ $6.7–27.0M/yr. For all trenchless construction post pilot trials: $44–156M/yr

SOM: target 1–5% of SAM via direct sales + rentals ⇒ ≈ $0.07–1.35M/yr. For all trenchless construction post pilot trials: $0.44–7.8M/yr

#### What competitors are already in the space?

Microtunneling: VMT (TUnIS Navigation MT / TBM Laser) and related tunneling measurement systems. 

HDD guidance (adjacent/secondary market): Digital Control Inc. (DigiTrak), Underground Magnetics, Subsite Electronics—dominant in walkover locating; useful benchmark for pricing and features. 

### Stakeholders

Penn Hyperloop – immediate competition application in Not-A-Boring Competition (NABC) as a field deployment test.

Other Trenchless companies, including Robbins, Herrenknecht AG, or Komatsu.

Utilities Contractors and government agencies such as Progressive Pipeline Management, Philadelphia Water, Philadelphia Gas Works, or PennDOT.

The Boring Company (TBC) & other full size TBM constructors

#### Call with Cole Kenny, GNC engineer at TBC, host of NABC:



### System-Level Diagrams



### Security Requirements Specification

SR-01: All navigation packets over LoRa shall use AES-CCM with a per-packet nonce and monotonic counter.

SR-02: Gateway shall authenticate to MQTT broker before publishing.

SR-03: Logs shall be append-only and tamper-evident using hash chaining with synchronized gateway timestamps.

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
