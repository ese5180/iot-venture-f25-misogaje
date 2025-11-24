# MagNav Tunnel Monitor

A real-time web dashboard for monitoring a micro tunnel boring machine (TBM) connected via MQTT (HiveMQ Cloud).

## Features

- **Real-time MQTT connection** with secure WebSocket support (WSS)
- **Animated tunnel boring machine visualization** with rotating drill head
- **Position tracking** (0-255 scale) with smooth transitions
- **User authentication** with AWS Cognito
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

### 4. Configure MQTT Connection

The application comes pre-configured with HiveMQ Cloud credentials:

- **Broker**: `c1412829227647ae9f57545fe534a511.s1.eu.hivemq.cloud`
- **Port**: `8883`
- **Protocol**: `wss` (WebSocket Secure)
- **Topic**: `misogate/pub`

To customize these settings, create a `.env.local` file (see `.env.example`) or the app will use the default values.

Log in with your AWS Cognito credentials to start receiving MQTT messages.

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
- **`app/hooks/useMqtt.ts`**: MQTT client hook with authentication support
- **`app/contexts/AuthContext.tsx`**: AWS Cognito authentication context

### Key Features

#### Smooth Animations

- Position changes are smoothly animated
- Rotating drill head with realistic motion
- Progress bar transitions

#### Authentication

- AWS Cognito for user authentication
- MQTT connection only active when user is logged in
- Username/password authentication for MQTT broker

## Development

### Project Structure

```
misoweb/
├── app/
│   ├── components/
│   │   └── TunnelBoringMachine.tsx   # TBM visualization
│   ├── contexts/
│   │   └── AuthContext.tsx           # Authentication context
│   ├── hooks/
│   │   └── useMqtt.ts                # MQTT client with auth
│   ├── page.tsx                       # Main dashboard
│   ├── layout.tsx                     # App layout
│   └── globals.css                    # Global styles
├── public/                            # Static assets
├── .env.example                       # Environment variables template
└── package.json
```

### Built With

- [Next.js 14](https://nextjs.org/) - React framework
- [TypeScript](https://www.typescriptlang.org/) - Type safety
- [Tailwind CSS](https://tailwindcss.com/) - Styling
- [mqtt.js](https://github.com/mqttjs/MQTT.js) - MQTT client
- [AWS Cognito](https://aws.amazon.com/cognito/) - User authentication
- [HiveMQ Cloud](https://www.hivemq.com/mqtt-cloud-broker/) - MQTT broker

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

1. **Verify credentials**: Check that MQTT username and password are correct in `.env.local`
2. **Check broker URL**: Ensure the HiveMQ Cloud URL is accessible
3. **Check browser console**: Look for detailed error messages
4. **Verify authentication**: Make sure you're logged in with AWS Cognito

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

- Check `.env.example` for environment variable configuration
- Review browser console for error messages
- Verify firmware is publishing to the correct topic
- Check HiveMQ Cloud dashboard for connection status
