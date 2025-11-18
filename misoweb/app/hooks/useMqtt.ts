'use client';

import { useEffect, useState, useCallback } from 'react';
import mqtt, { MqttClient } from 'mqtt';

interface MqttMessage {
  topic: string;
  payload: any;
  timestamp: number;
}

interface UseMqttReturn {
  client: MqttClient | null;
  isConnected: boolean;
  messages: MqttMessage[];
  error: string | null;
  publish: (topic: string, message: string) => void;
}

export function useMqtt(brokerUrl?: string, topics: string[] = []): UseMqttReturn {
  const [client, setClient] = useState<MqttClient | null>(null);
  const [isConnected, setIsConnected] = useState(false);
  const [messages, setMessages] = useState<MqttMessage[]>([]);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!brokerUrl) {
      setError('No broker URL provided');
      return;
    }

    // Create MQTT client
    const mqttClient = mqtt.connect(brokerUrl, {
      reconnectPeriod: 5000,
      connectTimeout: 30000,
    });

    mqttClient.on('connect', () => {
      console.log('Connected to MQTT broker');
      setIsConnected(true);
      setError(null);

      // Subscribe to topics
      topics.forEach((topic) => {
        mqttClient.subscribe(topic, (err) => {
          if (err) {
            console.error(`Failed to subscribe to ${topic}:`, err);
            setError(`Failed to subscribe to ${topic}`);
          } else {
            console.log(`Subscribed to ${topic}`);
          }
        });
      });
    });

    mqttClient.on('error', (err) => {
      console.error('MQTT error:', err);
      setError(err.message);
      setIsConnected(false);
    });

    mqttClient.on('close', () => {
      console.log('MQTT connection closed');
      setIsConnected(false);
    });

    mqttClient.on('message', (topic, payload) => {
      try {
        const message = payload.toString();
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
    });

    setClient(mqttClient);

    return () => {
      if (mqttClient) {
        mqttClient.end();
      }
    };
  }, [brokerUrl, topics.join(',')]);

  const publish = useCallback(
    (topic: string, message: string) => {
      if (client && isConnected) {
        client.publish(topic, message, (err) => {
          if (err) {
            console.error('Failed to publish:', err);
            setError('Failed to publish message');
          }
        });
      }
    },
    [client, isConnected]
  );

  return {
    client,
    isConnected,
    messages,
    error,
    publish,
  };
}
