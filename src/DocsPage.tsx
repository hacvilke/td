import { useState } from 'react';

type Tab = 'overview' | 'td-lang' | 'cpp' | 'ts' | 'examples' | 'editor' | 'workflows';

const TABS: { id: Tab; label: string; hint: string }[] = [
  { id: 'overview',  label: 'Overview',        hint: 'What this is, where things live' },
  { id: 'td-lang',   label: 'TD Scripting',    hint: 'Embedded scripting language' },
  { id: 'cpp',       label: 'C++ Engine',      hint: 'Native API reference' },
  { id: 'ts',        label: 'TS / JS Port',    hint: 'Browser port reference' },
  { id: 'examples',  label: 'Example Games',   hint: 'Pong, Platformer, Pong:Rush' },
  { id: 'editor',    label: 'Editor',          hint: 'Native visual editor' },
  { id: 'workflows', label: 'CI / Releases',   hint: 'GitHub Actions, Pages, releases' },
];

export default function DocsPage() {
  const [tab, setTab] = useState<Tab>('overview');
  const activeIdx = TABS.findIndex(t => t.id === tab);

  return (
    <div className="min-h-screen bg-white text-neutral-900 antialiased">
      {/* Top nav — matches the landing page */}
      <header className="border-b border-neutral-200 sticky top-0 z-50 bg-white/95 backdrop-blur">
        <div className="max-w-6xl mx-auto px-6 h-14 flex items-center justify-between">
          <a href="/" className="flex items-center gap-2 group">
            <span className="w-7 h-7 rounded-md border border-neutral-900 flex items-center justify-center text-[11px] font-bold tracking-tight group-hover:bg-neutral-900 group-hover:text-white transition-colors">
              TD
            </span>
            <span className="text-sm font-semibold tracking-tight">TD Engine — Docs</span>
            <span className="text-[11px] text-neutral-400 font-mono ml-1 hidden sm:inline">v1.0</span>
          </a>
          <nav className="flex items-center gap-1 text-sm">
            <a href="/" className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Home</a>
            <a href="/#game" className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Play</a>
            <a href="https://github.com/hacvilke/td/releases" className="px-3 py-1.5 text-neutral-600 hover:text-neutral-900 transition-colors">Releases</a>
            <a
              href="https://github.com/hacvilke/td"
              className="ml-2 px-3 py-1.5 rounded-md border border-neutral-300 hover:border-neutral-900 text-neutral-700 hover:text-neutral-900 transition-colors text-sm font-medium inline-flex items-center gap-1.5"
            >
              <svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true"><path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0016 8c0-4.42-3.58-8-8-8z"/></svg>
              <span>Source</span>
            </a>
          </nav>
        </div>
      </header>

      {/* Two-column body: sticky sidebar + content */}
      <div className="max-w-6xl mx-auto px-6 py-10 grid lg:grid-cols-[220px_1fr] gap-10">
        {/* Sidebar */}
        <nav className="lg:sticky lg:top-20 lg:self-start">
          <p className="text-[11px] font-mono uppercase tracking-wider text-neutral-500 mb-3 hidden lg:block">Contents</p>
          <ul className="flex lg:flex-col gap-1 overflow-x-auto pb-2 lg:pb-0 -mx-1 px-1">
            {TABS.map((t, i) => (
              <li key={t.id}>
                <button
                  onClick={() => setTab(t.id)}
                  className={`w-full text-left px-3 py-2 rounded-md text-sm whitespace-nowrap transition-colors ${
                    tab === t.id
                      ? 'bg-neutral-900 text-white'
                      : 'text-neutral-600 hover:text-neutral-900 hover:bg-neutral-100'
                  }`}
                >
                  <span className="font-mono text-[10px] text-neutral-400 mr-2">{String(i + 1).padStart(2, '0')}</span>
                  {t.label}
                </button>
                {tab === t.id && (
                  <p className="hidden lg:block text-[11px] text-neutral-500 mt-1 px-3 leading-relaxed">{t.hint}</p>
                )}
              </li>
            ))}
          </ul>

          <div className="hidden lg:block mt-8 pt-6 border-t border-neutral-200 text-xs text-neutral-500">
            <p className="font-mono">Engine v1.0</p>
            <p className="mt-1">MIT License — see <a href="https://github.com/hacvilke/td/blob/main/LICENSE" className="underline hover:text-neutral-900">LICENSE</a></p>
            <a href="https://github.com/hacvilke/td/issues" className="mt-3 inline-block underline hover:text-neutral-900">Report an issue →</a>
          </div>
        </nav>

        {/* Content */}
        <main className="min-w-0">
          {/* Section indicator strip */}
          <div className="flex items-center gap-2 mb-6 text-[11px] font-mono text-neutral-500">
            <span>Docs</span>
            <span>/</span>
            <span className="text-neutral-900">{TABS[activeIdx].label}</span>
          </div>

          {tab === 'overview'  && <OverviewTab />}
          {tab === 'td-lang'   && <TdLangTab />}
          {tab === 'cpp'       && <CppTab />}
          {tab === 'ts'        && <TsTab />}
          {tab === 'examples'  && <ExamplesTab />}
          {tab === 'editor'    && <EditorTab />}
          {tab === 'workflows' && <WorkflowsTab />}
        </main>
      </div>
    </div>
  );
}

