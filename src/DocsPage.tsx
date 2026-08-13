import { useState } from 'react';

type Tab = 'overview' | 'td-lang' | 'cpp' | 'ts' | 'examples' | 'editor' | 'workflows';

const TABS: { id: Tab; label: string }[] = [
  { id: 'overview',  label: 'Overview' },
  { id: 'td-lang',   label: 'TD Scripting' },
  { id: 'cpp',       label: 'C++ Engine' },
  { id: 'ts',        label: 'TS / JS Port' },
  { id: 'examples',  label: 'Example Games' },
  { id: 'editor',    label: 'Editor' },
  { id: 'workflows', label: 'CI / Releases' },
];

export default function DocsPage() {
  const [tab, setTab] = useState<Tab>('overview');

  return (
    <div className="min-h-screen bg-slate-950 text-slate-200">
      <header className="bg-slate-950/90 backdrop-blur border-b border-slate-800 sticky top-0 z-40">
        <div className="max-w-6xl mx-auto px-6 py-4 flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="w-9 h-9 bg-gradient-to-br from-cyan-400 to-pink-500 rounded-lg flex items-center justify-center font-bold text-slate-950">TD</div>
            <div>
              <h1 className="text-lg font-bold tracking-tight">TD Engine — Docs</h1>
              <p className="text-xs text-slate-400 font-mono">v1.0 · C++ + TypeScript</p>
            </div>
          </div>
          <a href="/" className="text-sm text-slate-400 hover:text-white">← Back to landing</a>
        </div>
      </header>

      <div className="max-w-6xl mx-auto px-6 py-8 grid lg:grid-cols-[200px_1fr] gap-8">
        {/* Sidebar */}
        <nav className="lg:sticky lg:top-24 lg:self-start">
          <ul className="flex lg:flex-col gap-1 overflow-x-auto pb-2 lg:pb-0">
            {TABS.map(t => (
              <li key={t.id}>
                <button
                  onClick={() => setTab(t.id)}
                  className={`w-full text-left px-3 py-2 rounded-md text-sm whitespace-nowrap transition-colors ${
                    tab === t.id
                      ? 'bg-cyan-500/15 text-cyan-300 border-l-2 border-cyan-400'
                      : 'text-slate-400 hover:text-white hover:bg-slate-900'
                  }`}
                >
                  {t.label}
                </button>
              </li>
            ))}
          </ul>
        </nav>

        {/* Content */}
        <div className="min-w-0">
          {tab === 'overview'  && <OverviewTab />}
          {tab === 'td-lang'   && <TdLangTab />}
          {tab === 'cpp'       && <CppTab />}
          {tab === 'ts'        && <TsTab />}
          {tab === 'examples'  && <ExamplesTab />}
          {tab === 'editor'    && <EditorTab />}
          {tab === 'workflows' && <WorkflowsTab />}
        </div>
      </div>
    </div>
  );
}

/* ---------- shared bits ---------- */

function Code({ children, lang = 'cpp' }: { children: string; lang?: string }) {
  return (
    <pre className="bg-slate-900 border border-slate-800 rounded-lg p-4 overflow-x-auto text-xs leading-relaxed font-mono text-slate-200">
      <code data-lang={lang}>{children}</code>
    </pre>
  );
}

function H2({ children }: { children: React.ReactNode }) {
  return <h2 className="text-2xl font-bold text-white mt-8 mb-3">{children}</h2>;
}

function H3({ children }: { children: React.ReactNode }) {
  return <h3 className="text-lg font-semibold text-cyan-300 mt-6 mb-2">{children}</h3>;
}

function P({ children }: { children: React.ReactNode }) {
  return <p className="text-sm text-slate-300 leading-relaxed mb-3">{children}</p>;
}

function Pill({ children, color = 'slate' }: { children: React.ReactNode; color?: 'slate' | 'cyan' | 'pink' | 'amber' | 'emerald' }) {
  const colors = {
    slate: 'bg-slate-800 text-slate-300 border-slate-700',
    cyan: 'bg-cyan-500/10 text-cyan-300 border-cyan-500/30',
    pink: 'bg-pink-500/10 text-pink-300 border-pink-500/30',
    amber: 'bg-amber-500/10 text-amber-300 border-amber-500/30',
    emerald: 'bg-emerald-500/10 text-emerald-300 border-emerald-500/30',
  };
  return (
    <span className={`inline-block px-2 py-0.5 rounded text-xs font-mono border ${colors[color]}`}>
      {children}
    </span>
  );
}

/* ---------- Overview ---------- */

