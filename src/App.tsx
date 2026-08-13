import { useState, lazy, Suspense } from 'react';

const DocsPage = lazy(() => import('./DocsPage'));

const features = [
  {
    title: 'ECS Core',
    desc: 'Bit-mask queries over a packed entity store. Same layout as the C++ engine — Position, Velocity, Sprite, Collider, RigidBody, Tag.',
  },
  {
    title: 'WebGL2 Renderer',
    desc: 'SpriteBatch with rotation, tint, alpha, batched draw calls. Same GLSL shaders as the native engine, ported to GLSL ES 3.00.',
  },
  {
    title: 'AABB Physics',
    desc: 'Box-vs-box collision detection with paddle spin, wall reflection, speed clamping. Mirrors src/physics/aabb.h.',
  },
  {
    title: 'Fixed-Step Loop',
    desc: '60Hz simulation with interpolation. Accumulator pattern matches td::GameLoop — deterministic, frame-rate independent.',
  },
  {
    title: 'Native C++ Build',
    desc: 'The original Win32 / OpenGL 3.3 / Winsock codebase is preserved in src/ and builds on Windows via CMake + MSVC.',
  },
  {
    title: 'MIT Licensed',
    desc: 'Permissive license. Read the source, learn from it, ship a game. No attribution strings attached.',
  },
];

