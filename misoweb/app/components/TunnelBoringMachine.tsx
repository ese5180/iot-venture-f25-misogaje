'use client';

import React, { useEffect, useState } from 'react';

interface TunnelBoringMachineProps {
  position: number; // 0-255
  className?: string;
}

export function TunnelBoringMachine({ position, className = '' }: TunnelBoringMachineProps) {
  const [animatedPosition, setAnimatedPosition] = useState(position);
  const [drillRotation, setDrillRotation] = useState(0);

  // Smooth animation of position changes
  useEffect(() => {
    const interval = setInterval(() => {
      setAnimatedPosition((prev) => {
        const diff = position - prev;
        if (Math.abs(diff) < 0.5) return position;
        return prev + diff * 0.15; // Faster animation
      });
    }, 16);

    return () => clearInterval(interval);
  }, [position]);

  // Continuous drill rotation
  useEffect(() => {
    const interval = setInterval(() => {
      setDrillRotation((prev) => (prev + 8) % 360);
    }, 16);

    return () => clearInterval(interval);
  }, []);

  // Convert position (0-255) to percentage (0-100)
  const percentage = (animatedPosition / 255) * 100;
  const totalDistance = 102; // Total tunnel length in meters (255 * 0.4)
  const excavated = Math.round(animatedPosition * 0.4);
  const remaining = totalDistance - excavated;

  return (
    <div className={`w-full ${className}`}>
      {/* Header with position info */}
      <div className="mb-6 flex justify-between items-center">
        <div>
          <h2 className="text-3xl font-bold text-gray-800 dark:text-gray-100">
            Tunnel Boring Machine
          </h2>
          <p className="text-lg text-gray-600 dark:text-gray-400 mt-1">
            Progress: {percentage.toFixed(1)}%
          </p>
        </div>
        <div className="text-right">
          <div className="text-5xl font-mono font-bold text-blue-600 dark:text-blue-400">
            {percentage.toFixed(1)}%
          </div>
          <div className="text-sm text-gray-500 mt-1">Complete</div>
        </div>
      </div>

      {/* Tunnel visualization */}
      <div className="relative w-full bg-gradient-to-b from-gray-700 via-gray-800 to-gray-900 rounded-2xl p-10 shadow-2xl">
        {/* Tunnel walls with better depth effect */}
        <div className="relative w-full h-40 bg-gradient-to-b from-gray-600 via-gray-700 to-gray-800 rounded-full overflow-hidden border-4 border-gray-900 shadow-inner">
          {/* Tunnel segments with perspective effect */}
          <div className="absolute inset-0 flex items-center">
            {[...Array(20)].map((_, i) => (
              <div
                key={i}
                className="flex-1 h-full border-l border-gray-500"
                style={{
                  opacity: 0.1 + (i / 20) * 0.3,
                  borderWidth: i % 2 === 0 ? '2px' : '1px'
                }}
              />
            ))}
          </div>

          {/* Excavated debris particles */}
          {position > 0 && (
            <div className="absolute inset-0 pointer-events-none">
              {[...Array(8)].map((_, i) => (
                <div
                  key={i}
                  className="absolute w-1 h-1 bg-orange-400 rounded-full animate-pulse"
                  style={{
                    left: `${Math.random() * percentage}%`,
                    top: `${30 + Math.random() * 40}%`,
                    opacity: 0.3 + Math.random() * 0.4,
                    animationDelay: `${i * 0.2}s`,
                  }}
                />
              ))}
            </div>
          )}

          {/* Progress track with glow effect */}
          <div className="absolute inset-0 flex items-center px-6">
            <div className="relative w-full h-3">
              {/* Completed track with glow */}
              <div
                className="absolute left-0 h-full bg-gradient-to-r from-green-400 via-blue-500 to-blue-600 rounded-full transition-all duration-500 ease-out shadow-lg"
                style={{
                  width: `${percentage}%`,
                  boxShadow: '0 0 20px rgba(59, 130, 246, 0.5)'
                }}
              />
              {/* Full track background */}
              <div className="absolute inset-0 bg-gray-500 opacity-20 rounded-full" />
            </div>
          </div>

          {/* TBM Machine with improved design */}
          <div
            className="absolute top-1/2 -translate-y-1/2 transition-all duration-500 ease-out z-10"
            style={{ left: `calc(${percentage}% - 40px)` }}
          >
            {/* Machine body */}
            <div className="relative">
              {/* Drill head with custom rotation */}
              <div className="absolute -left-6 top-1/2 -translate-y-1/2">
                <div
                  className="w-20 h-20 rounded-full bg-gradient-to-br from-yellow-400 via-orange-500 to-red-600 shadow-2xl relative"
                  style={{ transform: `rotate(${drillRotation}deg)` }}
                >
                  {/* Outer drill teeth - larger */}
                  {[...Array(12)].map((_, i) => (
                    <div
                      key={`outer-${i}`}
                      className="absolute w-3 h-3 bg-gray-900 rounded-sm"
                      style={{
                        top: '50%',
                        left: '50%',
                        transform: `translate(-50%, -50%) rotate(${i * 30}deg) translateY(-30px)`,
                        boxShadow: '0 0 5px rgba(0,0,0,0.5)'
                      }}
                    />
                  ))}
                  {/* Inner drill teeth */}
                  {[...Array(8)].map((_, i) => (
                    <div
                      key={`inner-${i}`}
                      className="absolute w-2 h-2 bg-gray-800 rounded-full"
                      style={{
                        top: '50%',
                        left: '50%',
                        transform: `translate(-50%, -50%) rotate(${i * 45}deg) translateY(-20px)`,
                      }}
                    />
                  ))}
                  {/* Center hub with metallic effect */}
                  <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-8 h-8 bg-gradient-to-br from-gray-700 to-gray-900 rounded-full border-3 border-yellow-600 shadow-inner" />
                  {/* Center bolt */}
                  <div className="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 w-3 h-3 bg-gray-900 rounded-full" />
                </div>
              </div>

              {/* Machine body with more detail */}
              <div className="ml-10 w-40 h-16 bg-gradient-to-r from-blue-700 via-blue-600 to-blue-800 rounded-r-xl shadow-2xl border-3 border-blue-900 relative">
                {/* Rivets */}
                <div className="absolute inset-2">
                  {[...Array(3)].map((_, row) => (
                    <div key={row} className="flex justify-around mb-2">
                      {[...Array(5)].map((_, col) => (
                        <div
                          key={col}
                          className="w-1.5 h-1.5 bg-gray-700 rounded-full shadow-inner"
                        />
                      ))}
                    </div>
                  ))}
                </div>
                {/* Ventilation grilles */}
                <div className="absolute right-4 top-2 bottom-2 w-8 flex flex-col justify-around">
                  {[...Array(4)].map((_, i) => (
                    <div key={i} className="h-0.5 bg-blue-400 opacity-60 rounded" />
                  ))}
                </div>
                {/* Headlight with stronger glow */}
                <div className="absolute -right-2 top-1/2 -translate-y-1/2 w-4 h-4 bg-yellow-300 rounded-full shadow-xl animate-pulse"
                  style={{
                    boxShadow: '0 0 20px rgba(253, 224, 71, 0.8), 0 0 40px rgba(253, 224, 71, 0.4)'
                  }}
                />
                {/* Light beam */}
                {position > 0 && (
                  <div
                    className="absolute -right-2 top-1/2 -translate-y-1/2 h-1 bg-gradient-to-r from-yellow-300 to-transparent opacity-50"
                    style={{ width: '60px' }}
                  />
                )}
              </div>
            </div>
          </div>
        </div>

        {/* Distance markers as percentages */}
        <div className="mt-4 flex justify-between text-sm font-medium text-gray-400">
          <span>0%</span>
          <span>25%</span>
          <span>50%</span>
          <span>75%</span>
          <span>100%</span>
        </div>
      </div>

      {/* Status indicators with better styling */}
      <div className="mt-8 grid grid-cols-3 gap-6">
        <div className="bg-gradient-to-br from-green-50 to-green-100 dark:from-green-900/20 dark:to-green-800/20 rounded-xl p-6 text-center border border-green-200 dark:border-green-800 shadow-lg">
          <div className="text-3xl font-bold text-green-600 dark:text-green-400">
            {position > 0 ? 'Active' : 'Idle'}
          </div>
          <div className="text-sm text-gray-600 dark:text-gray-400 mt-2">Status</div>
        </div>
        <div className="bg-gradient-to-br from-blue-50 to-blue-100 dark:from-blue-900/20 dark:to-blue-800/20 rounded-xl p-6 text-center border border-blue-200 dark:border-blue-800 shadow-lg">
          <div className="text-3xl font-bold text-blue-600 dark:text-blue-400">
            {remaining}m
          </div>
          <div className="text-sm text-gray-600 dark:text-gray-400 mt-2">Remaining</div>
        </div>
        <div className="bg-gradient-to-br from-purple-50 to-purple-100 dark:from-purple-900/20 dark:to-purple-800/20 rounded-xl p-6 text-center border border-purple-200 dark:border-purple-800 shadow-lg">
          <div className="text-3xl font-bold text-purple-600 dark:text-purple-400">
            {excavated}m
          </div>
          <div className="text-sm text-gray-600 dark:text-gray-400 mt-2">Excavated</div>
        </div>
      </div>
    </div>
  );
}