function OverviewTab() {
  return (
    <div>
      <H2>Overview</H2>
      <P>
        TD Engine is a complete 2D/3D game engine written from scratch in C/C++ with zero external
        dependencies, plus a TypeScript port that runs in any modern browser. The two halves share
        the same architecture (ECS, SpriteBatch, Camera2D, AABB physics, Input) and the same public
        API shape, so a game written against one can be ported to the other with mechanical changes.
      </P>

      <H3>Two targets, one architecture</H3>
      <div className="grid md:grid-cols-2 gap-4 mb-6">
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-2">
            <Pill color="cyan">Native</Pill>
            <span className="text-sm font-semibold text-white">C++ Engine</span>
          </div>
          <P>
            Builds on Windows with MinGW or Visual Studio 2019+. Uses Win32 for windowing/input,
            OpenGL 3.3 for rendering, Winsock2 for networking, waveOut for audio. The C++ source
            tree lives in <code className="text-cyan-300">src/</code>, the visual editor in
            <code className="text-cyan-300"> editor/</code>, and example games in
            <code className="text-cyan-300"> examples/</code>.
          </P>
        </div>
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-4">
          <div className="flex items-center gap-2 mb-2">
            <Pill color="pink">Browser</Pill>
            <span className="text-sm font-semibold text-white">TypeScript Port</span>
          </div>
          <P>
            Runs in any WebGL2-capable browser. Mirrors the C++ API 1:1 (Vec2/3/4, Mat4, World,
            SpriteBatch, Camera2D, Input, GameLoop). The port lives in
            <code className="text-cyan-300"> web/engine/</code>, the example game
            (<span className="text-pink-300">Pong:Rush</span>) in
            <code className="text-cyan-300"> web/game/</code>, and the React HUD in
            <code className="text-cyan-300"> src/</code>.
          </P>
        </div>
      </div>

      <H3>What's where</H3>
      <div className="overflow-x-auto mb-6">
        <table className="w-full text-sm border border-slate-800 rounded-lg overflow-hidden">
          <thead className="bg-slate-900">
            <tr>
              <th className="text-left px-3 py-2 text-slate-300 font-semibold">Path</th>
              <th className="text-left px-3 py-2 text-slate-300 font-semibold">Purpose</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-800">
            {[
              ['src/', 'C++ engine source (math, ECS, renderer, physics, audio, net, scripting)'],
              ['editor/', 'Native visual editor (immediate-mode GUI, scene/inspector/asset panels)'],
              ['examples/', 'Native example games: pong/, platformer/'],
              ['tests/', 'C++ unit tests (math, ECS, physics)'],
              ['assets/shaders/', 'GLSL shaders (sprite.vert/frag, basic_3d.vert/frag)'],
              ['web/engine/', 'TypeScript port of the engine core'],
              ['web/game/', 'Browser games (Pong:Rush, particle system)'],
              ['web/js_bridge.ts', 'Global window.TDEngine shim for static HTML pages'],
              ['web/engine-wrapper.ts', 'Re-exports the TS engine for legacy imports'],
              ['src/App.tsx, src/GamePage.tsx', 'React landing + game HUD'],
              ['.github/workflows/ci.yml', 'Web: typecheck + build + GitHub Pages'],
              ['.github/workflows/native.yml', 'Native: CMake build + release on tag'],
            ].map(([path, purpose]) => (
              <tr key={path} className="hover:bg-slate-900/50">
                <td className="px-3 py-2 font-mono text-cyan-300 whitespace-nowrap">{path}</td>
                <td className="px-3 py-2 text-slate-400">{purpose}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <H3>Quick start</H3>
      <H3>Browser</H3>
      <Code lang="bash">{`npm install
npm run dev      # Vite dev server at http://localhost:5173
npm run build    # single-file dist/index.html
npm run preview  # serve the production build`}</Code>
      <H3>Native (Windows)</H3>
      <Code lang="bash">{`# Ninja + MSVC (recommended on CI)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# Binaries land in build/bin/

# Or Visual Studio generator
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release`}</Code>
    </div>
  );
}

/* ---------- TD Scripting Language ---------- */

function TdLangTab() {
  return (
    <div>
      <H2>TD Scripting Language</H2>
      <P>
        TD is a small, statically-typed scripting language designed for game logic. It ships with the
        C++ engine (<code className="text-cyan-300">src/td/</code> — lexer, parser, compiler, VM) and
        is intended to be loaded at runtime by a <code className="text-cyan-300">ScriptComponent</code>
        attached to an entity. The language is intentionally tiny: no classes, no generics, no
        exceptions. Just enough to express per-frame entity behaviour.
      </P>

      <H3>Hello, entity</H3>
      <Code lang="td">{`// Defines an entity prototype called "Player"
entity Player {
    let health: int = 100;
    let speed: float = 200.0;
    let name: string = "Hero";

    // Called every frame by the engine
    fn update(dt: float) {
        if input.key("left") {
            this.x -= speed * dt;
        }
        if input.key("right") {
            this.x += speed * dt;
        }
    }

    // Called by the physics system on collision
    fn onCollision(other: Entity) {
        if other.tag == "enemy" {
            health -= 10;
        }
    }
}`}</Code>

      <H3>Keywords</H3>
      <div className="flex flex-wrap gap-2 mb-6">
        {['let','fn','if','else','while','for','return','true','false','null','struct','entity','this'].map(k => (
          <Pill key={k} color="cyan">{k}</Pill>
        ))}
      </div>

      <H3>Types</H3>
      <div className="flex flex-wrap gap-2 mb-6">
        {['int','float','string','bool','void','Entity'].map(t => (
          <Pill key={t} color="pink">{t}</Pill>
        ))}
      </div>

      <H3>Operators</H3>
      <div className="grid md:grid-cols-2 gap-4 mb-6">
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-4">
          <div className="text-xs font-semibold text-slate-400 mb-2">ARITHMETIC</div>
          <Pill color="slate">+</Pill> <Pill color="slate">-</Pill>{' '}
          <Pill color="slate">*</Pill> <Pill color="slate">/</Pill>{' '}
          <Pill color="slate">%</Pill>
        </div>
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-4">
          <div className="text-xs font-semibold text-slate-400 mb-2">COMPARISON</div>
          <Pill color="slate">==</Pill> <Pill color="slate">!=</Pill>{' '}
          <Pill color="slate">&lt;</Pill> <Pill color="slate">&lt;=</Pill>{' '}
          <Pill color="slate">&gt;</Pill> <Pill color="slate">&gt;=</Pill>
        </div>
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-4">
          <div className="text-xs font-semibold text-slate-400 mb-2">LOGICAL</div>
          <Pill color="slate">&amp;&amp;</Pill> <Pill color="slate">||</Pill>{' '}
          <Pill color="slate">!</Pill>
        </div>
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-4">
          <div className="text-xs font-semibold text-slate-400 mb-2">ASSIGNMENT</div>
          <Pill color="slate">=</Pill> <Pill color="slate">+=</Pill>{' '}
          <Pill color="slate">-=</Pill> <Pill color="slate">*=</Pill>{' '}
          <Pill color="slate">/=</Pill>
        </div>
      </div>

      <H3>Statements</H3>
      <Code lang="td">{`// Variable declaration
let x: int = 10;
let name: string = "Player One";
let active: bool = true;

// If / else
if x > 5 && active {
    x = x - 1;
} else {
    x = 0;
}

// While loop
while x > 0 {
    x -= 1;
}

// For loop (C-style)
for let i: int = 0; i < 10; i += 1 {
    print(i);
}

// Function
fn add(a: int, b: int) : int {
    return a + b;
}

// Early return
fn update(dt: float) {
    if health <= 0 { return; }
    // ...
}`}</Code>

      <H3>Entity model</H3>
      <P>
        An <code className="text-cyan-300">entity</code> block declares a prototype. The engine
        instantiates one per <code className="text-cyan-300">ScriptComponent</code> that references
        it. Inside an entity's <code className="text-cyan-300">fn</code> bodies,
        <code className="text-cyan-300"> this</code> refers to the host entity, and
        <code className="text-cyan-300"> this.x</code> / <code className="text-cyan-300">this.y</code>
        are bound to the entity's <code className="text-cyan-300">PositionComponent</code>.
      </P>

      <H3>Built-in globals</H3>
      <div className="overflow-x-auto mb-6">
        <table className="w-full text-sm border border-slate-800 rounded-lg overflow-hidden">
          <thead className="bg-slate-900">
            <tr>
              <th className="text-left px-3 py-2 text-slate-300">Name</th>
              <th className="text-left px-3 py-2 text-slate-300">Type</th>
              <th className="text-left px-3 py-2 text-slate-300">Description</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-800">
            {[
              ['input.key(name: string)', 'bool', 'True while the named key is held. Names: "left", "right", "up", "down", "space", "a".."z".'],
              ['input.mouseX', 'float', 'Mouse X in world units.'],
              ['input.mouseY', 'float', 'Mouse Y in world units.'],
              ['print(...)', 'void', 'Logs to the engine console.'],
              ['spawn(prefab: string, x: float, y: float)', 'Entity', 'Instantiates an entity by prefab name.'],
              ['destroy(self)', 'void', 'Marks the entity for removal at end of frame.'],
            ].map(([n, t, d]) => (
              <tr key={n}>
                <td className="px-3 py-2 font-mono text-cyan-300 whitespace-nowrap">{n}</td>
                <td className="px-3 py-2 font-mono text-pink-300 whitespace-nowrap">{t}</td>
                <td className="px-3 py-2 text-slate-400">{d}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <H3>Loading a script (C++)</H3>
      <Code lang="cpp">{`#include "td/ecs/world.h"

auto enemy = world.createEntity("Enemy");
auto* script = world.addComponent<ScriptComponent>(enemy);
strcpy(script->scriptPath, "scripts/enemy.td");
script->initialized = false;  // VM will compile + run on first update`}</Code>

      <P>
        <span className="text-amber-300">Note:</span> the TypeScript port does not currently include
        a TD VM. Browser games write their logic in TypeScript directly against the engine API (see
        the <button onClick={() => location.hash = '#ts'} className="text-cyan-300 underline">TS / JS Port</button> tab).
        The TD language reference above applies to the C++ engine's VM in <code className="text-cyan-300">src/td/</code>.
      </P>
    </div>
  );
}

/* ---------- C++ Engine ---------- */

function CppTab() {
  return (
    <div>
      <H2>C++ Engine API</H2>
      <P>
        The native engine is in <code className="text-cyan-300">src/</code>. Everything is under the
        <code className="text-cyan-300"> td::</code> namespace. Zero external dependencies — even
        math, PNG decoding, and OBJ loading are written from scratch.
      </P>

      <H3>Module map</H3>
      <div className="overflow-x-auto mb-6">
        <table className="w-full text-sm border border-slate-800 rounded-lg overflow-hidden">
          <thead className="bg-slate-900">
            <tr>
              <th className="text-left px-3 py-2 text-slate-300">Header</th>
              <th className="text-left px-3 py-2 text-slate-300">Provides</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-800">
            {[
              ['core/math/{vec2,vec3,mat4}.h', 'Vector & matrix types, orthographic/perspective/lookAt'],
              ['core/game_loop.h', 'Fixed-step game loop with interpolation'],
              ['core/logger.h', 'TD_LOG_INFO / TD_LOG_ERROR macros'],
              ['core/memory.h', 'Linear allocator, pool allocator'],
              ['platform/win32_window.h', 'Win32Window, WindowConfig, InputState, Key enum'],
              ['platform/win32_input.h', 'Keyboard / mouse state'],
              ['renderer/gl_renderer.h', 'td::Renderer singleton (init, clear, viewport)'],
              ['renderer/sprite_batch.h', 'SpriteBatch — textured quads with batching'],
              ['renderer/camera.h', 'Camera2D / Camera3D with projection + view matrices'],
              ['renderer/texture.h', 'Texture loading + caching'],
              ['renderer/mesh.h', '3D mesh for OBJ-loaded geometry'],
              ['renderer/framebuffer.h', 'FBO wrapper for render-to-texture'],
              ['physics/aabb.h', 'AABB intersection tests'],
              ['physics/collision.h', 'Collision system (broad/narrow phase)'],
              ['physics/rigidbody.h', 'RigidBody dynamics'],
              ['audio/audio_engine.h', 'WAV playback via waveOut'],
              ['audio/mixer.h', 'Software mixer for multiple simultaneous sources'],
              ['net/server.h, net/client.h', 'TCP/UDP server & client over Winsock2'],
              ['assets/png_decoder.h', 'From-scratch PNG decoder (zlib inflate)'],
              ['assets/obj_loader.h', 'Wavefront OBJ loader'],
              ['ecs/world.h', 'World — entity/component management, system dispatch'],
              ['td/lexer.h, td/parser.h, td/compiler.h, td/vm.h', 'TD scripting language toolchain'],
            ].map(([h, p]) => (
              <tr key={h} className="hover:bg-slate-900/50">
                <td className="px-3 py-2 font-mono text-cyan-300 whitespace-nowrap">{h}</td>
                <td className="px-3 py-2 text-slate-400">{p}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <H3>ECS usage</H3>
      <Code lang="cpp">{`#include "td/ecs/world.h"
using namespace td;

World world;

// Create an entity and add components
EntityId player = world.createEntity("Player");
auto* pos = world.addComponent<PositionComponent>(player);
pos->x = 100; pos->y = 200;

auto* sprite = world.addComponent<SpriteComponent>(player);
sprite->width = 32; sprite->height = 32;
sprite->r = 1; sprite->g = 1; sprite->b = 1; sprite->a = 1;

auto* col = world.addComponent<ColliderComponent>(player);
col->width = 32; col->height = 32;

// Query entities with both Position and Sprite
ComponentMask mask = componentBit(ComponentType::Position)
                   | componentBit(ComponentType::Sprite);
EntityId entities[100];
int count = world.queryActive(mask, entities, 100);

for (int i = 0; i < count; i++) {
    auto* p = world.getComponent<PositionComponent>(entities[i]);
    auto* s = world.getComponent<SpriteComponent>(entities[i]);
    // ...
}`}</Code>

      <H3>Rendering a sprite</H3>
      <Code lang="cpp">{`#include "td/renderer/gl_renderer.h"
#include "td/renderer/sprite_batch.h"
#include "td/renderer/camera.h"

Renderer::get().init();
SpriteBatch batch;
batch.init();
Camera2D camera;
camera.setViewport(800, 600);

// In render callback:
Renderer::get().clear(0.1f, 0.1f, 0.15f);
Mat4 proj = camera.getProjection();
Mat4 view = camera.getView();

batch.begin(proj, view);
batch.drawQuad(100, 100, 64, 64, 1, 0, 0, 1);  // red quad
batch.end();`}</Code>

      <H3>Custom system</H3>
      <Code lang="cpp">{`class PhysicsSystem : public System {
public:
    void onUpdate(World& world, float dt) override {
        ComponentMask mask = componentBit(ComponentType::Position)
                           | componentBit(ComponentType::Velocity);
        EntityId entities[TD_MAX_ENTITIES];
        int count = world.queryActive(mask, entities, TD_MAX_ENTITIES);

        for (int i = 0; i < count; i++) {
            auto* p = world.getComponent<PositionComponent>(entities[i]);
            auto* v = world.getComponent<VelocityComponent>(entities[i]);
            p->prevX = p->x; p->prevY = p->y;
            p->x += v->vx * dt;
            p->y += v->vy * dt;
        }
    }
};

world.addSystem(new PhysicsSystem());`}</Code>

      <H3>Game loop</H3>
      <Code lang="cpp">{`#include "td/platform/win32_window.h"
#include "td/core/game_loop.h"

void init() { /* one-time setup */ }
void update(float dt) { /* game logic */ }
void render(float alpha) { /* draw */ }

int main() {
    Win32Window window;
    WindowConfig cfg;
    cfg.title = "My Game";
    cfg.width = 800; cfg.height = 600;
    window.create(cfg);

    GameLoop loop;
    loop.setCallbacks(init, update, render);
    loop.setFixedStep(1.0f / 60.0f);
    loop.run(window);
    return 0;
}`}</Code>
    </div>
  );
}

/* ---------- TS / JS Port ---------- */

function TsTab() {
  return (
    <div>
      <H2>TypeScript / JavaScript Port</H2>
      <P>
        The browser port mirrors the C++ engine's public API under
        <code className="text-cyan-300"> web/engine/</code>. The same shapes (
        <code className="text-cyan-300">World</code>,
        <code className="text-cyan-300">SpriteBatch</code>,
        <code className="text-cyan-300">Camera2D</code>,
        <code className="text-cyan-300">Input</code>,
        <code className="text-cyan-300">Mat4</code>) mean a C++ game translates almost line-for-line.
      </P>

      <H3>Engine modules</H3>
      <div className="overflow-x-auto mb-6">
        <table className="w-full text-sm border border-slate-800 rounded-lg overflow-hidden">
          <thead className="bg-slate-900">
            <tr>
              <th className="text-left px-3 py-2 text-slate-300">File</th>
              <th className="text-left px-3 py-2 text-slate-300">Mirrors C++</th>
              <th className="text-left px-3 py-2 text-slate-300">Exports</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-800">
            {[
              ['web/engine/math.ts', 'src/core/math/{vec2,vec3,vec4,mat4}.h', 'Vec2, Vec3, Vec4, Mat4, Color, clamp, lerp, degToRad'],
              ['web/engine/ecs.ts', 'src/ecs/*', 'World, ComponentType, componentBit, components'],
              ['web/engine/renderer.ts', 'src/renderer/{gl_renderer,sprite_batch}.{h,cpp}', 'Renderer, SpriteBatch (WebGL2)'],
              ['web/engine/camera.ts', 'src/renderer/camera.h', 'Camera2D'],
              ['web/engine/input.ts', 'src/platform/win32_input.h', 'Input, Key'],
              ['web/engine/engine.ts', 'src/core/game_loop.h + window bootstrap', 'Engine (top-level entry)'],
              ['web/engine-wrapper.ts', '— (compat shim)', 'Re-exports everything above'],
              ['web/js_bridge.ts', 'wasm/js_bridge.js (replaced)', 'Global window.TDEngine shim'],
            ].map(([f, m, e]) => (
              <tr key={f} className="hover:bg-slate-900/50">
                <td className="px-3 py-2 font-mono text-cyan-300 whitespace-nowrap">{f}</td>
                <td className="px-3 py-2 font-mono text-slate-400 text-xs">{m}</td>
                <td className="px-3 py-2 text-slate-400 text-xs">{e}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <H3>Hello, browser game</H3>
      <P>
        Minimal example: a bouncing quad. Drop this into a Vite + TS project that has
        <code className="text-cyan-300"> web/engine/</code> available.
      </P>
      <Code lang="ts">{`import { Engine } from './web/engine/engine';
import { Color } from './web/engine/math';

const canvas = document.querySelector('canvas')!;
const engine = new Engine({
  canvas,
  width: 800,
  height: 600,
  bgR: 0.05, bgG: 0.05, bgB: 0.08,
});
engine.init();

let x = 100, y = 100;
let vx = 200, vy = 160;

engine.setCallbacks(
  null,
  (dt) => {
    x += vx * dt; y += vy * dt;
    if (x < 0 || x > 800 - 32) vx = -vx;
    if (y < 0 || y > 600 - 32) vy = -vy;
  },
  () => {
    const batch = engine.getRenderer().getSpriteBatch();
    batch.begin(engine.getProjectionMatrix(), engine.getViewMatrix());
    batch.drawQuad(x, y, 32, 32, 1, 0.4, 0.8, 1);
    batch.end();
  },
);
engine.start();`}</Code>

      <H3>Using the ECS</H3>
      <Code lang="ts">{`import { Engine } from './web/engine/engine';
import { ComponentType, componentBit } from './web/engine/ecs';

const engine = new Engine({ canvas, width: 800, height: 600 });
engine.init();
const world = engine.getWorld();

// Create an entity
const enemy = world.createEntity('Enemy');
world.addPosition(enemy, 200, 100);
world.addVelocity(enemy, -50, 0);
world.addSprite({ width: 24, height: 24, r: 1, g: 0.3, b: 0.3 })(enemy);
world.addCollider({ width: 24, height: 24 })(enemy);

// Query entities with Position + Velocity
const mask = componentBit(ComponentType.Position)
           | componentBit(ComponentType.Velocity);
const ids = world.queryActive(mask);
for (const id of ids) {
  const pos = world.getPosition(id)!;
  const vel = world.getVelocity(id)!;
  pos.x += vel.vx * engine.getDelta();
  pos.y += vel.vy * engine.getDelta();
}`}</Code>

      <H3>Input</H3>
      <Code lang="ts">{`import { Key } from './web/engine/input';

const input = engine.getInput();
if (input.key(Key.W) || input.key(Key.Up)) { /* move up */ }
if (input.keyPressed(Key.Space)) { /* jump (edge-triggered) */ }
if (input.keyReleased(Key.Escape)) { /* pause */ }`}</Code>

      <H3>For static HTML pages (no bundler)</H3>
      <P>
        <code className="text-cyan-300">web/js_bridge.ts</code> exposes a global
        <code className="text-cyan-300"> window.TDEngine</code> shim with
        <code className="text-cyan-300"> init / start / stop / shutdown / onReady / onLog</code> —
        the same shape the original broken bridge pretended to provide. Because browsers cannot
        load <code className="text-cyan-300">.ts</code> files directly, you must compile it to
        JavaScript first (e.g. <code className="text-cyan-300">tsc</code> or
        <code className="text-cyan-300">esbuild</code>), then import the resulting
        <code className="text-cyan-300">.js</code> file.
      </P>
      <Code lang="bash">{`# Compile the bridge (and its engine deps) to a single JS file
npx esbuild web/js_bridge.ts --bundle --format=iife --globalName=TDEngine \\
  --outfile=public/td-engine.js

# Or with the TypeScript compiler
npx tsc web/js_bridge.ts --outDir public --module esnext --target es2020`}</Code>
      <Code lang="html">{`<canvas id="game-canvas" width="800" height="600"></canvas>
<script type="module">
  import { TDEngine } from './td-engine.js';   <!-- compiled, not .ts -->
  TDEngine.onLog(m => console.log(m));
  await TDEngine.init('game-canvas');
  TDEngine.start();
</script>`}</Code>
      <P>
        <span className="text-amber-300 font-semibold">Note:</span> If you're using Vite
        (as this repo does), you don't need the shim at all — just import
        <code className="text-cyan-300"> Engine</code> directly from
        <code className="text-cyan-300"> ./web/engine/engine</code> and Vite will bundle it.
      </P>
    </div>
  );
}

/* ---------- Example Games ---------- */

function ExamplesTab() {
  return (
    <div>
      <H2>Example Games</H2>
      <P>
        The repo ships with three complete games that demonstrate the engine end-to-end.
        The native examples are C++ and build with the rest of the engine; the browser example
        is TypeScript and runs in <code className="text-cyan-300">npm run dev</code>.
      </P>

      <div className="grid md:grid-cols-2 gap-4 mb-6">
        {/* Pong native */}
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-5">
          <div className="flex items-center justify-between mb-2">
            <h3 className="text-lg font-bold text-cyan-300">Pong <Pill color="cyan">C++</Pill></h3>
            <code className="text-xs text-slate-400 font-mono">examples/pong/main.cpp</code>
          </div>
          <P>
            Two-paddle Pong against a simple AI. Demonstrates ECS entity creation,
            <code className="text-cyan-300"> PositionComponent</code> +
            <code className="text-cyan-300"> VelocityComponent</code> +
            <code className="text-cyan-300"> SpriteComponent</code> +
            <code className="text-cyan-300"> ColliderComponent</code>, AABB collision response
            with paddle-spin, score tracking via window title.
          </P>
          <Code lang="bash">{`make run-pong
# or
./build/bin/Release/pong.exe`}</Code>
        </div>

        {/* Platformer native */}
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-5">
          <div className="flex items-center justify-between mb-2">
            <h3 className="text-lg font-bold text-cyan-300">Platformer <Pill color="cyan">C++</Pill></h3>
            <code className="text-xs text-slate-400 font-mono">examples/platformer/main.cpp</code>
          </div>
          <P>
            Side-scrolling platformer with gravity, jumping, platforms, patrolling enemies, and a
            score. Demonstrates <code className="text-cyan-300">RigidBodyComponent</code>,
            <code className="text-cyan-300"> useGravity</code>, AABB-vs-AABB resolution with
            ground detection, camera follow.
          </P>
          <Code lang="bash">{`make run-platformer
# or
./build/bin/Release/platformer.exe`}</Code>
        </div>

        {/* Pong:Rush browser */}
        <div className="bg-slate-900 border border-slate-800 rounded-lg p-5 md:col-span-2">
          <div className="flex items-center justify-between mb-2">
            <h3 className="text-lg font-bold text-pink-300">Pong:Rush <Pill color="pink">TS</Pill></h3>
            <code className="text-xs text-slate-400 font-mono">web/game/pong.ts</code>
          </div>
          <P>
            Polished browser Pong built on the TypeScript port. First to 7 wins. Features:
          </P>
          <ul className="text-sm text-slate-300 space-y-1 ml-4 list-disc mb-3">
            <li>Scene state machine: title → countdown → playing → scored → gameover</li>
            <li>AI opponent that predicts ball trajectory (including wall bounces) with reaction delay + error</li>
            <li>Particle system with burst, drag, gravity, fade-out (built on the ECS)</li>
            <li>Ball trail, screen shake on hits, slow-mo on heavy contact</li>
            <li>7-segment score display rendered via SpriteBatch (no DOM text in-canvas)</li>
            <li>React HUD overlays scene transitions, countdown, scores, controls help</li>
          </ul>
          <Code lang="bash">{`npm install
npm run dev     # then click "Play Pong:Rush"`}</Code>
        </div>
      </div>

      <H3>Architecture shared by all three</H3>
      <Code lang="text">{`┌──────────────────────────────────┐
│           Game Code              │   ← examples/*.cpp / web/game/*.ts
├──────────────────────────────────┤
│            Engine API            │   ← src/ / web/engine/
├──────┬──────┬──────┬─────────────┤
│ ECS  │Rendr │Input │  Particles  │
├──────┴──────┴──────┴─────────────┤
│   Math (Vec2/3, Mat4, Color)     │
├──────────────────────────────────┤
│     Platform (Win32 / WebGL2)    │
└──────────────────────────────────┘`}</Code>

      <H3>Reusing the engine in your own game</H3>
      <P>
        Copy <code className="text-cyan-300">web/game/pong.ts</code> as a starting point, rename the
        class, swap the <code className="text-cyan-300">onUpdate</code> / <code className="text-cyan-300">onRender</code>
        bodies, and you have a new game. The engine handles the game loop, fixed-step integration,
        input, WebGL2 context, and sprite batching — you just write per-frame logic.
      </P>
    </div>
  );
}

/* ---------- Editor ---------- */

function EditorTab() {
  return (
    <div>
      <H2>Visual Editor</H2>
      <P>
        The native editor is a Windows-only immediate-mode GUI application in
        <code className="text-cyan-300"> editor/</code>. It wraps the engine and provides a Unity-like
        workflow: scene hierarchy, inspector, asset browser, console, and menu bar.
      </P>

      <H3>Building</H3>
      <Code lang="bash">{`# CMake + Visual Studio
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Editor binary: build/bin/Release/td-editor.exe

# Or via the workflow's release artifact (see CI / Releases tab)
# Download td-engine-windows-x64.zip from the latest GitHub Release`}</Code>

      <H3>Panels</H3>
      <div className="grid md:grid-cols-2 gap-4 mb-6">
        {[
          ['Scene Panel', 'editor/scene_panel.cpp', 'Tree view of all entities in the world. Click to select, right-click for context menu (add child, rename, delete).'],
          ['Inspector', 'editor/inspector_panel.cpp', 'Property editor for the selected entity. Edits each component inline (position, sprite color, collider size, etc.).'],
          ['Asset Browser', 'editor/asset_browser.cpp', 'Filesystem browser rooted at assets/. Shows PNG/OBJ previews, click to assign to the selected entity\'s sprite/mesh.'],
          ['Console', 'editor/console_panel.cpp', 'Live log stream from the engine (TD_LOG_INFO / TD_LOG_ERROR). Collapsible by severity.'],
          ['Menu Bar', 'editor/menu_bar.cpp', 'File (New/Open/Save scene), Edit (Undo/Redo, Preferences), View (toggle panels), Help.'],
        ].map(([name, file, desc]) => (
          <div key={name} className="bg-slate-900 border border-slate-800 rounded-lg p-4">
            <div className="flex items-center justify-between mb-2">
              <span className="font-semibold text-white">{name}</span>
              <code className="text-xs text-slate-400 font-mono">{file}</code>
            </div>
            <P>{desc}</P>
          </div>
        ))}
      </div>

      <H3>Scene file format</H3>
      <P>
        Scenes are saved as text — the same format accepted by
        <code className="text-cyan-300"> td_load_scene()</code> in the WASM bridge. One entity per
        line, components as key=value pairs:
      </P>
      <Code lang="text">{`entity Player
  position 100 200
  sprite 32 32 #ffffff
  collider 32 32
  script scripts/player.td

entity Enemy
  position 400 100
  sprite 24 24 #ff4040
  collider 24 24`}</Code>

      <H3>Custom editor panels</H3>
      <P>
        The editor uses an immediate-mode GUI pattern. To add a custom panel, derive from
        <code className="text-cyan-300"> Panel</code> (or just write a free function) and call it
        from <code className="text-cyan-300"> editor/main.cpp</code>'s render loop:
      </P>
      <Code lang="cpp">{`// my_panel.cpp
void renderMyPanel(GuiContext& gui, World& world) {
    if (!gui.beginPanel("My Panel", 10, 10, 200, 300)) return;
    gui.label("Selected: " + std::string(world.getEntityName(gui.selectedEntity)));
    if (gui.button("Respawn", 80, 24)) {
        auto* pos = world.getComponent<PositionComponent>(gui.selectedEntity);
        if (pos) { pos->x = 0; pos->y = 0; }
    }
    gui.endPanel();
}`}</Code>

      <P>
        <span className="text-amber-300">Note:</span> the browser port does not ship a visual editor.
        Browser games compose scenes imperatively in TypeScript via
        <code className="text-cyan-300"> world.createEntity()</code> /
        <code className="text-cyan-300"> world.addPosition()</code> / etc., as shown in
        <code className="text-cyan-300"> web/game/pong.ts</code> → <code className="text-cyan-300">buildLevel()</code>.
      </P>
    </div>
  );
}

/* ---------- Workflows ---------- */

function WorkflowsTab() {
  return (
    <div>
      <H2>CI / Releases</H2>
      <P>
        Two GitHub Actions workflows keep the project healthy. Both live in
        <code className="text-cyan-300"> .github/workflows/</code>.
      </P>

      <H3>ci.yml — Web build + GitHub Pages</H3>
      <P>
        Runs on every push to <code className="text-cyan-300">main</code> / <code className="text-cyan-300">master</code>
        and on every PR. Uses Node 24. Runs <code className="text-cyan-300">tsc --noEmit</code> for typecheck,
        then <code className="text-cyan-300">npm run build</code> to produce a single-file
        <code className="text-cyan-300"> dist/index.html</code>. On <code className="text-cyan-300">main</code>,
        deploys that to GitHub Pages.
      </P>
      <Code lang="yaml">{`# .github/workflows/ci.yml (excerpt)
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with: { node-version: '24' }
      - run: npm install --no-audit --no-fund
      - run: npx tsc --noEmit
      - run: npm run build
      - uses: actions/upload-pages-artifact@v3
        with: { path: dist/ }
  deploy:
    needs: build
    if: github.ref == 'refs/heads/main'
    runs-on: ubuntu-latest
    environment: github-pages
    steps:
      - uses: actions/deploy-pages@v4`}</Code>

      <H3>native.yml — C++ build + release</H3>
      <P>
        Runs on every push and PR. Builds the C++ engine + editor + examples on
        <code className="text-cyan-300"> windows-latest</code> with CMake + Visual Studio 2022.
        Runs the unit tests (<code className="text-cyan-300">test_math</code>,
        <code className="text-cyan-300">test_ecs</code>, <code className="text-cyan-300">test_physics</code>).
        Stages everything (binaries, assets, headers, examples, README) into
        <code className="text-cyan-300"> td-engine-windows-x64.zip</code> and uploads it as a build
        artifact (14-day retention).
      </P>
      <P>
        When you push a tag matching <code className="text-cyan-300">v*.*.*</code> (e.g.
        <code className="text-cyan-300"> v1.0.0</code>), the same workflow attaches the zip to a
        GitHub Release with auto-generated release notes.
      </P>
      <Code lang="yaml">{`# .github/workflows/native.yml (excerpt)
jobs:
  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - run: cmake -B build -S . -G "Visual Studio 17 2022" -A x64
      - run: cmake --build build --config Release --parallel
      - run: bin\\Release\\test_math.exe
      # ...stage + zip...
      - uses: softprops/action-gh-release@v2
        if: startsWith(github.ref, 'refs/tags/v')
        with: { files: td-engine-windows-x64.zip }`}</Code>

      <H3>How to cut a release</H3>
      <Code lang="bash">{`git tag v1.0.0
git push origin v1.0.0
# The native.yml workflow will build + publish a GitHub Release
# with td-engine-windows-x64.zip attached, at:
#   https://github.com/hacvilke/td/releases/tag/v1.0.0`}</Code>

      <H3>Enabling GitHub Pages (one-time)</H3>
      <ol className="text-sm text-slate-300 space-y-2 list-decimal ml-5 mb-6">
        <li>Open <code className="text-cyan-300">Settings → Pages</code> in the repo.</li>
        <li>Under <span className="font-semibold">Build and deployment → Source</span>, select <span className="font-semibold">GitHub Actions</span>.</li>
        <li>The next push to <code className="text-cyan-300">main</code> will publish the game to <code className="text-cyan-300">https://hacvilke.github.io/td/</code>.</li>
      </ol>

      <H3>Workflow artifacts at a glance</H3>
      <div className="overflow-x-auto mb-6">
        <table className="w-full text-sm border border-slate-800 rounded-lg overflow-hidden">
          <thead className="bg-slate-900">
            <tr>
              <th className="text-left px-3 py-2 text-slate-300">Workflow</th>
              <th className="text-left px-3 py-2 text-slate-300">Trigger</th>
              <th className="text-left px-3 py-2 text-slate-300">Output</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-800">
            {[
              ['ci.yml', 'push to main, any PR', 'GitHub Pages (single-file dist/index.html)'],
              ['native.yml', 'push to main, any PR, tag v*.*.*', 'td-engine-windows-x64.zip (release asset on tags)'],
            ].map(([w, t, o]) => (
              <tr key={w}>
                <td className="px-3 py-2 font-mono text-cyan-300">{w}</td>
                <td className="px-3 py-2 text-slate-400">{t}</td>
                <td className="px-3 py-2 text-slate-400">{o}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}
