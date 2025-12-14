"use client";

import { useState, useEffect, useRef } from "react";
import { useRouter } from "next/navigation";
import { LocationTracker } from "./components/LocationTracker";
import { useMqtt } from "./hooks/useMqtt";
import { useAuth } from "./contexts/AuthContext";

interface Point {
  x: number;
  y: number;
  timestamp: number;
}

export default function Home() {
  const router = useRouter();
  const { user, signOut, loading: authLoading } = useAuth();
  const [currentPosition, setCurrentPosition] = useState<{
    x: number;
    y: number;
  } | null>(null);
  const [positionHistory, setPositionHistory] = useState<Point[]>([]);
  const [lastUpdate, setLastUpdate] = useState<Date | null>(null);

  // Load configuration from environment variables
  const mqttUrl =
    process.env.NEXT_PUBLIC_MQTT_URL ||
    "c1412829227647ae9f57545fe534a511.s1.eu.hivemq.cloud";
  const mqttPort = process.env.NEXT_PUBLIC_MQTT_PORT
    ? parseInt(process.env.NEXT_PUBLIC_MQTT_PORT)
    : 8884;
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
    user ? [topic] : []
  );

  const { isConnected, messages, error } = mqttConnection;

  // Process incoming MQTT messages
  useEffect(() => {
    if (messages.length > 0) {
      const latestMessage = messages[messages.length - 1];
      console.log("Received message:", latestMessage);

      try {
        // Handle {x, y} format where x and y are 0-1000
        if (
          typeof latestMessage.payload === "object" &&
          "x" in latestMessage.payload &&
          "y" in latestMessage.payload
        ) {
          const x = Math.max(
            0,
            Math.min(1000, parseFloat(latestMessage.payload.x))
          );
          const y = Math.max(
            0,
            Math.min(1000, parseFloat(latestMessage.payload.y))
          );

          if (!isNaN(x) && !isNaN(y)) {
            const newPoint: Point = {
              x,
              y,
              timestamp: Date.now(),
            };

            setCurrentPosition({ x, y });
            setPositionHistory((prev) => [...prev.slice(-499), newPoint]); // Keep last 500 points
            setLastUpdate(new Date());
          }
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

  const handleClearHistory = () => {
    setPositionHistory([]);
  };

  return (
    <main className="min-h-screen bg-gradient-to-br from-slate-950 via-slate-900 to-slate-950 p-6 lg:p-8">
      {/* Subtle background pattern */}
      <div
        className="fixed inset-0 pointer-events-none opacity-30"
        style={{
          backgroundImage: `radial-gradient(circle at 25% 25%, rgba(59, 130, 246, 0.1) 0%, transparent 50%),
                           radial-gradient(circle at 75% 75%, rgba(139, 92, 246, 0.1) 0%, transparent 50%)`,
        }}
      />

      <div className="max-w-6xl mx-auto relative">
        {/* Header */}
        <div className="mb-8">
          <div className="flex items-center justify-between mb-4">
            <div className="flex-1">
              <h1
                className="text-3xl lg:text-4xl font-bold text-white mb-2 tracking-tight"
                style={{
                  fontFamily:
                    '"SF Pro Display", "Segoe UI", system-ui, sans-serif',
                }}
              >
                <span className="bg-gradient-to-r from-emerald-400 via-cyan-400 to-blue-500 bg-clip-text text-transparent">
                  MagNav
                </span>{" "}
                Tunnel Monitor
              </h1>
              <p className="text-slate-400 text-sm lg:text-base">
                Real-time position tracking system
              </p>
            </div>
            <div className="flex items-center gap-4">
              {authLoading ? (
                <div className="text-slate-400">Loading...</div>
              ) : user ? (
                <div className="flex items-center gap-4">
                  <div className="text-right hidden sm:block">
                    <div className="text-sm font-semibold text-white">
                      {user.username}
                    </div>
                    {user.email && (
                      <div className="text-xs text-slate-400">{user.email}</div>
                    )}
                  </div>
                  <button
                    onClick={handleLogout}
                    className="px-4 py-2 bg-red-500/20 hover:bg-red-500/30 text-red-400 border border-red-500/30 rounded-lg transition-all text-sm font-medium"
                  >
                    Logout
                  </button>
                </div>
              ) : (
                <div className="flex gap-2">
                  <button
                    onClick={() => router.push("/login")}
                    className="px-4 py-2 bg-cyan-500/20 hover:bg-cyan-500/30 text-cyan-400 border border-cyan-500/30 rounded-lg transition-all text-sm font-medium"
                  >
                    Login
                  </button>
                  <button
                    onClick={() => router.push("/signup")}
                    className="px-4 py-2 bg-emerald-500/20 hover:bg-emerald-500/30 text-emerald-400 border border-emerald-500/30 rounded-lg transition-all text-sm font-medium"
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
          <div className="bg-slate-800/50 backdrop-blur-sm rounded-2xl border border-slate-700/50 p-12 text-center">
            <div className="w-8 h-8 border-2 border-cyan-500 border-t-transparent rounded-full animate-spin mx-auto mb-4"></div>
            <div className="text-slate-400">Loading...</div>
          </div>
        ) : !user ? (
          <div className="bg-slate-800/50 backdrop-blur-sm rounded-2xl border border-slate-700/50 p-12 text-center">
            <div className="max-w-md mx-auto">
              <div className="w-16 h-16 bg-gradient-to-br from-cyan-500 to-emerald-500 rounded-2xl mx-auto mb-6 flex items-center justify-center">
                <svg
                  className="w-8 h-8 text-white"
                  fill="none"
                  viewBox="0 0 24 24"
                  stroke="currentColor"
                >
                  <path
                    strokeLinecap="round"
                    strokeLinejoin="round"
                    strokeWidth={2}
                    d="M12 15v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2zm10-10V7a4 4 0 00-8 0v4h8z"
                  />
                </svg>
              </div>
              <h2 className="text-2xl font-bold text-white mb-4">
                Authentication Required
              </h2>
              <p className="text-slate-400 mb-8">
                Please log in to access the tunnel monitoring system
              </p>
              <div className="flex gap-4 justify-center">
                <button
                  onClick={() => router.push("/login")}
                  className="px-6 py-3 bg-gradient-to-r from-cyan-500 to-blue-500 hover:from-cyan-600 hover:to-blue-600 text-white rounded-xl transition-all font-semibold shadow-lg shadow-cyan-500/20"
                >
                  Log In
                </button>
                <button
                  onClick={() => router.push("/signup")}
                  className="px-6 py-3 bg-slate-700 hover:bg-slate-600 text-white rounded-xl transition-all font-semibold border border-slate-600"
                >
                  Sign Up
                </button>
              </div>
            </div>
          </div>
        ) : (
          <>
            {/* Connection Status */}
            <div className="bg-slate-800/50 backdrop-blur-sm rounded-xl border border-slate-700/50 p-4 mb-6">
              <div className="flex items-center justify-between flex-wrap gap-4">
                <div className="flex items-center gap-3">
                  <div className="relative">
                    <div
                      className={`w-3 h-3 rounded-full ${
                        isConnected
                          ? "bg-emerald-500 shadow-lg shadow-emerald-500/50"
                          : "bg-amber-500 shadow-lg shadow-amber-500/50 animate-pulse"
                      }`}
                    />
                    {isConnected && (
                      <div className="absolute inset-0 w-3 h-3 rounded-full bg-emerald-500 animate-ping opacity-50" />
                    )}
                  </div>
                  <div>
                    <div className="font-medium text-white text-sm">
                      {isConnected
                        ? "Connected to MQTT Broker"
                        : "Connecting..."}
                    </div>
                    <div className="text-xs text-slate-500 font-mono">
                      {mqttUrl}:{mqttPort}
                    </div>
                  </div>
                </div>
                <div className="flex items-center gap-4 text-xs">
                  <div className="text-slate-400">
                    Topic:{" "}
                    <span className="text-cyan-400 font-mono">{topic}</span>
                  </div>
                  {lastUpdate && (
                    <div className="text-slate-500">
                      Last: {lastUpdate.toLocaleTimeString()}
                    </div>
                  )}
                  {positionHistory.length > 0 && (
                    <button
                      onClick={handleClearHistory}
                      className="px-3 py-1 bg-slate-700/50 hover:bg-slate-600/50 text-slate-400 rounded-lg transition-all text-xs border border-slate-600/50"
                    >
                      Clear Path
                    </button>
                  )}
                </div>
              </div>
              {error && (
                <div className="mt-3 p-3 bg-red-500/10 border border-red-500/20 text-red-400 rounded-lg text-sm">
                  Error: {error}
                </div>
              )}
            </div>

            {/* Location Tracker Visualization */}
            <div className="bg-slate-800/50 backdrop-blur-sm rounded-2xl border border-slate-700/50 p-6 lg:p-8">
              <LocationTracker
                currentPosition={currentPosition}
                history={positionHistory}
                onClearCanvas={handleClearHistory}
              />
            </div>

            {/* Message Log */}
            <div className="mt-6 bg-slate-800/50 backdrop-blur-sm rounded-xl border border-slate-700/50 p-5">
              <div className="flex items-center justify-between mb-4">
                <h3 className="text-sm font-semibold text-white uppercase tracking-wider">
                  Recent Messages
                </h3>
                <span className="text-xs text-slate-500 font-mono">
                  {messages.length} total
                </span>
              </div>
              <div className="space-y-2 max-h-48 overflow-y-auto custom-scrollbar">
                {messages
                  .slice(-8)
                  .reverse()
                  .map((msg, idx) => (
                    <div
                      key={idx}
                      className="p-3 bg-slate-900/50 rounded-lg border border-slate-700/30"
                    >
                      <div className="flex items-center justify-between mb-1">
                        <span className="text-xs text-slate-500 font-mono">
                          {msg.topic}
                        </span>
                        <span className="text-xs text-slate-600">
                          {new Date(msg.timestamp).toLocaleTimeString()}
                        </span>
                      </div>
                      <div className="text-sm font-mono text-cyan-400">
                        {JSON.stringify(msg.payload)}
                      </div>
                    </div>
                  ))}
                {messages.length === 0 && (
                  <div className="text-slate-500 text-center py-8 text-sm">
                    <div className="w-12 h-12 border-2 border-dashed border-slate-700 rounded-full mx-auto mb-3 flex items-center justify-center">
                      <svg
                        className="w-6 h-6 text-slate-600"
                        fill="none"
                        viewBox="0 0 24 24"
                        stroke="currentColor"
                      >
                        <path
                          strokeLinecap="round"
                          strokeLinejoin="round"
                          strokeWidth={2}
                          d="M8 12h.01M12 12h.01M16 12h.01M21 12c0 4.418-4.03 8-9 8a9.863 9.863 0 01-4.255-.949L3 20l1.395-3.72C3.512 15.042 3 13.574 3 12c0-4.418 4.03-8 9-8s9 3.582 9 8z"
                        />
                      </svg>
                    </div>
                    Waiting for messages...
                  </div>
                )}
              </div>
            </div>
          </>
        )}
      </div>

      {/* Custom scrollbar styles */}
      <style jsx global>{`
        .custom-scrollbar::-webkit-scrollbar {
          width: 6px;
        }
        .custom-scrollbar::-webkit-scrollbar-track {
          background: rgba(30, 41, 59, 0.5);
          border-radius: 3px;
        }
        .custom-scrollbar::-webkit-scrollbar-thumb {
          background: rgba(71, 85, 105, 0.8);
          border-radius: 3px;
        }
        .custom-scrollbar::-webkit-scrollbar-thumb:hover {
          background: rgba(100, 116, 139, 0.8);
        }
      `}</style>
    </main>
  );
}
