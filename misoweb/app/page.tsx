'use client';

import { useState, useEffect } from 'react';
import { TunnelBoringMachine } from './components/TunnelBoringMachine';
import { useMqtt } from './hooks/useMqtt';
import { useAwsIotMqtt } from './hooks/useAwsIotMqtt';

export default function Home() {
  const [position, setPosition] = useState(0);
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null);
  const [endpoint, setEndpoint] = useState('a1nqctvl2kwinw-ats.iot.us-east-2.amazonaws.com');
  const [region, setRegion] = useState('us-east-2');
  const [identityPoolId, setIdentityPoolId] = useState('');
  const [topic, setTopic] = useState('misogate/pub');
  const [isConfigured, setIsConfigured] = useState(false);
  const [useAwsIot, setUseAwsIot] = useState(true);

  // AWS IoT connection (preferred)
  const awsIotConnection = useAwsIotMqtt(
    isConfigured && useAwsIot
      ? {
          endpoint,
          region,
          identityPoolId: identityPoolId || undefined,
        }
      : undefined,
    isConfigured && useAwsIot ? [topic] : []
  );

  // Fallback to simple MQTT connection
  const mqttConnection = useMqtt(
    isConfigured && !useAwsIot ? `wss://${endpoint}/mqtt` : undefined,
    isConfigured && !useAwsIot ? [topic] : []
  );

  // Use the appropriate connection
  const { isConnected, messages, error } = useAwsIot ? awsIotConnection : mqttConnection;

  // Persist position - no auto-reset

  // Process incoming MQTT messages
  useEffect(() => {
    if (messages.length > 0) {
      const latestMessage = messages[messages.length - 1];
      console.log('Received message:', latestMessage);

      try {
        // Handle different message formats
        let newPosition = position;

        if (typeof latestMessage.payload === 'object') {
          // Try common field names for position
          if ('position' in latestMessage.payload) {
            newPosition = parseInt(latestMessage.payload.position);
          } else if ('pos' in latestMessage.payload) {
            newPosition = parseInt(latestMessage.payload.pos);
          } else if ('value' in latestMessage.payload) {
            newPosition = parseInt(latestMessage.payload.value);
          }
        } else if (typeof latestMessage.payload === 'number') {
          newPosition = latestMessage.payload;
        } else if (typeof latestMessage.payload === 'string') {
          const parsed = parseInt(latestMessage.payload);
          if (!isNaN(parsed)) {
            newPosition = parsed;
          }
        }

        // Clamp position to 0-255 range
        newPosition = Math.max(0, Math.min(255, newPosition));

        if (newPosition !== position) {
          setPosition(newPosition);
          setLastUpdate(new Date());
        }
      } catch (err) {
        console.error('Error parsing message:', err);
      }
    }
  }, [messages]);

  const handleConnect = (e: React.FormEvent) => {
    e.preventDefault();
    if (endpoint && topic) {
      setIsConfigured(true);
    }
  };

  const handleDisconnect = () => {
    setIsConfigured(false);
  };

  return (
    <main className="min-h-screen bg-gradient-to-br from-gray-50 to-gray-100 dark:from-gray-900 dark:to-gray-800 p-8">
      <div className="max-w-6xl mx-auto">
        {/* Header */}
        <div className="mb-8 text-center">
          <h1 className="text-4xl font-bold text-gray-900 dark:text-white mb-2">
            MisoGate Tunnel Monitor
          </h1>
          <p className="text-gray-600 dark:text-gray-400">
            Real-time micro tunnel boring machine position tracking
          </p>
        </div>

        {/* Configuration Panel */}
        {!isConfigured ? (
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6 mb-8">
            <h2 className="text-xl font-semibold mb-4 text-gray-900 dark:text-white">
              AWS IoT Configuration
            </h2>
            <form onSubmit={handleConnect} className="space-y-4">
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  AWS IoT Endpoint
                </label>
                <input
                  type="text"
                  value={endpoint}
                  onChange={(e) => setEndpoint(e.target.value)}
                  placeholder="xxxxx-ats.iot.us-east-2.amazonaws.com"
                  className="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
                  required
                />
                <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  Your AWS IoT endpoint (without wss:// or /mqtt)
                </p>
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  AWS Region
                </label>
                <input
                  type="text"
                  value={region}
                  onChange={(e) => setRegion(e.target.value)}
                  placeholder="us-east-2"
                  className="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
                  required
                />
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  Cognito Identity Pool ID (Optional)
                </label>
                <input
                  type="text"
                  value={identityPoolId}
                  onChange={(e) => setIdentityPoolId(e.target.value)}
                  placeholder="us-east-2:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
                  className="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
                />
                <p className="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  Leave empty to connect without Cognito (requires custom authorizer or open policy)
                </p>
              </div>
              <div>
                <label className="block text-sm font-medium text-gray-700 dark:text-gray-300 mb-2">
                  Topic
                </label>
                <input
                  type="text"
                  value={topic}
                  onChange={(e) => setTopic(e.target.value)}
                  placeholder="misogate/pub"
                  className="w-full px-4 py-2 border border-gray-300 dark:border-gray-600 rounded-lg focus:ring-2 focus:ring-blue-500 dark:bg-gray-700 dark:text-white"
                  required
                />
              </div>
              <button
                type="submit"
                className="w-full bg-blue-600 hover:bg-blue-700 text-white font-semibold py-3 px-6 rounded-lg transition-colors"
              >
                Connect to AWS IoT
              </button>
            </form>
          </div>
        ) : (
          <>
            {/* Connection Status */}
            <div className="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6 mb-8">
              <div className="flex items-center justify-between">
                <div className="flex items-center space-x-4">
                  <div
                    className={`w-4 h-4 rounded-full ${
                      isConnected ? 'bg-green-500 animate-pulse' : 'bg-red-500'
                    }`}
                  />
                  <div>
                    <div className="font-semibold text-gray-900 dark:text-white">
                      {isConnected ? 'Connected' : 'Disconnected'}
                    </div>
                    <div className="text-sm text-gray-600 dark:text-gray-400">
                      {endpoint} - {topic}
                    </div>
                    {lastUpdate && (
                      <div className="text-xs text-gray-500 dark:text-gray-500">
                        Last update: {lastUpdate.toLocaleTimeString()}
                      </div>
                    )}
                  </div>
                </div>
                <button
                  onClick={handleDisconnect}
                  className="px-4 py-2 bg-red-600 hover:bg-red-700 text-white rounded-lg transition-colors"
                >
                  Disconnect
                </button>
              </div>
              {error && (
                <div className="mt-4 p-3 bg-red-100 dark:bg-red-900 text-red-700 dark:text-red-200 rounded-lg text-sm">
                  Error: {error}
                </div>
              )}
            </div>

            {/* TBM Visualization */}
            <div className="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-8">
              <TunnelBoringMachine position={position} />
            </div>

            {/* Message Log */}
            <div className="mt-8 bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6">
              <h3 className="text-lg font-semibold mb-4 text-gray-900 dark:text-white">
                Recent Messages ({messages.length})
              </h3>
              <div className="space-y-2 max-h-64 overflow-y-auto">
                {messages.slice(-10).reverse().map((msg, idx) => (
                  <div
                    key={idx}
                    className="p-3 bg-gray-50 dark:bg-gray-700 rounded text-sm font-mono"
                  >
                    <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">
                      {new Date(msg.timestamp).toLocaleTimeString()} - {msg.topic}
                    </div>
                    <div className="text-gray-900 dark:text-gray-100">
                      {JSON.stringify(msg.payload, null, 2)}
                    </div>
                  </div>
                ))}
                {messages.length === 0 && (
                  <div className="text-gray-500 dark:text-gray-400 text-center py-8">
                    Waiting for messages...
                  </div>
                )}
              </div>
            </div>
          </>
        )}
      </div>
    </main>
  );
}
