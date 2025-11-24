"use client";

import { useState, useEffect } from "react";
import { useRouter } from "next/navigation";
import { TunnelBoringMachine } from "./components/TunnelBoringMachine";
import { useMqtt } from "./hooks/useMqtt";
import { useAuth } from "./contexts/AuthContext";

export default function Home() {
  const router = useRouter();
  const { user, signOut, loading: authLoading } = useAuth();
  const [position, setPosition] = useState(0);
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null);

  // Load configuration from environment variables
  const mqttUrl =
    process.env.NEXT_PUBLIC_MQTT_URL ||
    "c1412829227647ae9f57545fe534a511.s1.eu.hivemq.cloud";
  const mqttPort = process.env.NEXT_PUBLIC_MQTT_PORT
    ? parseInt(process.env.NEXT_PUBLIC_MQTT_PORT)
    : 8883;
  const mqttUsername = process.env.NEXT_PUBLIC_MQTT_USERNAME || "misogate";
  const mqttPassword = process.env.NEXT_PUBLIC_MQTT_PASSWORD || "";
  const topic = process.env.NEXT_PUBLIC_MQTT_TOPIC || "misogate/pub";

  // MQTT connection - only connect when user is logged in
  const mqttConnection = useMqtt(
    user
      ? {
          url: mqttUrl,
          port: mqttPort,
          username: mqttUsername,
          password: mqttPassword,
          protocol: "wss",
        }
      : undefined,
    user ? [topic] : [],
  );

  const { isConnected, messages, error } = mqttConnection;

  // Persist position - no auto-reset

  // Process incoming MQTT messages
  useEffect(() => {
    if (messages.length > 0) {
      const latestMessage = messages[messages.length - 1];
      console.log("Received message:", latestMessage);

      try {
        // Handle different message formats
        let newPosition = position;

        if (typeof latestMessage.payload === "object") {
          // Try common field names for position
          if ("position" in latestMessage.payload) {
            newPosition = parseInt(latestMessage.payload.position);
          } else if ("pos" in latestMessage.payload) {
            newPosition = parseInt(latestMessage.payload.pos);
          } else if ("value" in latestMessage.payload) {
            newPosition = parseInt(latestMessage.payload.value);
          }
        } else if (typeof latestMessage.payload === "number") {
          newPosition = latestMessage.payload;
        } else if (typeof latestMessage.payload === "string") {
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
        console.error("Error parsing message:", err);
      }
    }
  }, [messages]);

  const handleLogout = async () => {
    await signOut();
    router.push("/login");
  };

  return (
    <main className="min-h-screen bg-gradient-to-br from-gray-50 to-gray-100 dark:from-gray-900 dark:to-gray-800 p-8">
      <div className="max-w-6xl mx-auto">
        {/* Header */}
        <div className="mb-8">
          <div className="flex items-center justify-between mb-4">
            <div className="text-center flex-1">
              <h1 className="text-4xl font-bold text-gray-900 dark:text-white mb-2">
                MagNav Tunnel Monitor
              </h1>
              <p className="text-gray-600 dark:text-gray-400">
                Real-time micro tunnel boring machine position tracking
              </p>
            </div>
            <div className="flex items-center gap-4">
              {authLoading ? (
                <div className="text-gray-500 dark:text-gray-400">
                  Loading...
                </div>
              ) : user ? (
                <div className="flex items-center gap-4">
                  <div className="text-right">
                    <div className="text-sm font-semibold text-gray-900 dark:text-white">
                      {user.username}
                    </div>
                    {user.email && (
                      <div className="text-xs text-gray-600 dark:text-gray-400">
                        {user.email}
                      </div>
                    )}
                  </div>
                  <button
                    onClick={handleLogout}
                    className="px-4 py-2 bg-red-600 hover:bg-red-700 text-white rounded-lg transition-colors text-sm font-semibold"
                  >
                    Logout
                  </button>
                </div>
              ) : (
                <div className="flex gap-2">
                  <button
                    onClick={() => router.push("/login")}
                    className="px-4 py-2 bg-blue-600 hover:bg-blue-700 text-white rounded-lg transition-colors text-sm font-semibold"
                  >
                    Login
                  </button>
                  <button
                    onClick={() => router.push("/signup")}
                    className="px-4 py-2 bg-green-600 hover:bg-green-700 text-white rounded-lg transition-colors text-sm font-semibold"
                  >
                    Sign Up
                  </button>
                </div>
              )}
            </div>
          </div>
        </div>

        {/* Login Prompt or Content */}
        {authLoading ? (
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-12 text-center">
            <div className="text-gray-600 dark:text-gray-400">Loading...</div>
          </div>
        ) : !user ? (
          <div className="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-12 text-center">
            <div className="max-w-md mx-auto">
              <h2 className="text-2xl font-bold text-gray-900 dark:text-white mb-4">
                Authentication Required
              </h2>
              <p className="text-gray-600 dark:text-gray-400 mb-8">
                Please log in to access the tunnel monitoring system
              </p>
              <div className="flex gap-4 justify-center">
                <button
                  onClick={() => router.push("/login")}
                  className="px-6 py-3 bg-blue-600 hover:bg-blue-700 text-white rounded-lg transition-colors font-semibold"
                >
                  Log In
                </button>
                <button
                  onClick={() => router.push("/signup")}
                  className="px-6 py-3 bg-green-600 hover:bg-green-700 text-white rounded-lg transition-colors font-semibold"
                >
                  Sign Up
                </button>
              </div>
            </div>
          </div>
        ) : (
          <>
            {/* Connection Status */}
            <div className="bg-white dark:bg-gray-800 rounded-lg shadow-lg p-6 mb-8">
              <div className="flex items-center space-x-4">
                <div
                  className={`w-4 h-4 rounded-full ${
                    isConnected ? "bg-green-500 animate-pulse" : "bg-yellow-500"
                  }`}
                />
                <div>
                  <div className="font-semibold text-gray-900 dark:text-white">
                    {isConnected
                      ? "Connected to MQTT Broker"
                      : "Connecting to MQTT Broker..."}
                  </div>
                  <div className="text-sm text-gray-600 dark:text-gray-400">
                    {mqttUrl}:{mqttPort} - {topic}
                  </div>
                  {lastUpdate && (
                    <div className="text-xs text-gray-500 dark:text-gray-500">
                      Last update: {lastUpdate.toLocaleTimeString()}
                    </div>
                  )}
                </div>
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
                {messages
                  .slice(-10)
                  .reverse()
                  .map((msg, idx) => (
                    <div
                      key={idx}
                      className="p-3 bg-gray-50 dark:bg-gray-700 rounded text-sm font-mono"
                    >
                      <div className="text-xs text-gray-500 dark:text-gray-400 mb-1">
                        {new Date(msg.timestamp).toLocaleTimeString()} -{" "}
                        {msg.topic}
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
