# MagNav Tunnel Monitor

A real-time web dashboard for monitoring a micro tunnel boring machine (TBM) connected via AWS IoT MQTT.

## Features

- **Real-time AWS IoT MQTT connection** with WebSocket support
- **Animated tunnel boring machine visualization** with rotating drill head
- **Position tracking** (0-255 scale) with smooth transitions
- **Auto-reset to position 0** when disconnected or no data received for 30 seconds
- **Progress indicators** showing completion percentage and distance
- **Message log** for debugging incoming MQTT data
- **Dark mode support**
- **Flexible message parsing** supporting multiple data formats

## Quick Start

### 1. Install Dependencies

```bash
bun install
```

### 2. Run Development Server

```bash
bun run dev
```

### 3. Open the Application

Open [http://localhost:3000](http://localhost:3000) in your browser.

### 4. Configure AWS IoT Connection

The application comes pre-configured with:

- **Endpoint**: `a1nqctvl2kwinw-ats.iot.us-east-2.amazonaws.com`
- **Region**: `us-east-2`
- **Topic**: `misogate/pub`

Simply click "Connect to AWS IoT" to start (see [AWS IoT Setup](#aws-iot-setup) for authentication configuration).

## AWS IoT Setup

For detailed AWS IoT configuration instructions, see [AWS_IOT_SETUP.md](./AWS_IOT_SETUP.md).

### Quick Summary:

1. **Without Cognito** (testing only): Leave "Cognito Identity Pool ID" empty
2. **With Cognito** (recommended): Create a Cognito Identity Pool and enter the ID

Test the connection by publishing a message:

```bash
aws iot-data publish \
  --topic "misogate/pub" \
  --payload '{"position": 128}' \
  --region us-east-2
```

## Message Format

The application accepts position data in multiple formats:

### JSON Format (Preferred)

```json
{
  "position": 128
}
```

### Alternative JSON Fields

```json
{"pos": 128}
{"value": 128}
```

### Plain Number

```
128
```

Position values should be between **0-255**.

## Architecture

### Components

- **`app/page.tsx`**: Main dashboard with MQTT configuration and TBM visualization
- **`app/components/TunnelBoringMachine.tsx`**: Animated TBM visualization component
- **`app/hooks/useMqtt.ts`**: Generic MQTT client hook (WebSocket)
- **`app/hooks/useAwsIotMqtt.ts`**: AWS IoT specific MQTT hook with authentication

### Key Features

#### Auto-Reset Position

- Position automatically resets to 0 when:
  - Disconnected from MQTT broker
  - No data received for 30 seconds
  - Connection is lost

#### Smooth Animations

- Position changes are smoothly animated
- Rotating drill head with realistic motion
- Progress bar transitions

#### Flexible Authentication

- Supports AWS Cognito Identity Pool
- Supports direct connection (with custom authorizer)
- Falls back to simple WebSocket MQTT if needed

## Development

### Project Structure

```
misoweb/
├── app/
│   ├── components/
│   │   └── TunnelBoringMachine.tsx   # TBM visualization
│   ├── hooks/
│   │   ├── useMqtt.ts                # Generic MQTT client
│   │   └── useAwsIotMqtt.ts          # AWS IoT MQTT client
│   ├── page.tsx                       # Main dashboard
│   ├── layout.tsx                     # App layout
│   └── globals.css                    # Global styles
├── public/                            # Static assets
├── AWS_IOT_SETUP.md                   # AWS IoT setup guide
├── SETUP.md                           # General setup guide
└── package.json
```

### Built With

- [Next.js 14](https://nextjs.org/) - React framework
- [TypeScript](https://www.typescriptlang.org/) - Type safety
- [Tailwind CSS](https://tailwindcss.com/) - Styling
- [mqtt.js](https://github.com/mqttjs/MQTT.js) - MQTT client
- [AWS IoT Device SDK v2](https://github.com/aws/aws-iot-device-sdk-js-v2) - AWS IoT integration
- [AWS SDK](https://aws.amazon.com/sdk-for-javascript/) - AWS services

### Building for Production

```bash
bun run build
bun run start
```

## Firmware Integration

Your MisoGate firmware should publish position data to `misogate/pub` topic.

Example in Zephyr (C):

```c
#include "mqtt/mqtt.h"

// Publish position data
int position = 128; // 0-255
char message[32];
snprintf(message, sizeof(message), "{\"position\": %d}", position);
mqtt_publish_json(message, strlen(message), MQTT_QOS_0_AT_MOST_ONCE);
```

## Troubleshooting

### Connection Issues

1. **Check endpoint format**: Should be hostname only (no `wss://` or `/mqtt`)
2. **Verify AWS IoT policy**: Ensure it allows `iot:Connect`, `iot:Subscribe`, `iot:Receive`
3. **Check browser console**: Look for detailed error messages
4. **Test with AWS CLI**: Verify your AWS IoT setup with manual publish

### No Data Appearing

1. **Check message log**: Scroll to bottom of dashboard to see received messages
2. **Verify topic name**: Ensure firmware publishes to `misogate/pub`
3. **Check message format**: Must include a "position" field or be a plain number
4. **Test publishing**: Use AWS CLI to publish test messages

### Position Not Updating

1. **Verify value range**: Position must be between 0-255
2. **Check console logs**: Open browser DevTools to see parsing errors
3. **Inspect messages**: Look at the raw message in the message log

## License

This project is part of the ESE 5180 IoT Venture course at the University of Pennsylvania.

## Support

For issues and questions:

- Check [AWS_IOT_SETUP.md](./AWS_IOT_SETUP.md) for AWS IoT configuration
- Check [SETUP.md](./SETUP.md) for general setup information
- Review browser console for error messages
- Verify firmware is publishing to the correct topic