/* ---------- shared bits ---------- */

function Code({ children, lang = 'cpp' }: { children: string; lang?: string }) {
  return (
    <pre className="bg-neutral-900 border border-neutral-800 rounded-md p-4 overflow-x-auto text-[12px] leading-[1.55] font-mono text-neutral-300 my-4">
      <code data-lang={lang}>{children}</code>
    </pre>
  );
}

function H2({ children }: { children: React.ReactNode }) {
  return <h2 className="text-2xl font-semibold tracking-tight text-neutral-900 mt-2 mb-3">{children}</h2>;
}

function H3({ children }: { children: React.ReactNode }) {
  return <h3 className="text-[11px] font-mono uppercase tracking-wider text-neutral-500 mt-8 mb-2">{children}</h3>;
}

function P({ children }: { children: React.ReactNode }) {
  return <p className="text-sm text-neutral-700 leading-relaxed mb-3">{children}</p>;
}

function Mono({ children }: { children: React.ReactNode }) {
  return <code className="font-mono text-[12px] bg-neutral-200 px-1 py-0.5 rounded text-neutral-900">{children}</code>;
}

function Table({ rows }: { rows: [string, string, string?][] }) {
  return (
    <div className="overflow-x-auto my-4 border border-neutral-200 rounded-md">
      <table className="w-full text-sm">
        <thead className="bg-neutral-50 border-b border-neutral-200">
          <tr>
            <th className="text-left px-3 py-2 text-[11px] font-mono uppercase tracking-wider text-neutral-500">Name</th>
            <th className="text-left px-3 py-2 text-[11px] font-mono uppercase tracking-wider text-neutral-500">{rows[0][1] ? 'Detail' : 'Detail'}</th>
            {rows[0][2] !== undefined && (
              <th className="text-left px-3 py-2 text-[11px] font-mono uppercase tracking-wider text-neutral-500">Extra</th>
            )}
          </tr>
        </thead>
        <tbody className="divide-y divide-neutral-200">
          {rows.map((r) => (
            <tr key={r[0]} className="hover:bg-neutral-50">
              <td className="px-3 py-2 font-mono text-[12px] text-neutral-900 whitespace-nowrap align-top">{r[0]}</td>
              <td className="px-3 py-2 text-neutral-600 align-top">{r[1]}</td>
              {r[2] !== undefined && <td className="px-3 py-2 text-neutral-600 text-xs align-top">{r[2]}</td>}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

function Pill({ children }: { children: React.ReactNode }) {
  return (
    <span className="inline-block px-2 py-0.5 rounded text-[10px] font-mono uppercase tracking-wider bg-neutral-100 text-neutral-600 border border-neutral-200">
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
        dependencies, plus a TypeScript port that runs in any modern browser. The C++ source tree in
        <Mono>src/</Mono> is the engine itself — Win32 + OpenGL 3.3 + Winsock + waveOut, with an ECS,
        SpriteBatch, AABB physics, audio mixer, networking, asset pipeline, and the TD scripting
        language. The TypeScript port in <Mono>web/engine/</Mono> mirrors the parts needed to run games
        in a browser (ECS, SpriteBatch, Camera2D, Input, Math) — a subset, not a parallel codebase.
      </P>

      <H3>The engine, and a browser port of part of it</H3>
      <div className="grid md:grid-cols-2 gap-4 mb-2">
        <div className="border border-neutral-200 rounded-md p-5">
          <div className="flex items-center justify-between mb-2">
            <span className="text-sm font-semibold text-neutral-900">The engine</span>
            <Pill>C++17 · src/</Pill>
          </div>
          <P>
            Builds on Windows with MinGW or Visual Studio 2019+. Uses Win32 for windowing/input,
            OpenGL 3.3 for rendering, Winsock2 for networking, waveOut for audio. ~80 files across
            <Mono>core/</Mono>, <Mono>platform/</Mono>, <Mono>renderer/</Mono>, <Mono>physics/</Mono>,
            <Mono>audio/</Mono>, <Mono>net/</Mono>, <Mono>assets/</Mono>, <Mono>ecs/</Mono>, <Mono>td/</Mono>.
          </P>
        </div>
        <div className="border border-neutral-200 rounded-md p-5">
          <div className="flex items-center justify-between mb-2">
            <span className="text-sm font-semibold text-neutral-900">Browser port (subset)</span>
            <Pill>TS · web/engine/</Pill>
          </div>
          <P>
            Runs in any WebGL2-capable browser. Mirrors the C++ API 1:1 (Vec2/3/4, Mat4, World,
            SpriteBatch, Camera2D, Input, GameLoop). 6 files — enough to run Pong:Rush. Audio,
            networking, 3D, asset pipeline, and the TD scripting VM are not ported.
          </P>
        </div>
      </div>

      <H3>What's where</H3>
      <Table rows={[
        ['src/',                 'C++ engine source (math, ECS, renderer, physics, audio, net, scripting)'],
        ['editor/',              'Native visual editor (immediate-mode GUI, scene/inspector/asset panels)'],
        ['examples/',            'Native example games: pong/, platformer/'],
        ['tests/',               'C++ unit tests (math, ECS, physics)'],
        ['assets/shaders/',      'GLSL shaders (sprite.vert/frag, basic_3d.vert/frag)'],
        ['web/engine/',          'TypeScript port of the engine core (subset)'],
        ['web/game/',            'Browser games (Pong:Rush, particle system)'],
        ['web/js_bridge.ts',     'Global window.TDEngine shim for static HTML pages'],
        ['web/engine-wrapper.ts','Re-exports the TS engine for legacy imports'],
        ['src/App.tsx, src/GamePage.tsx', 'React landing + game HUD'],
        ['.github/workflows/ci.yml',      'Web: typecheck + build + GitHub Pages'],
        ['.github/workflows/native.yml',  'Native: CMake build + release on tag'],
      ]} />

      <H3>Quick start — browser</H3>
      <Code lang="bash">{`npm install
npm run dev      # Vite dev server at http://localhost:5173
npm run build    # single-file dist/index.html
npm run preview  # serve the production build`}</Code>

      <H3>Quick start — native (Windows)</H3>
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
        C++ engine (<Mono>src/td/</Mono> — lexer, parser, compiler, VM) and is intended to be loaded at
        runtime by a <Mono>ScriptComponent</Mono> attached to an entity. The language is intentionally
        tiny: no classes, no generics, no exceptions. Just enough to express per-frame entity behaviour.
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
      <div className="flex flex-wrap gap-1.5 mb-2">
        {['let','fn','if','else','while','for','return','true','false','null','struct','entity','this'].map(k => (
          <Pill key={k}>{k}</Pill>
        ))}
      </div>

      <H3>Types</H3>
      <div className="flex flex-wrap gap-1.5 mb-2">
        {['int','float','string','bool','void','Entity'].map(t => (
          <Pill key={t}>{t}</Pill>
        ))}
      </div>

      <H3>Operators</H3>
      <div className="grid md:grid-cols-2 gap-4 my-4">
        <div className="border border-neutral-200 rounded-md p-4">
          <div className="text-[11px] font-mono uppercase tracking-wider text-neutral-500 mb-2">Arithmetic</div>
          <div className="flex flex-wrap gap-1.5"><Pill>+</Pill><Pill>-</Pill><Pill>*</Pill><Pill>/</Pill><Pill>%</Pill></div>
        </div>
        <div className="border border-neutral-200 rounded-md p-4">
          <div className="text-[11px] font-mono uppercase tracking-wider text-neutral-500 mb-2">Comparison</div>
          <div className="flex flex-wrap gap-1.5"><Pill>==</Pill><Pill>!=</Pill><Pill>&lt;</Pill><Pill>&lt;=</Pill><Pill>&gt;</Pill><Pill>&gt;=</Pill></div>
        </div>
        <div className="border border-neutral-200 rounded-md p-4">
          <div className="text-[11px] font-mono uppercase tracking-wider text-neutral-500 mb-2">Logical</div>
          <div className="flex flex-wrap gap-1.5"><Pill>&amp;&amp;</Pill><Pill>||</Pill><Pill>!</Pill></div>
        </div>
        <div className="border border-neutral-200 rounded-md p-4">
          <div className="text-[11px] font-mono uppercase tracking-wider text-neutral-500 mb-2">Assignment</div>
          <div className="flex flex-wrap gap-1.5"><Pill>=</Pill><Pill>+=</Pill><Pill>-=</Pill><Pill>*=</Pill><Pill>/=</Pill></div>
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
        An <Mono>entity</Mono> block declares a prototype. The engine instantiates one per
        <Mono>ScriptComponent</Mono> that references it. Inside an entity's <Mono>fn</Mono> bodies,
        <Mono>this</Mono> refers to the host entity, and <Mono>this.x</Mono> / <Mono>this.y</Mono>
        are bound to the entity's <Mono>PositionComponent</Mono>.
      </P>

      <H3>Built-in globals</H3>
      <Table rows={[
        ['input.key(name: string)', 'bool — True while the named key is held. Names: "left", "right", "up", "down", "space", "a".."z".'],
        ['input.mouseX',           'float — Mouse X in world units.'],
        ['input.mouseY',           'float — Mouse Y in world units.'],
        ['print(...)',             'void — Logs to the engine console.'],
        ['spawn(prefab, x, y)',    'Entity — Instantiates an entity by prefab name.'],
        ['destroy(self)',          'void — Marks the entity for removal at end of frame.'],
      ]} />

      <H3>Loading a script (C++)</H3>
      <Code lang="cpp">{`#include "td/ecs/world.h"

auto enemy = world.createEntity("Enemy");
auto* script = world.addComponent<ScriptComponent>(enemy);
strcpy(script->scriptPath, "scripts/enemy.td");
script->initialized = false;  // VM will compile + run on first update`}</Code>

      <P>
        <span className="text-neutral-900 font-medium">Note:</span> the TypeScript port does not currently
        include a TD VM. Browser games write their logic in TypeScript directly against the engine API
        (see the TS / JS Port tab). The TD language reference above applies to the C++ engine's VM in
        <Mono>src/td/</Mono>.
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
        The native engine is in <Mono>src/</Mono>. Everything is under the <Mono>td::</Mono> namespace.
        Zero external dependencies — even math, PNG decoding, and OBJ loading are written from scratch.
      </P>

      <H3>Module map</H3>
      <Table rows={[
        ['core/math/{vec2,vec3,mat4}.h',                       'Vector & matrix types, orthographic/perspective/lookAt'],
        ['core/game_loop.h',                                    'Fixed-step game loop with interpolation'],
        ['core/logger.h',                                       'TD_LOG_INFO / TD_LOG_ERROR macros'],
        ['core/memory.h',                                       'Linear allocator, pool allocator'],
        ['platform/win32_window.h',                             'Win32Window, WindowConfig, InputState, Key enum'],
        ['platform/win32_input.h',                              'Keyboard / mouse state'],
        ['renderer/gl_renderer.h',                              'td::Renderer singleton (init, clear, viewport)'],
        ['renderer/sprite_batch.h',                             'SpriteBatch — textured quads with batching'],
        ['renderer/camera.h',                                   'Camera2D / Camera3D with projection + view matrices'],
        ['renderer/texture.h',                                  'Texture loading + caching'],
        ['renderer/mesh.h',                                     '3D mesh for OBJ-loaded geometry'],
        ['renderer/framebuffer.h',                              'FBO wrapper for render-to-texture'],
        ['physics/aabb.h',                                      'AABB intersection tests'],
        ['physics/collision.h',                                 'Collision system (broad/narrow phase)'],
        ['physics/rigidbody.h',                                 'RigidBody dynamics'],
        ['audio/audio_engine.h',                                'WAV playback via waveOut'],
        ['audio/mixer.h',                                       'Software mixer for multiple simultaneous sources'],
        ['net/server.h, net/client.h',                          'TCP/UDP server & client over Winsock2'],
        ['assets/png_decoder.h',                                'From-scratch PNG decoder (zlib inflate)'],
        ['assets/obj_loader.h',                                 'Wavefront OBJ loader'],
        ['ecs/world.h',                                         'World — entity/component management, system dispatch'],
        ['td/{lexer,parser,compiler,vm}.h',                    'TD scripting language toolchain'],
      ]} />

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
        The browser port mirrors the C++ engine's public API under <Mono>web/engine/</Mono>. The same
        shapes (<Mono>World</Mono>, <Mono>SpriteBatch</Mono>, <Mono>Camera2D</Mono>, <Mono>Input</Mono>,
        <Mono>Mat4</Mono>) mean a C++ game translates almost line-for-line. It is a subset — audio,
        networking, 3D meshes, asset pipeline, and the TD scripting VM are not ported.
      </P>

      <H3>Engine modules</H3>
      <Table rows={[
        ['web/engine/math.ts',      'src/core/math/{vec2,vec3,vec4,mat4}.h',          'Vec2, Vec3, Vec4, Mat4, Color, clamp, lerp, degToRad'],
        ['web/engine/ecs.ts',       'src/ecs/*',                                      'World, ComponentType, componentBit, components'],
        ['web/engine/renderer.ts',  'src/renderer/{gl_renderer,sprite_batch}.{h,cpp}','Renderer, SpriteBatch (WebGL2)'],
        ['web/engine/camera.ts',    'src/renderer/camera.h',                          'Camera2D'],
        ['web/engine/input.ts',     'src/platform/win32_input.h',                     'Input, Key'],
        ['web/engine/engine.ts',    'src/core/game_loop.h + window bootstrap',        'Engine (top-level entry)'],
        ['web/engine-wrapper.ts',   '— (compat shim)',                                'Re-exports everything above'],
        ['web/js_bridge.ts',        'wasm/js_bridge.js (replaced)',                   'Global window.TDEngine shim'],
      ]} />

      <H3>Hello, browser game</H3>
      <P>
        Minimal example: a bouncing quad. Drop this into a Vite + TS project that has
        <Mono>web/engine/</Mono> available.
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
        <Mono>web/js_bridge.ts</Mono> exposes a global <Mono>window.TDEngine</Mono> shim with
        <Mono>init / start / stop / shutdown / onReady / onLog</Mono> — the same shape the original
        broken bridge pretended to provide. Because browsers cannot load <Mono>.ts</Mono> files
        directly, you must compile it to JavaScript first (e.g. <Mono>tsc</Mono> or
        <Mono>esbuild</Mono>), then import the resulting <Mono>.js</Mono> file.
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
        <span className="text-neutral-900 font-medium">Note:</span> If you're using Vite (as this repo
        does), you don't need the shim at all — just import <Mono>Engine</Mono> directly from
        <Mono>./web/engine/engine</Mono> and Vite will bundle it.
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
        The repo ships with three complete games that demonstrate the engine end-to-end. The native
        examples are C++ and build with the rest of the engine; the browser example is TypeScript and
        runs in <Mono>npm run dev</Mono>.
      </P>

      <div className="grid md:grid-cols-2 gap-4 mb-2">
        {/* Pong native */}
        <div className="border border-neutral-200 rounded-md p-5">
          <div className="flex items-center justify-between mb-2">
            <div className="flex items-center gap-2">
              <span className="text-base font-semibold text-neutral-900">Pong</span>
              <Pill>C++</Pill>
            </div>
            <code className="text-[11px] text-neutral-500 font-mono">examples/pong/main.cpp</code>
          </div>
          <P>
            Two-paddle Pong against a simple AI. Demonstrates ECS entity creation,
            <Mono>PositionComponent</Mono> + <Mono>VelocityComponent</Mono> +
            <Mono>SpriteComponent</Mono> + <Mono>ColliderComponent</Mono>, AABB collision response with
            paddle-spin, score tracking via window title.
          </P>
          <Code lang="bash">{`make run-pong
# or
./build/bin/Release/pong.exe`}</Code>
        </div>

        {/* Platformer native */}
        <div className="border border-neutral-200 rounded-md p-5">
          <div className="flex items-center justify-between mb-2">
            <div className="flex items-center gap-2">
              <span className="text-base font-semibold text-neutral-900">Platformer</span>
              <Pill>C++</Pill>
            </div>
            <code className="text-[11px] text-neutral-500 font-mono">examples/platformer/main.cpp</code>
          </div>
          <P>
            Side-scrolling platformer with gravity, jumping, platforms, patrolling enemies, and a
            score. Demonstrates <Mono>RigidBodyComponent</Mono>, <Mono>useGravity</Mono>, AABB-vs-AABB
            resolution with ground detection, camera follow.
          </P>
          <Code lang="bash">{`make run-platformer
# or
./build/bin/Release/platformer.exe`}</Code>
        </div>

        {/* Pong:Rush browser */}
        <div className="border border-neutral-200 rounded-md p-5 md:col-span-2">
          <div className="flex items-center justify-between mb-2">
            <div className="flex items-center gap-2">
              <span className="text-base font-semibold text-neutral-900">Pong:Rush</span>
              <Pill>TS</Pill>
            </div>
            <code className="text-[11px] text-neutral-500 font-mono">web/game/pong.ts</code>
          </div>
          <P>Polished browser Pong built on the TypeScript port. First to 7 wins. Features:</P>
          <ul className="text-sm text-neutral-700 space-y-1 ml-4 list-disc mb-3">
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
        Copy <Mono>web/game/pong.ts</Mono> as a starting point, rename the class, swap the
        <Mono>onUpdate</Mono> / <Mono>onRender</Mono> bodies, and you have a new game. The engine
        handles the game loop, fixed-step integration, input, WebGL2 context, and sprite batching —
        you just write per-frame logic.
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
        The native editor is a Windows-only immediate-mode GUI application in <Mono>editor/</Mono>. It
        wraps the engine and provides a Unity-like workflow: scene hierarchy, inspector, asset
        browser, console, and menu bar.
      </P>

      <H3>Building</H3>
      <Code lang="bash">{`# CMake + Visual Studio
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Editor binary: build/bin/Release/td-editor.exe

# Or via the workflow's release artifact (see CI / Releases tab)
# Download td-engine-windows-x64.zip from the latest GitHub Release`}</Code>

      <H3>Panels</H3>
      <div className="grid md:grid-cols-2 gap-4 my-4">
        {[
          ['Scene Panel',    'editor/scene_panel.cpp',     'Tree view of all entities in the world. Click to select, right-click for context menu (add child, rename, delete).'],
          ['Inspector',      'editor/inspector_panel.cpp', 'Property editor for the selected entity. Edits each component inline (position, sprite color, collider size, etc.).'],
          ['Asset Browser',  'editor/asset_browser.cpp',   'Filesystem browser rooted at assets/. Shows PNG/OBJ previews, click to assign to the selected entity\'s sprite/mesh.'],
          ['Console',        'editor/console_panel.cpp',   'Live log stream from the engine (TD_LOG_INFO / TD_LOG_ERROR). Collapsible by severity.'],
          ['Menu Bar',       'editor/menu_bar.cpp',        'File (New/Open/Save scene), Edit (Undo/Redo, Preferences), View (toggle panels), Help.'],
        ].map(([name, file, desc]) => (
          <div key={name} className="border border-neutral-200 rounded-md p-4">
            <div className="flex items-center justify-between mb-2">
              <span className="font-semibold text-neutral-900 text-sm">{name}</span>
              <code className="text-[11px] text-neutral-500 font-mono">{file}</code>
            </div>
            <p className="text-sm text-neutral-600 leading-relaxed">{desc}</p>
          </div>
        ))}
      </div>

      <H3>Scene file format</H3>
      <P>
        Scenes are saved as text — the same format accepted by <Mono>td_load_scene()</Mono> in the
        WASM bridge. One entity per line, components as key=value pairs:
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
        <Mono>Panel</Mono> (or just write a free function) and call it from
        <Mono>editor/main.cpp</Mono>'s render loop:
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
        <span className="text-neutral-900 font-medium">Note:</span> the browser port does not ship a
        visual editor. Browser games compose scenes imperatively in TypeScript via
        <Mono>world.createEntity()</Mono> / <Mono>world.addPosition()</Mono> / etc., as shown in
        <Mono>web/game/pong.ts</Mono> → <Mono>buildLevel()</Mono>.
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
        <Mono>.github/workflows/</Mono>.
      </P>

      <H3>ci.yml — Web build + GitHub Pages</H3>
      <P>
        Runs on every push to <Mono>main</Mono> / <Mono>master</Mono> and on every PR. Uses Node 24.
        Runs <Mono>tsc --noEmit</Mono> for typecheck, then <Mono>npm run build</Mono> to produce a
        single-file <Mono>dist/index.html</Mono>. On <Mono>main</Mono>, deploys that to GitHub Pages.
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
        <Mono>windows-latest</Mono> with CMake + Ninja + Visual Studio 2022 (located via
        <Mono>vswhere</Mono> so it survives runner-image rotations). Runs the unit tests
        (<Mono>test_math</Mono>, <Mono>test_ecs</Mono>, <Mono>test_physics</Mono>). Stages everything
        (binaries, assets, headers, examples, README, LICENSE) into
        <Mono>td-engine-windows-x64.zip</Mono> and uploads it as a build artifact (30-day retention).
      </P>
      <P>
        When you push a tag matching <Mono>v*.*.*</Mono> (e.g. <Mono>v1.0.0</Mono>), the same workflow
        attaches the zip to a GitHub Release with auto-generated release notes.
      </P>
      <Code lang="yaml">{`# .github/workflows/native.yml (excerpt)
jobs:
  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      # Locate VS via vswhere, then call VsDevCmd.bat + cmake
      - name: Configure (Ninja + MSVC)
        shell: pwsh
        run: |
          $vswhere = "\${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
          $vsPath  = & $vswhere -latest -property installationPath
          cmd /c "call \`"$vsPath\Common7\Tools\VsDevCmd.bat\`" -arch=amd64 -host_arch=x64 && cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release"
      - name: Build
        shell: pwsh
        run: |
          $vswhere = "\${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
          $vsPath  = & $vswhere -latest -property installationPath
          cmd /c "call \`"$vsPath\Common7\Tools\VsDevCmd.bat\`" -arch=amd64 -host_arch=x64 && cmake --build build --parallel"
      - run: build\\bin\\test_math.exe
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
      <ol className="text-sm text-neutral-700 space-y-2 list-decimal ml-5 mb-4">
        <li>Open <Mono>Settings → Pages</Mono> in the repo.</li>
        <li>Under <span className="font-medium">Build and deployment → Source</span>, select <span className="font-medium">GitHub Actions</span>.</li>
        <li>The next push to <Mono>main</Mono> will publish the game to <Mono>https://hacvilke.github.io/td/</Mono>.</li>
      </ol>

      <H3>Workflow artifacts at a glance</H3>
      <Table rows={[
        ['ci.yml',      'push to main, any PR',                         'GitHub Pages (single-file dist/index.html)'],
        ['native.yml',  'push to main, any PR, tag v*.*.*',             'td-engine-windows-x64.zip (release asset on tags)'],
      ]} />
    </div>
  );
}