function App() {
  const [view, setView] = useState<'home' | 'game' | 'docs'>('home');

  if (view === 'docs') {
    return (
      <Suspense fallback={<div className="min-h-screen bg-white text-neutral-900 flex items-center justify-center font-mono text-sm text-neutral-500">Loading docs…</div>}>
        <DocsPage />
      </Suspense>
    );
  }

  if (view === 'game') {
    const GamePage = lazy(() => import('./GamePage'));
    return (
      <Suspense fallback={<div className="min-h-screen bg-white text-neutral-900 flex items-center justify-center font-mono text-sm text-neutral-500">Loading game…</div>}>
        <GamePage />
      </Suspense>
    );
  }

  return (
    <div className="min-h-screen bg-white text-neutral-900 antialiased">
      {/* Top nav — single row, vertically centered, clean spacing */}
      <header className="border-b border-neutral-200 sticky top-0 z-50 bg-white/95 backdrop-blur">
        <div className="max-w-6xl mx-auto px-6 h-14 flex items-center justify-between">
          <button
            onClick={() => setView('home')}
            className="flex items-center gap-2 group"
            aria-label="TD Engine home"
          >
            <span className="w-7 h-7 rounded-md border border-neutral-900 flex items-center justify-center text-[11px] font-bold tracking-tight group-hover:bg-neutral-900 group-hover:text-white transition-colors">
              TD
            </span>
            <span className="text-sm font-semibold tracking-tight">TD Engine</span>
            <span className="text-[11px] text-neutral-400 font-mono ml-1 hidden sm:inline">v1.0</span>
          </button>

          <nav className="flex items-center gap-1 text-sm">
            <button onClick={() => setView('home')} className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Home</button>
            <button onClick={() => setView('docs')} className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Docs</button>
            <button onClick={() => setView('game')} className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Play</button>
            <a href="https://github.com/hacvilke/td/releases" className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Releases</a>
            <a
              href="https://github.com/hacvilke/td"
              className="ml-2 px-3 py-1.5 rounded-md border border-neutral-300 hover:border-neutral-900 text-neutral-700 hover:text-neutral-900 transition-colors text-sm font-medium inline-flex items-center gap-1.5"
            >
              <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0016 8c0-4.42-3.58-8-8-8z"/></svg>
              <span>Source</span>
            </a>
            <button
              onClick={() => setView('game')}
              className="ml-1 px-3 py-1.5 rounded-md bg-neutral-900 text-white text-sm font-medium hover:bg-neutral-700 transition-colors"
            >
              Play Pong
            </button>
          </nav>
        </div>
      </header>

      {/* Hero — minimal, type-driven, no emoji */}
      <section className="border-b border-neutral-200">
        <div className="max-w-6xl mx-auto px-6 py-24 md:py-32">
          <p className="text-xs font-mono uppercase tracking-widest text-neutral-500 mb-6">
            Open-source · MIT · C++ + TypeScript
          </p>
          <h1 className="text-5xl md:text-6xl font-semibold tracking-tight text-neutral-900 max-w-3xl leading-[1.05]">
            A small game engine, written from scratch in C++ and ported to run in your browser.
          </h1>
          <p className="mt-6 text-lg text-neutral-600 max-w-2xl leading-relaxed">
            TD Engine is a learning-oriented 2D game engine: ECS, SpriteBatch, AABB physics, fixed-step loop.
            The native build targets Windows with OpenGL 3.3; the TypeScript port runs in any WebGL2 browser.
          </p>
          <div className="mt-10 flex flex-wrap items-center gap-3">
            <button
              onClick={() => setView('game')}
              className="px-5 py-2.5 rounded-md bg-neutral-900 text-white text-sm font-medium hover:bg-neutral-700 transition-colors"
            >
              Play Pong:Rush →
            </button>
            <button
              onClick={() => setView('docs')}
              className="px-5 py-2.5 rounded-md border border-neutral-300 hover:border-neutral-900 text-sm font-medium transition-colors"
            >
              Read the docs
            </button>
            <a
              href="https://github.com/hacvilke/td#readme"
              className="px-5 py-2.5 text-sm font-medium text-neutral-600 hover:text-neutral-900 transition-colors"
            >
              View on GitHub
            </a>
          </div>

          {/* Quick stats — monospace, no decoration */}
          <div className="mt-16 grid grid-cols-2 md:grid-cols-4 gap-px bg-neutral-200 border border-neutral-200 rounded-lg overflow-hidden">
            {[
              ['Language', 'C++17 / TS 5'],
              ['Renderer', 'OpenGL 3.3 / WebGL2'],
              ['Loop', '60 Hz fixed step'],
              ['License', 'MIT'],
            ].map(([k, v]) => (
              <div key={k} className="bg-white px-5 py-4">
                <div className="text-[11px] font-mono uppercase tracking-wider text-neutral-500">{k}</div>
                <div className="mt-1 text-sm font-medium text-neutral-900 font-mono">{v}</div>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Bug callout — factual, no emoji */}
      <section className="border-b border-neutral-200 bg-neutral-50">
        <div className="max-w-6xl mx-auto px-6 py-12">
          <div className="flex flex-col md:flex-row gap-6">
            <div className="md:w-1/4">
              <p className="text-[11px] font-mono uppercase tracking-wider text-neutral-500">Bug fix</p>
              <p className="mt-1 text-sm font-medium text-neutral-900">wasm/js_bridge.js</p>
            </div>
            <div className="md:flex-1 text-sm text-neutral-700 leading-relaxed">
              <p>
                The original repo shipped with a WebAssembly bridge whose <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded">_loadWASM()</code> method
                never actually loaded a WASM module — it stored the config object as <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded">this._module</code> and
                every <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded">_td_init</code> / <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded">_td_update</code> call
                was a silent no-op. The browser canvas stayed blank.
              </p>
              <p className="mt-3">
                We replaced the stub with a real TypeScript port of the engine under <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded">web/engine/</code>.
                The C++ source in <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded">src/</code> is preserved for native builds.
              </p>
            </div>
          </div>
        </div>
      </section>

      {/* Features — plain grid, type-driven */}
      <section className="border-b border-neutral-200">
        <div className="max-w-6xl mx-auto px-6 py-20">
          <div className="mb-12">
            <p className="text-[11px] font-mono uppercase tracking-wider text-neutral-500">Features</p>
            <h2 className="mt-2 text-3xl font-semibold tracking-tight text-neutral-900">What's in the engine</h2>
          </div>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-px bg-neutral-200 border border-neutral-200 rounded-lg overflow-hidden">
            {features.map((f) => (
              <div key={f.title} className="bg-white p-6">
                <h3 className="text-base font-semibold text-neutral-900">{f.title}</h3>
                <p className="mt-2 text-sm text-neutral-600 leading-relaxed">{f.desc}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Architecture — two columns, monospace diagrams */}
      <section className="border-b border-neutral-200 bg-neutral-50">
        <div className="max-w-6xl mx-auto px-6 py-20">
          <div className="mb-12">
            <p className="text-[11px] font-mono uppercase tracking-wider text-neutral-500">Architecture</p>
            <h2 className="mt-2 text-3xl font-semibold tracking-tight text-neutral-900">Two builds, one API</h2>
            <p className="mt-3 text-sm text-neutral-600 max-w-2xl">
              The C++ engine targets Windows natively (Win32, OpenGL 3.3, Winsock, waveOut).
              The TypeScript port targets the browser (WebGL2, DOM, Web Audio).
              Both expose the same ECS / SpriteBatch / Camera2D / Input surface.
            </p>
          </div>
          <div className="grid lg:grid-cols-2 gap-6">
            <div className="bg-white border border-neutral-200 rounded-lg overflow-hidden">
              <div className="px-5 py-3 border-b border-neutral-200 flex items-center justify-between">
                <span className="text-sm font-semibold text-neutral-900">Native (C++)</span>
                <span className="text-[11px] font-mono text-neutral-500">src/</span>
              </div>
              <pre className="px-5 py-5 font-mono text-[11px] text-neutral-700 leading-relaxed overflow-x-auto">
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
            </div>
            <div className="bg-white border border-neutral-200 rounded-lg overflow-hidden">
              <div className="px-5 py-3 border-b border-neutral-200 flex items-center justify-between">
                <span className="text-sm font-semibold text-neutral-900">Browser (TS port)</span>
                <span className="text-[11px] font-mono text-neutral-500">web/engine/</span>
              </div>
              <pre className="px-5 py-5 font-mono text-[11px] text-neutral-700 leading-relaxed overflow-x-auto">
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
            </div>
          </div>
        </div>
      </section>

      {/* Quick start */}
      <section className="border-b border-neutral-200">
        <div className="max-w-6xl mx-auto px-6 py-20">
          <div className="mb-12">
            <p className="text-[11px] font-mono uppercase tracking-wider text-neutral-500">Quick start</p>
            <h2 className="mt-2 text-3xl font-semibold tracking-tight text-neutral-900">Run it locally</h2>
          </div>
          <div className="grid md:grid-cols-2 gap-6">
            <div className="bg-neutral-900 rounded-lg p-5 overflow-x-auto">
              <div className="flex items-center gap-2 mb-3">
                <span className="w-3 h-3 rounded-full bg-neutral-700"></span>
                <span className="w-3 h-3 rounded-full bg-neutral-700"></span>
                <span className="w-3 h-3 rounded-full bg-neutral-700"></span>
                <span className="ml-2 text-[11px] font-mono text-neutral-500">browser — bash</span>
              </div>
              <pre className="font-mono text-[12px] text-neutral-300 leading-relaxed">
{`git clone https://github.com/hacvilke/td.git
cd td
npm install
npm run dev
# open http://localhost:5173`}
              </pre>
            </div>
            <div className="bg-neutral-900 rounded-lg p-5 overflow-x-auto">
              <div className="flex items-center gap-2 mb-3">
                <span className="w-3 h-3 rounded-full bg-neutral-700"></span>
                <span className="w-3 h-3 rounded-full bg-neutral-700"></span>
                <span className="w-3 h-3 rounded-full bg-neutral-700"></span>
                <span className="ml-2 text-[11px] font-mono text-neutral-500">native — cmd</span>
              </div>
              <pre className="font-mono text-[12px] text-neutral-300 leading-relaxed">
{`git clone https://github.com/hacvilke/td.git
cd td
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\\bin\\pong.exe`}
              </pre>
            </div>
          </div>
        </div>
      </section>

      {/* Final CTA */}
      <section className="bg-neutral-50 border-b border-neutral-200">
        <div className="max-w-6xl mx-auto px-6 py-20 text-center">
          <h2 className="text-3xl font-semibold tracking-tight text-neutral-900">First to 7 points wins.</h2>
          <p className="mt-3 text-sm text-neutral-600">The AI predicts your shots. Mix it up.</p>
          <div className="mt-8 flex justify-center gap-3">
            <button
              onClick={() => setView('game')}
              className="px-6 py-2.5 rounded-md bg-neutral-900 text-white text-sm font-medium hover:bg-neutral-700 transition-colors"
            >
              Play Pong:Rush
            </button>
            <button
              onClick={() => setView('docs')}
              className="px-6 py-2.5 rounded-md border border-neutral-300 hover:border-neutral-900 text-sm font-medium transition-colors"
            >
              Read the docs
            </button>
          </div>
        </div>
      </section>

      {/* Footer */}
      <footer className="bg-white">
        <div className="max-w-6xl mx-auto px-6 py-10 flex flex-col md:flex-row items-center justify-between gap-4 text-sm">
          <div className="flex items-center gap-2 text-neutral-500">
            <span className="w-5 h-5 rounded border border-neutral-400 flex items-center justify-center text-[9px] font-bold">TD</span>
            <span className="font-mono text-xs">TD Engine · MIT License</span>
          </div>
          <div className="flex items-center gap-5 text-neutral-600">
            <button onClick={() => setView('docs')} className="hover:text-neutral-900 transition-colors">Docs</button>
            <button onClick={() => setView('game')} className="hover:text-neutral-900 transition-colors">Play</button>
            <a href="https://github.com/hacvilke/td/releases" className="hover:text-neutral-900 transition-colors">Releases</a>
            <a href="https://github.com/hacvilke/td/actions" className="hover:text-neutral-900 transition-colors">CI</a>
            <a href="https://github.com/hacvilke/td" className="hover:text-neutral-900 transition-colors">GitHub</a>
          </div>
        </div>
      </footer>
    </div>
  );
}

export default App;
