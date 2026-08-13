import { useEffect, useRef, useState } from 'react';
import { PongRush } from '../web/game/pong';

type Scene = 'title' | 'countdown' | 'playing' | 'scored' | 'gameover';

export default function GamePage() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const gameRef = useRef<PongRush | null>(null);
  const [scene, setScene] = useState<Scene>('title');
  const [leftScore, setLeftScore] = useState(0);
  const [rightScore, setRightScore] = useState(0);
  const [countdown, setCountdown] = useState(0);
  const [pulse, setPulse] = useState(0);
  const [showControls, setShowControls] = useState(false);

  useEffect(() => {
    if (!canvasRef.current) return;
    const game = new PongRush(canvasRef.current);
    gameRef.current = game;
    game.init();

    let raf = 0;
    const sync = () => {
      setScene(game.getScene());
      setLeftScore(game.getLeftScore());
      setRightScore(game.getRightScore());
      setCountdown(game.getCountdown());
      setPulse(game.getTitlePulse());
      raf = requestAnimationFrame(sync);
    };
    raf = requestAnimationFrame(sync);

    return () => {
      cancelAnimationFrame(raf);
      game.shutdown();
      gameRef.current = null;
    };
  }, []);

  const winner = leftScore >= 7 ? 'player' : rightScore >= 7 ? 'ai' : null;

  return (
    <div className="min-h-screen bg-slate-950 text-white flex flex-col items-center justify-center p-4 font-mono">
      <header className="mb-4 text-center">
        <h1 className="text-3xl font-bold tracking-tight">
          <span className="text-cyan-400">PONG</span>
          <span className="text-pink-400">:RUSH</span>
        </h1>
        <p className="text-xs text-slate-400 mt-1">built on the TD Engine (TS port)</p>
      </header>

      <div className="relative rounded-lg overflow-hidden shadow-2xl ring-1 ring-slate-700">
        <canvas
          ref={canvasRef}
          width={800}
          height={600}
          className="block bg-black"
          style={{ width: '800px', height: '600px' }}
        />

        {/* Title overlay */}
        {scene === 'title' && (
          <div className="absolute inset-0 flex flex-col items-center justify-center bg-black/60 backdrop-blur-sm">
            <h2
              className="text-7xl font-black tracking-tight mb-2"
              style={{
                textShadow: `0 0 ${20 + Math.sin(pulse * 2) * 10}px rgba(80, 220, 255, 0.6)`,
              }}
            >
              <span className="text-cyan-300">PONG</span>
              <span className="text-pink-300">:RUSH</span>
            </h2>
            <p className="text-slate-300 mb-8 text-sm tracking-widest">
              FIRST TO 7 WINS
            </p>
            <button
              onClick={() => gameRef.current?.startFromReact()}
              className="px-8 py-3 bg-gradient-to-r from-cyan-500 to-pink-500 rounded-md font-bold text-lg hover:scale-105 transition-transform shadow-lg shadow-cyan-500/30"
            >
              PRESS SPACE TO PLAY
            </button>
            <div className="mt-6 text-xs text-slate-400 space-y-1 text-center">
              <div><span className="text-cyan-300">W / S</span> or <span className="text-cyan-300">↑ / ↓</span> — move paddle</div>
              <div><span className="text-cyan-300">Space</span> — start / restart</div>
            </div>
          </div>
        )}

        {/* Countdown overlay */}
        {scene === 'countdown' && countdown > 0 && (
          <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
            <div
              key={countdown}
              className="text-9xl font-black text-cyan-300"
              style={{
                textShadow: '0 0 40px rgba(80, 220, 255, 0.8)',
                animation: 'pop 0.4s ease-out',
              }}
            >
              {countdown}
            </div>
          </div>
        )}

        {/* Scored overlay */}
        {scene === 'scored' && (
          <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
            <div
              className="text-6xl font-black animate-pulse"
              style={{ color: leftScore > rightScore ? '#50dcff' : '#ff6eb4' }}
            >
              {leftScore > rightScore ? 'POINT!' : 'AI SCORES!'}
            </div>
          </div>
        )}

        {/* Game over overlay */}
        {scene === 'gameover' && (
          <div className="absolute inset-0 flex flex-col items-center justify-center bg-black/70 backdrop-blur-sm">
            <h2
              className="text-7xl font-black mb-2"
              style={{ color: winner === 'player' ? '#50dcff' : '#ff6eb4' }}
            >
              {winner === 'player' ? 'YOU WIN!' : 'AI WINS'}
            </h2>
            <p className="text-3xl font-bold mb-8 text-slate-200">
              {leftScore} — {rightScore}
            </p>
            <button
              onClick={() => gameRef.current?.startFromReact()}
              className="px-8 py-3 bg-gradient-to-r from-cyan-500 to-pink-500 rounded-md font-bold text-lg hover:scale-105 transition-transform"
            >
              PLAY AGAIN
            </button>
            <p className="mt-4 text-xs text-slate-400">or press Space / R</p>
          </div>
        )}

        {/* Top-right live score */}
        {scene !== 'title' && scene !== 'gameover' && (
          <div className="absolute top-2 left-1/2 -translate-x-1/2 flex gap-6 text-2xl font-black tracking-widest pointer-events-none">
            <span className="text-cyan-300">{leftScore.toString().padStart(2, '0')}</span>
            <span className="text-slate-500">·</span>
            <span className="text-pink-300">{rightScore.toString().padStart(2, '0')}</span>
          </div>
        )}

        {/* Controls toggle */}
        <button
          onClick={() => setShowControls(v => !v)}
          className="absolute top-2 right-2 text-xs text-slate-400 bg-slate-900/80 px-2 py-1 rounded hover:text-white"
        >
          {showControls ? '×' : '?'}
        </button>
        {showControls && (
          <div className="absolute top-9 right-2 bg-slate-900/90 border border-slate-700 rounded-md p-3 text-xs space-y-1 w-48">
            <div className="font-bold text-slate-200 mb-2">CONTROLS</div>
            <div className="text-cyan-300">W / S <span className="text-slate-400">— move up/down</span></div>
            <div className="text-cyan-300">↑ / ↓ <span className="text-slate-400">— arrow keys</span></div>
            <div className="text-cyan-300">Space <span className="text-slate-400">— start / restart</span></div>
            <div className="text-cyan-300">R <span className="text-slate-400">— restart from game over</span></div>
            <div className="border-t border-slate-700 mt-2 pt-2 text-slate-400">
              Engine: TD (TS port) — ECS, SpriteBatch, AABB, particles, AI prediction
            </div>
          </div>
        )}
      </div>

      <footer className="mt-4 text-xs text-slate-500 text-center">
        <div>TD Engine · TypeScript port · WebGL2 · {WIDTH}×{HEIGHT}</div>
        <div className="mt-1">
          <a href="https://github.com/hacvilke/td" className="text-cyan-400 hover:underline">github.com/hacvilke/td</a>
        </div>
      </footer>

      <style>{`
        @keyframes pop {
          0% { transform: scale(0.4); opacity: 0; }
          60% { transform: scale(1.2); opacity: 1; }
          100% { transform: scale(1); opacity: 1; }
        }
      `}</style>
    </div>
  );
}

const WIDTH = 800;
const HEIGHT = 600;
