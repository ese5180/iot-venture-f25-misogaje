'use client';

import React, { useRef, useEffect, useState, useCallback } from 'react';

interface Point {
  x: number;
  y: number;
  timestamp: number;
}

interface LocationTrackerProps {
  currentPosition: { x: number; y: number } | null;
  history: Point[];
  className?: string;
  onClearCanvas?: () => void;
}

export function LocationTracker({ currentPosition, history, className = '', onClearCanvas }: LocationTrackerProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [dimensions, setDimensions] = useState({ width: 800, height: 600 });
  const [animatedPosition, setAnimatedPosition] = useState(currentPosition);
  const [showTracing, setShowTracing] = useState(true);
  const animationRef = useRef<number>();
  const pulseRef = useRef(0);

  // Handle resize
  useEffect(() => {
    const updateDimensions = () => {
      if (containerRef.current) {
        const rect = containerRef.current.getBoundingClientRect();
        const size = Math.min(rect.width - 48, 700);
        setDimensions({ width: size, height: size });
      }
    };

    updateDimensions();
    window.addEventListener('resize', updateDimensions);
    return () => window.removeEventListener('resize', updateDimensions);
  }, []);

  // Smooth position animation
  useEffect(() => {
    if (!currentPosition) return;

    const animate = () => {
      setAnimatedPosition((prev) => {
        if (!prev) return currentPosition;
        const dx = currentPosition.x - prev.x;
        const dy = currentPosition.y - prev.y;
        if (Math.abs(dx) < 0.1 && Math.abs(dy) < 0.1) return currentPosition;
        return {
          x: prev.x + dx * 0.12,
          y: prev.y + dy * 0.12,
        };
      });
      animationRef.current = requestAnimationFrame(animate);
    };

    animationRef.current = requestAnimationFrame(animate);
    return () => {
      if (animationRef.current) cancelAnimationFrame(animationRef.current);
    };
  }, [currentPosition]);

  // Catmull-Rom spline interpolation for smooth curves
  const catmullRomSpline = useCallback(
    (p0: Point, p1: Point, p2: Point, p3: Point, t: number): { x: number; y: number } => {
      const t2 = t * t;
      const t3 = t2 * t;

      const x =
        0.5 *
        (2 * p1.x +
          (-p0.x + p2.x) * t +
          (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
          (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);

      const y =
        0.5 *
        (2 * p1.y +
          (-p0.y + p2.y) * t +
          (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
          (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);

      return { x, y };
    },
    []
  );

  // Convert coordinates from 0-1000 to canvas pixels
  const toCanvas = useCallback(
    (x: number, y: number) => {
      const padding = 60;
      const plotSize = dimensions.width - padding * 2;
      return {
        x: padding + (x / 1000) * plotSize,
        y: padding + ((1000 - y) / 1000) * plotSize, // Flip Y axis
      };
    },
    [dimensions]
  );

  // Main drawing function
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Animation loop for pulse effect
    const drawFrame = () => {
      pulseRef.current = (pulseRef.current + 0.03) % (Math.PI * 2);
      const pulse = Math.sin(pulseRef.current) * 0.5 + 0.5;

      // Clear canvas
      ctx.clearRect(0, 0, dimensions.width, dimensions.height);

      const padding = 60;
      const plotSize = dimensions.width - padding * 2;

      // Background gradient
      const bgGradient = ctx.createLinearGradient(0, 0, dimensions.width, dimensions.height);
      bgGradient.addColorStop(0, '#0a0f1a');
      bgGradient.addColorStop(0.5, '#0d1525');
      bgGradient.addColorStop(1, '#0a1020');
      ctx.fillStyle = bgGradient;
      ctx.fillRect(0, 0, dimensions.width, dimensions.height);

      // Subtle noise texture effect
      ctx.fillStyle = 'rgba(255, 255, 255, 0.008)';
      for (let i = 0; i < 50; i++) {
        const x = Math.random() * dimensions.width;
        const y = Math.random() * dimensions.height;
        ctx.beginPath();
        ctx.arc(x, y, Math.random() * 2, 0, Math.PI * 2);
        ctx.fill();
      }

      // Grid area background
      ctx.fillStyle = 'rgba(15, 25, 45, 0.6)';
      ctx.fillRect(padding, padding, plotSize, plotSize);

      // Border glow
      ctx.strokeStyle = 'rgba(59, 130, 246, 0.3)';
      ctx.lineWidth = 2;
      ctx.strokeRect(padding, padding, plotSize, plotSize);

      // Grid lines - minor
      ctx.strokeStyle = 'rgba(59, 130, 246, 0.08)';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 20; i++) {
        const pos = padding + (i / 20) * plotSize;
        ctx.beginPath();
        ctx.moveTo(pos, padding);
        ctx.lineTo(pos, padding + plotSize);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(padding, pos);
        ctx.lineTo(padding + plotSize, pos);
        ctx.stroke();
      }

      // Grid lines - major
      ctx.strokeStyle = 'rgba(59, 130, 246, 0.2)';
      ctx.lineWidth = 1;
      for (let i = 0; i <= 4; i++) {
        const pos = padding + (i / 4) * plotSize;
        ctx.beginPath();
        ctx.moveTo(pos, padding);
        ctx.lineTo(pos, padding + plotSize);
        ctx.stroke();
        ctx.beginPath();
        ctx.moveTo(padding, pos);
        ctx.lineTo(padding + plotSize, pos);
        ctx.stroke();
      }

      // Axis labels
      ctx.fillStyle = 'rgba(148, 163, 184, 0.9)';
      ctx.font = '12px "JetBrains Mono", "SF Mono", "Fira Code", monospace';
      ctx.textAlign = 'center';

      // X-axis labels
      for (let i = 0; i <= 4; i++) {
        const value = i * 250;
        const x = padding + (i / 4) * plotSize;
        ctx.fillText(value.toString(), x, dimensions.height - 25);
      }

      // Y-axis labels
      ctx.textAlign = 'right';
      for (let i = 0; i <= 4; i++) {
        const value = i * 250;
        const y = padding + ((4 - i) / 4) * plotSize;
        ctx.fillText(value.toString(), padding - 12, y + 4);
      }

      // Axis titles
      ctx.fillStyle = 'rgba(148, 163, 184, 0.7)';
      ctx.font = '13px "SF Pro Display", "Segoe UI", system-ui, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('X Position', dimensions.width / 2, dimensions.height - 8);

      ctx.save();
      ctx.translate(16, dimensions.height / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.fillText('Y Position', 0, 0);
      ctx.restore();

      // Draw path history (only if tracing is enabled)
      if (showTracing && history.length >= 2) {
        // Create gradient for the path based on time
        const pathGradient = ctx.createLinearGradient(
          padding,
          padding,
          padding + plotSize,
          padding + plotSize
        );
        pathGradient.addColorStop(0, 'rgba(139, 92, 246, 0.3)');
        pathGradient.addColorStop(0.3, 'rgba(59, 130, 246, 0.6)');
        pathGradient.addColorStop(0.7, 'rgba(34, 211, 238, 0.8)');
        pathGradient.addColorStop(1, 'rgba(52, 211, 153, 1)');

        // Draw smooth curve using Catmull-Rom spline
        ctx.beginPath();
        ctx.strokeStyle = pathGradient;
        ctx.lineWidth = 3;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';

        const points = history.map((p) => toCanvas(p.x, p.y));

        if (points.length >= 2) {
          // Extended points for smooth ends
          const extendedPoints = [
            points[0],
            ...points,
            points[points.length - 1],
          ];

          ctx.moveTo(points[0].x, points[0].y);

          for (let i = 1; i < extendedPoints.length - 2; i++) {
            for (let t = 0; t <= 1; t += 0.05) {
              const pt = catmullRomSpline(
                { ...extendedPoints[i - 1], timestamp: 0 },
                { ...extendedPoints[i], timestamp: 0 },
                { ...extendedPoints[i + 1], timestamp: 0 },
                { ...extendedPoints[i + 2], timestamp: 0 },
                t
              );
              ctx.lineTo(pt.x, pt.y);
            }
          }
          ctx.stroke();

          // Glow effect
          ctx.shadowBlur = 15;
          ctx.shadowColor = 'rgba(34, 211, 238, 0.5)';
          ctx.strokeStyle = 'rgba(34, 211, 238, 0.3)';
          ctx.lineWidth = 6;
          ctx.stroke();
          ctx.shadowBlur = 0;
        }

        // Draw historical points
        history.forEach((point, index) => {
          const canvasPoint = toCanvas(point.x, point.y);
          const age = (history.length - 1 - index) / Math.max(history.length - 1, 1);
          const opacity = 0.3 + (1 - age) * 0.7;
          const size = 3 + (1 - age) * 3;

          // Point glow
          const gradient = ctx.createRadialGradient(
            canvasPoint.x,
            canvasPoint.y,
            0,
            canvasPoint.x,
            canvasPoint.y,
            size * 2
          );
          gradient.addColorStop(0, `rgba(34, 211, 238, ${opacity})`);
          gradient.addColorStop(1, 'rgba(34, 211, 238, 0)');
          ctx.fillStyle = gradient;
          ctx.beginPath();
          ctx.arc(canvasPoint.x, canvasPoint.y, size * 2, 0, Math.PI * 2);
          ctx.fill();

          // Point center
          ctx.fillStyle = `rgba(255, 255, 255, ${opacity * 0.8})`;
          ctx.beginPath();
          ctx.arc(canvasPoint.x, canvasPoint.y, size * 0.6, 0, Math.PI * 2);
          ctx.fill();
        });
      }

      // Draw current position marker
      if (animatedPosition) {
        const pos = toCanvas(animatedPosition.x, animatedPosition.y);

        // Outer pulse rings
        for (let ring = 3; ring >= 1; ring--) {
          const ringPulse = (pulse + ring * 0.2) % 1;
          const ringRadius = 12 + ringPulse * 25;
          const ringOpacity = (1 - ringPulse) * 0.4;

          ctx.strokeStyle = `rgba(52, 211, 153, ${ringOpacity})`;
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(pos.x, pos.y, ringRadius, 0, Math.PI * 2);
          ctx.stroke();
        }

        // Glow effect
        const glowGradient = ctx.createRadialGradient(pos.x, pos.y, 0, pos.x, pos.y, 35);
        glowGradient.addColorStop(0, 'rgba(52, 211, 153, 0.6)');
        glowGradient.addColorStop(0.4, 'rgba(34, 211, 238, 0.3)');
        glowGradient.addColorStop(1, 'rgba(34, 211, 238, 0)');
        ctx.fillStyle = glowGradient;
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, 35, 0, Math.PI * 2);
        ctx.fill();

        // Main marker
        const markerGradient = ctx.createRadialGradient(pos.x - 3, pos.y - 3, 0, pos.x, pos.y, 12);
        markerGradient.addColorStop(0, '#6ee7b7');
        markerGradient.addColorStop(0.5, '#34d399');
        markerGradient.addColorStop(1, '#10b981');
        ctx.fillStyle = markerGradient;
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, 10 + pulse * 2, 0, Math.PI * 2);
        ctx.fill();

        // Inner highlight
        ctx.fillStyle = 'rgba(255, 255, 255, 0.9)';
        ctx.beginPath();
        ctx.arc(pos.x - 2, pos.y - 2, 4, 0, Math.PI * 2);
        ctx.fill();

        // Border
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(pos.x, pos.y, 10 + pulse * 2, 0, Math.PI * 2);
        ctx.stroke();
      }

      requestAnimationFrame(drawFrame);
    };

    const frameId = requestAnimationFrame(drawFrame);
    return () => cancelAnimationFrame(frameId);
  }, [dimensions, history, animatedPosition, toCanvas, catmullRomSpline, showTracing]);

  const speed = history.length >= 2 
    ? Math.sqrt(
        Math.pow(history[history.length - 1].x - history[history.length - 2].x, 2) +
        Math.pow(history[history.length - 1].y - history[history.length - 2].y, 2)
      ).toFixed(2)
    : '0.00';

  const distance = history.length >= 2
    ? history.slice(1).reduce((acc, point, i) => {
        return acc + Math.sqrt(
          Math.pow(point.x - history[i].x, 2) +
          Math.pow(point.y - history[i].y, 2)
        );
      }, 0).toFixed(1)
    : '0.0';

  return (
    <div className={`w-full ${className}`} ref={containerRef}>
      {/* Header */}
      <div className="mb-6 flex justify-between items-start">
        <div>
          <h2 className="text-2xl font-semibold text-white tracking-tight" style={{ fontFamily: '"SF Pro Display", "Segoe UI", system-ui, sans-serif' }}>
            Live Position Tracker
          </h2>
          <p className="text-sm text-slate-400 mt-1">
            Real-time coordinate monitoring
          </p>
        </div>
        <div className="flex items-center gap-3">
          {/* Tracing Toggle */}
          <div className="flex items-center gap-2 bg-slate-800/50 rounded-lg px-3 py-2 border border-slate-700/50">
            <span className="text-xs text-slate-400 uppercase tracking-wider">Trace</span>
            <button
              onClick={() => setShowTracing(!showTracing)}
              className={`relative w-11 h-6 rounded-full transition-colors duration-200 ${
                showTracing ? 'bg-cyan-500' : 'bg-slate-600'
              }`}
            >
              <div
                className={`absolute top-1 w-4 h-4 rounded-full bg-white shadow-md transition-transform duration-200 ${
                  showTracing ? 'translate-x-6' : 'translate-x-1'
                }`}
              />
            </button>
          </div>
          {onClearCanvas && history.length > 0 && (
            <button
              onClick={onClearCanvas}
              className="px-4 py-2 bg-slate-700/50 hover:bg-slate-600/50 text-slate-300 rounded-lg transition-all text-sm font-medium border border-slate-600/50 flex items-center gap-2"
            >
              <svg className="w-4 h-4" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16" />
              </svg>
              Clear Canvas
            </button>
          )}
          {currentPosition && (
            <div className="flex items-center gap-3 bg-slate-800/50 rounded-lg px-4 py-2 border border-slate-700/50">
              <div className="text-center">
                <div className="text-xs text-slate-500 uppercase tracking-wider">X</div>
                <div className="text-xl font-mono font-semibold text-emerald-400">
                  {currentPosition.x.toFixed(1)}
                </div>
              </div>
              <div className="w-px h-8 bg-slate-700"></div>
              <div className="text-center">
                <div className="text-xs text-slate-500 uppercase tracking-wider">Y</div>
                <div className="text-xl font-mono font-semibold text-cyan-400">
                  {currentPosition.y.toFixed(1)}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>

      {/* Canvas container */}
      <div className="relative rounded-xl overflow-hidden shadow-2xl border border-slate-700/50 bg-slate-900">
        {/* Corner decorations */}
        <div className="absolute top-0 left-0 w-16 h-16 border-l-2 border-t-2 border-cyan-500/30 rounded-tl-xl pointer-events-none"></div>
        <div className="absolute top-0 right-0 w-16 h-16 border-r-2 border-t-2 border-cyan-500/30 rounded-tr-xl pointer-events-none"></div>
        <div className="absolute bottom-0 left-0 w-16 h-16 border-l-2 border-b-2 border-cyan-500/30 rounded-bl-xl pointer-events-none"></div>
        <div className="absolute bottom-0 right-0 w-16 h-16 border-r-2 border-b-2 border-cyan-500/30 rounded-br-xl pointer-events-none"></div>

        <canvas
          ref={canvasRef}
          width={dimensions.width}
          height={dimensions.height}
          className="block mx-auto"
        />

        {/* Status overlay */}
        <div className="absolute top-4 left-4 flex items-center gap-2 bg-slate-900/80 backdrop-blur-sm rounded-lg px-3 py-1.5 border border-slate-700/50">
          <div className="w-2 h-2 rounded-full bg-emerald-500 animate-pulse shadow-lg shadow-emerald-500/50"></div>
          <span className="text-xs font-medium text-slate-300 uppercase tracking-wider">Live</span>
        </div>

        {/* Data points overlay */}
        <div className="absolute top-4 right-4 bg-slate-900/80 backdrop-blur-sm rounded-lg px-3 py-1.5 border border-slate-700/50">
          <span className="text-xs font-mono text-slate-400">
            {history.length} <span className="text-slate-500">points</span>
          </span>
        </div>
      </div>

      {/* Stats panel */}
      <div className="mt-6 grid grid-cols-4 gap-4">
        <div className="bg-gradient-to-br from-slate-800/80 to-slate-900/80 rounded-xl p-4 border border-slate-700/50 backdrop-blur-sm">
          <div className="text-xs text-slate-500 uppercase tracking-wider mb-1">Current X</div>
          <div className="text-2xl font-mono font-bold text-emerald-400">
            {currentPosition?.x.toFixed(2) ?? '—'}
          </div>
        </div>
        <div className="bg-gradient-to-br from-slate-800/80 to-slate-900/80 rounded-xl p-4 border border-slate-700/50 backdrop-blur-sm">
          <div className="text-xs text-slate-500 uppercase tracking-wider mb-1">Current Y</div>
          <div className="text-2xl font-mono font-bold text-cyan-400">
            {currentPosition?.y.toFixed(2) ?? '—'}
          </div>
        </div>
        <div className="bg-gradient-to-br from-slate-800/80 to-slate-900/80 rounded-xl p-4 border border-slate-700/50 backdrop-blur-sm">
          <div className="text-xs text-slate-500 uppercase tracking-wider mb-1">Last Step</div>
          <div className="text-2xl font-mono font-bold text-violet-400">
            {speed}
          </div>
        </div>
        <div className="bg-gradient-to-br from-slate-800/80 to-slate-900/80 rounded-xl p-4 border border-slate-700/50 backdrop-blur-sm">
          <div className="text-xs text-slate-500 uppercase tracking-wider mb-1">Total Distance</div>
          <div className="text-2xl font-mono font-bold text-amber-400">
            {distance}
          </div>
        </div>
      </div>
    </div>
  );
}

