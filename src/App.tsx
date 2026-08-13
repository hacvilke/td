import { useState, lazy, Suspense } from 'react';

const features = [
  { icon: '🎮', title: 'Browser Game', desc: 'A complete Pong:Rush built on the TS engine port — particles, AI, screen shake, slow-mo.', cta: 'Play Pong:Rush' },
  { icon: '🎨', title: '2D Renderer', desc: 'WebGL2 SpriteBatch with rotation, color tint, alpha — same shaders as the C++ engine.' },
  { icon: '🧩', title: 'ECS Architecture', desc: 'World / Entity / Component / System with bit-mask queries, faithful to src/ecs/.' },
  { icon: '⚡', title: 'Physics', desc: 'AABB collision detection, ball/paddle/wall response, ball trail, speed clamping.' },
  { icon: '📜', title: 'Original C++ Engine', desc: 'The repo\'s C++ codebase (Win32/OpenGL/Winsock) is preserved in src/.' },
  { icon: '🔧', title: 'Fixed Bridge Bug', desc: 'Replaced the broken wasm/js_bridge.js stub with a real working TS engine.' },
];

function App() {
  const [view, setView] = useState<'home' | 'game'>('home');

  if (view === 'game') {
    // Lazy import so the heavy game + WebGL code doesn't load on the landing page.
    const GamePage = lazy(() => import('./GamePage'));
    return (
      <Suspense fallback={<div className="min-h-screen bg-slate-950 text-white flex items-center justify-center font-mono">Loading game...</div>}>
        <GamePage />
      </Suspense>
    );
  }

  return (
    <div className="min-h-screen bg-gradient-to-br from-slate-950 via-slate-900 to-slate-950 text-white">
      {/* Header */}
      <header className="bg-slate-950/80 backdrop-blur-sm border-b border-slate-800 sticky top-0 z-50">
        <div className="max-w-6xl mx-auto px-6 py-4 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 bg-gradient-to-br from-cyan-400 to-pink-500 rounded-lg flex items-center justify-center font-bold text-xl text-slate-950">
              TD
            </div>
            <div>
              <h1 className="text-xl font-bold tracking-tight">TD Engine</h1>
              <p className="text-xs text-slate-400 font-mono">C++ / TypeScript</p>
            </div>
          </div>
          <nav className="flex gap-6 text-sm">
            <a href="#features" className="text-slate-400 hover:text-white transition-colors">Features</a>
            <a href="#architecture" className="text-slate-400 hover:text-white transition-colors">Architecture</a>
            <a href="https://github.com/hacvilke/td" className="text-slate-400 hover:text-white transition-colors">GitHub</a>
            <button
              onClick={() => setView('game')}
              className="px-4 py-1.5 bg-gradient-to-r from-cyan-500 to-pink-500 rounded-md font-semibold text-slate-950 hover:opacity-90 transition-opacity"
            >
              ▶ Play Game
            </button>
          </nav>
        </div>
      </header>

      {/* Hero */}
      <section className="py-24 px-6">
        <div className="max-w-4xl mx-auto text-center">
          <div className="inline-block px-4 py-1 bg-cyan-500/10 text-cyan-300 rounded-full text-xs font-mono tracking-widest mb-6 border border-cyan-500/20">
            FIXED: WASM BRIDGE BUG · ENGINE PORTED TO TS · GAME SHIPPED
          </div>
          <h2 className="text-5xl md:text-6xl font-black mb-6 tracking-tight">
            <span className="bg-gradient-to-r from-cyan-300 via-white to-pink-300 bg-clip-text text-transparent">
              A complete game engine,
            </span>
            <br />
            <span className="text-slate-400 text-3xl md:text-4xl">now actually running in your browser.</span>
          </h2>
          <p className="text-lg text-slate-400 max-w-2xl mx-auto mb-10">
            The original repo shipped with a broken WebAssembly bridge that left the canvas
            blank. We replaced it with a real TypeScript implementation of the engine's public
            API — ECS, SpriteBatch, Camera2D, Input, AABB physics — and built Pong:Rush on top.
          </p>
          <div className="flex justify-center gap-4">
            <button
              onClick={() => setView('game')}
              className="px-8 py-3 bg-gradient-to-r from-cyan-500 to-pink-500 rounded-lg font-bold text-lg text-slate-950 hover:scale-105 transition-transform shadow-lg shadow-cyan-500/20"
            >
              ▶ Play Pong:Rush
            </button>
            <a
              href="#architecture"
              className="px-8 py-3 bg-slate-800 rounded-lg font-bold text-lg hover:bg-slate-700 transition-colors border border-slate-700"
            >
              View Architecture
            </a>
          </div>
        </div>
      </section>

      {/* Bug callout */}
      <section className="px-6 pb-12">
        <div className="max-w-4xl mx-auto bg-slate-900/50 border border-amber-500/30 rounded-xl p-6">
          <div className="flex items-start gap-3">
            <div className="text-2xl">🐛</div>
            <div>
              <h3 className="font-bold text-amber-300 mb-2">Bug fixed: WASM bridge was a stub</h3>
              <p className="text-sm text-slate-300 mb-3">
                The original <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded">wasm/js_bridge.js</code> had a
                <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded mx-1">_loadWASM()</code>
                method that never actually loaded a WASM module — it just simulated a progress bar
                and stored the config object as <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded">this._module</code>.
                Every <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded">_td_init</code> /
                <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded mx-1">_td_update</code> call was a silent no-op.
              </p>
              <p className="text-sm text-slate-300">
                <span className="text-emerald-300 font-bold">Fix:</span> Replaced with a real TypeScript
                implementation under <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded">web/engine/</code>.
                The C++ engine is preserved in <code className="text-cyan-300 font-mono text-xs bg-slate-950 px-1.5 py-0.5 rounded">src/</code> for native builds.
              </p>
            </div>
          </div>
        </div>
      </section>

      {/* Features */}
      <section id="features" className="py-16 px-6 bg-slate-950/40">
        <div className="max-w-6xl mx-auto">
          <h3 className="text-2xl font-bold text-center mb-12">Engine Features</h3>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6">
            {features.map((f, i) => (
              <div
                key={i}
                className={`bg-slate-900/50 rounded-xl p-6 border border-slate-800 hover:border-cyan-500/50 transition-colors ${f.cta ? 'cursor-pointer' : ''}`}
                onClick={() => f.cta && setView('game')}
              >
                <div className="text-4xl mb-4">{f.icon}</div>
                <h4 className="text-lg font-semibold mb-2">{f.title}</h4>
                <p className="text-slate-400 text-sm">{f.desc}</p>
                {f.cta && (
                  <div className="mt-4 text-cyan-300 text-sm font-semibold">▶ {f.cta}</div>
                )}
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Architecture */}
      <section id="architecture" className="py-16 px-6">
        <div className="max-w-5xl mx-auto">
          <h3 className="text-2xl font-bold text-center mb-12">Architecture</h3>
          <div className="grid lg:grid-cols-2 gap-8">
            <div>
              <h4 className="text-lg font-semibold mb-3 text-cyan-300">C++ Engine (native)</h4>
              <pre className="bg-slate-900 rounded-xl p-6 font-mono text-xs text-slate-300 border border-slate-800 overflow-x-auto">
{`┌─────────────────────────────────┐
│          Game Code              │
├─────────────────────────────────┤
│         Engine API              │
├─────┬─────┬─────┬─────┬────────┤
│Rendr│Phys │Audio│ Net │ Assets │
├─────┴─────┴─────┴─────┴────────┤
│   Core (Math, Memory, Logger)   │
├─────────────────────────────────┤
│     Platform (Win32, OpenGL)    │
└─────────────────────────────────┘`}
              </pre>
              <p className="text-xs text-slate-500 mt-2 font-mono">src/ — preserved, builds on Windows</p>
            </div>
            <div>
              <h4 className="text-lg font-semibold mb-3 text-pink-300">TS Port (browser)</h4>
              <pre className="bg-slate-900 rounded-xl p-6 font-mono text-xs text-slate-300 border border-slate-800 overflow-x-auto">
{`┌─────────────────────────────────┐
│       Pong:Rush (game code)     │
├─────────────────────────────────┤
│       Engine (TS port)          │
├─────┬─────┬─────┬───────────────┤
│ ECS │Rendr│Input│ Particles/AI  │
├─────┴─────┴─────┴───────────────┤
│  Math (Vec2/3, Mat4, Color)     │
├─────────────────────────────────┤
│   Platform (WebGL2, DOM)        │
└─────────────────────────────────┘`}
              </pre>
              <p className="text-xs text-slate-500 mt-2 font-mono">web/engine/ — works in any modern browser</p>
            </div>
          </div>
        </div>
      </section>

      {/* Stats */}
      <section className="py-12 px-6 bg-slate-950/40">
        <div className="max-w-4xl mx-auto grid grid-cols-2 md:grid-cols-4 gap-6 text-center">
          {[
            { v: '0', l: 'External deps (game)' },
            { v: 'WebGL2', l: 'Renderer' },
            { v: '60fps', l: 'Fixed-step loop' },
            { v: 'ECS', l: 'Architecture' },
          ].map((s, i) => (
            <div key={i}>
              <div className="text-3xl font-black text-cyan-300 mb-1">{s.v}</div>
              <div className="text-xs text-slate-400 font-mono">{s.l}</div>
            </div>
          ))}
        </div>
      </section>

      {/* CTA */}
      <section className="py-20 px-6 text-center">
        <h3 className="text-3xl font-black mb-4">Ready to play?</h3>
        <p className="text-slate-400 mb-8">First to 7 points wins. The AI predicts your shots — mix it up.</p>
        <button
          onClick={() => setView('game')}
          className="px-10 py-4 bg-gradient-to-r from-cyan-500 to-pink-500 rounded-lg font-bold text-xl text-slate-950 hover:scale-105 transition-transform shadow-lg shadow-cyan-500/30"
        >
          ▶ Play Pong:Rush
        </button>
      </section>

      <footer className="py-8 px-6 border-t border-slate-800 text-center text-slate-500 text-sm">
        <p>TD Engine · MIT License · <a href="https://github.com/hacvilke/td" className="text-cyan-400 hover:underline">github.com/hacvilke/td</a></p>
        <p className="mt-1 text-xs font-mono">No STL for hot paths · OpenGL 3.3 Core · WebGL2 · TypeScript port preserves the original C++ API</p>
      </footer>
    </div>
  );
}

export default App;
