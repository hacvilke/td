import { useEffect, useRef, useState } from 'react';
import { PongRush } from '../web/game/pong';

type Scene = 'title' | 'countdown' | 'playing' | 'scored' | 'gameover';

const WIDTH = 800;
const HEIGHT = 600;

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
    <div className="min-h-screen bg-white text-neutral-900 flex flex-col">
      {/* Top nav (matches landing page) */}
      <header className="border-b border-neutral-200 sticky top-0 z-50 bg-white/95 backdrop-blur">
        <div className="max-w-6xl mx-auto px-6 h-14 flex items-center justify-between">
          <button
            onClick={() => { window.location.hash = ''; window.location.reload(); }}
            className="flex items-center gap-2"
          >
            <span className="w-7 h-7 rounded-md border border-neutral-900 flex items-center justify-center text-[11px] font-bold">TD</span>
            <span className="text-sm font-semibold tracking-tight">TD Engine</span>
          </button>
          <button
            onClick={() => { window.location.hash = ''; window.location.reload(); }}
            className="text-sm text-neutral-600 hover:text-neutral-900 transition-colors"
          >
            ← Back to home
          </button>
        </div>
      </header>

      {/* Game area */}
      <div className="flex-1 flex flex-col items-center justify-center px-6 py-10 bg-neutral-50">
        <div className="mb-6 text-center">
          <h1 className="text-2xl font-semibold tracking-tight text-neutral-900">
            Pong:Rush
          </h1>
          <p className="mt-1 text-xs font-mono uppercase tracking-widest text-neutral-500">
            Built on the TD Engine · TypeScript port · WebGL2
          </p>
        </div>

        {/* Canvas + overlays — fixed 800x600, centered */}
        <div
          className="relative bg-black shadow-lg ring-1 ring-neutral-300 overflow-hidden"
          style={{ width: WIDTH + 'px', height: HEIGHT + 'px' }}
        >
          <canvas
            ref={canvasRef}
            width={WIDTH}
            height={HEIGHT}
            className="block"
            style={{ width: WIDTH + 'px', height: HEIGHT + 'px', display: 'block' }}
          />

          {/* Live score — top center, monospace */}
          {(scene === 'countdown' || scene === 'playing' || scene === 'scored') && (
            <div className="absolute top-3 left-1/2 -translate-x-1/2 font-mono text-2xl font-bold tracking-widest text-white pointer-events-none">
              <span className="text-cyan-400">{leftScore.toString().padStart(2, '0')}</span>
              <span className="text-neutral-600 mx-3">·</span>
              <span className="text-pink-400">{rightScore.toString().padStart(2, '0')}</span>
            </div>
          )}

          {/* Title overlay */}
          {scene === 'title' && (
            <div className="absolute inset-0 flex flex-col items-center justify-center bg-black/70">
              <h2
                className="text-6xl font-bold tracking-tight text-white"
                style={{
                  textShadow: `0 0 ${20 + Math.sin(pulse * 2) * 10}px rgba(255,255,255,0.25)`,
                }}
              >
                PONG:RUSH
              </h2>
              <p className="mt-3 text-xs font-mono uppercase tracking-widest text-neutral-400">
                First to 7 wins
              </p>
              <button
                onClick={() => gameRef.current?.startFromReact()}
                className="mt-8 px-6 py-2.5 bg-white text-black text-sm font-semibold rounded hover:bg-neutral-200 transition-colors"
              >
                Press Space to Play
              </button>
              <div className="mt-6 text-xs text-neutral-400 space-y-1 text-center font-mono">
                <div><span className="text-white">W/S</span> or <span className="text-white">↑/↓</span> — move paddle</div>
                <div><span className="text-white">Space</span> — start / restart</div>
              </div>
            </div>
          )}

          {/* Countdown */}
          {scene === 'countdown' && countdown > 0 && (
            <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
              <div
                key={Math.ceil(countdown)}
                className="text-8xl font-bold text-white"
                style={{ animation: 'pop 0.4s ease-out' }}
              >
                {Math.ceil(countdown)}
              </div>
            </div>
          )}

          {/* Scored */}
          {scene === 'scored' && (
            <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
              <div className="text-5xl font-bold tracking-tight text-white">
                {leftScore > rightScore ? 'POINT' : 'AI SCORES'}
              </div>
            </div>
          )}

          {/* Game over */}
          {scene === 'gameover' && (
            <div className="absolute inset-0 flex flex-col items-center justify-center bg-black/80">
              <h2 className="text-6xl font-bold tracking-tight text-white">
                {winner === 'player' ? 'YOU WIN' : 'AI WINS'}
              </h2>
              <p className="mt-3 text-3xl font-mono text-neutral-300">
                {leftScore} — {rightScore}
              </p>
              <button
                onClick={() => gameRef.current?.startFromReact()}
                className="mt-8 px-6 py-2.5 bg-white text-black text-sm font-semibold rounded hover:bg-neutral-200 transition-colors"
              >
                Play Again
              </button>
              <p className="mt-3 text-xs text-neutral-500 font-mono">or press Space / R</p>
            </div>
          )}

          {/* Controls help — top right, minimal */}
          <button
            onClick={() => setShowControls(v => !v)}
            className="absolute top-3 right-3 w-7 h-7 rounded border border-neutral-700 bg-black/60 text-neutral-300 text-xs hover:bg-black/80 hover:text-white transition-colors"
            aria-label="Toggle controls"
          >
            ?
          </button>
          {showControls && (
            <div className="absolute top-12 right-3 bg-black/90 border border-neutral-700 rounded p-3 text-xs font-mono text-neutral-300 space-y-1 w-48">
              <div className="text-neutral-500 uppercase tracking-wider text-[10px] mb-2">Controls</div>
              <div><span className="text-white">W/S</span> — move up/down</div>
              <div><span className="text-white">↑/↓</span> — arrow keys</div>
              <div><span className="text-white">Space</span> — start / restart</div>
              <div><span className="text-white">R</span> — restart from game over</div>
            </div>
          )}
        </div>

        <p className="mt-6 text-xs font-mono text-neutral-500">
          TD Engine · ECS · SpriteBatch · AABB · Particles · AI trajectory prediction
        </p>
      </div>

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
