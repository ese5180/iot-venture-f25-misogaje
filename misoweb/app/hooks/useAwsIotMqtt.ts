'use client';

import { useEffect, useState, useCallback } from 'react';
import { mqtt, iot, auth } from 'aws-iot-device-sdk-v2';
import { fromCognitoIdentityPool } from '@aws-sdk/credential-providers';

interface MqttMessage {
  topic: string;
  payload: any;
  timestamp: number;
}

interface UseAwsIotMqttReturn {
  isConnected: boolean;
  messages: MqttMessage[];
  error: string | null;
  publish: (topic: string, message: string) => void;
}

interface AwsIotConfig {
  endpoint: string;
  region: string;
  identityPoolId?: string;
  clientId?: string;
}

export function useAwsIotMqtt(
  config: AwsIotConfig | undefined,
  topics: string[] = []
): UseAwsIotMqttReturn {
  const [connection, setConnection] = useState<mqtt.MqttClientConnection | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [messages, setMessages] = useState<MqttMessage[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!config) {
      return;
    }

    let mqttConnection: mqtt.MqttClientConnection | null = null;

    const connectToAwsIot = async () => {
      try {
        console.log('Connecting to AWS IoT...', config);

        // Create MQTT connection configuration
        const clientId = config.clientId || `misoweb-${Math.random().toString(36).substring(7)}`;

        // For AWS IoT WebSocket with Cognito Identity Pool
        if (config.identityPoolId) {
          const credentialProvider = fromCognitoIdentityPool({
            clientConfig: { region: config.region },
            identityPoolId: config.identityPoolId,
          });

          const awsCredentials = await credentialProvider();

          const configBuilder = iot.AwsIotMqttConnectionConfigBuilder.new_builder_for_websocket()
            .with_clean_session(true)
            .with_client_id(clientId)
            .with_endpoint(config.endpoint)
            .with_credentials(
              config.region,
              awsCredentials.accessKeyId,
              awsCredentials.secretAccessKey,
              awsCredentials.sessionToken
            );

          const mqttConfig = configBuilder.build();
          const client = new mqtt.MqttClient();
          mqttConnection = client.new_connection(mqttConfig);
        } else {
          // Simple WebSocket connection (requires AWS IoT Custom Authorizer or open policy)
          const configBuilder = iot.AwsIotMqttConnectionConfigBuilder.new_builder_for_websocket()
            .with_clean_session(true)
            .with_client_id(clientId)
            .with_endpoint(config.endpoint);

          const mqttConfig = configBuilder.build();
          const client = new mqtt.MqttClient();
          mqttConnection = client.new_connection(mqttConfig);
        }

        // Set up connection event handlers
        mqttConnection.on('connect', async () => {
          console.log('Connected to AWS IoT');
          setIsConnected(true);
          setError(null);

          // Subscribe to topics
          for (const topic of topics) {
            try {
              await mqttConnection!.subscribe(
                topic,
                mqtt.QoS.AtLeastOnce,
                (topic: string, payload: ArrayBuffer) => {
                  try {
                    const decoder = new TextDecoder('utf-8');
                    const message = decoder.decode(payload);
                    let parsedPayload;

                    try {
                      parsedPayload = JSON.parse(message);
                    } catch {
                      parsedPayload = message;
                    }

                    const newMessage: MqttMessage = {
                      topic,
                      payload: parsedPayload,
                      timestamp: Date.now(),
                    };

                    setMessages((prev) => [...prev.slice(-99), newMessage]);
                  } catch (err) {
                    console.error('Error processing message:', err);
                  }
                }
              );
              console.log(`Subscribed to ${topic}`);
            } catch (err) {
              console.error(`Failed to subscribe to ${topic}:`, err);
              setError(`Failed to subscribe to ${topic}`);
            }
          }
        });

        mqttConnection.on('error', (error: Error) => {
          console.error('AWS IoT connection error:', error);
          setError(error.message);
          setIsConnected(false);
        });

        mqttConnection.on('disconnect', () => {
          console.log('Disconnected from AWS IoT');
          setIsConnected(false);
        });

        mqttConnection.on('closed', () => {
          console.log('AWS IoT connection closed');
          setIsConnected(false);
        });

        // Connect
        await mqttConnection.connect();
        setConnection(mqttConnection);
      } catch (err: any) {
        console.error('Failed to connect to AWS IoT:', err);
        setError(err.message || 'Failed to connect to AWS IoT');
        setIsConnected(false);
      }
    };

    connectToAwsIot();

    return () => {
      if (mqttConnection) {
        mqttConnection.disconnect();
      }
    };
  }, [config?.endpoint, config?.region, config?.identityPoolId, topics.join(',')]);

  const publish = useCallback(
    async (topic: string, message: string) => {
      if (connection && isConnected) {
        try {
          await connection.publish(topic, message, mqtt.QoS.AtLeastOnce);
        } catch (err: any) {
          console.error('Failed to publish:', err);
          setError('Failed to publish message');
        }
      }
    },
    [connection, isConnected]
  );

  return {
    isConnected,
    messages,
    error,
    publish,
  };
}
