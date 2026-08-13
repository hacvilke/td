import { useState } from 'react';

// File structure for TD Engine
const engineStructure = {
  'Platform': ['platform.h', 'win32_window.h', 'win32_window.cpp', 'win32_input.h', 'win32_input.cpp'],
  'Core': ['math.h', 'vec2.h', 'vec3.h', 'mat4.h', 'game_loop.h', 'game_loop.cpp', 'memory.h', 'logger.h', 'logger.cpp'],
  'Renderer': ['gl_renderer.h', 'gl_renderer.cpp', 'gl_shader.h', 'gl_shader.cpp', 'texture.h', 'texture.cpp', 'sprite_batch.h', 'sprite_batch.cpp', 'camera.h', 'camera.cpp', 'mesh.h', 'mesh.cpp', 'framebuffer.h', 'framebuffer.cpp'],
  'Physics': ['aabb.h', 'aabb.cpp', 'collision.h', 'collision.cpp', 'rigidbody.h', 'rigidbody.cpp'],
  'Audio': ['audio_engine.h', 'audio_engine.cpp', 'wav_loader.h', 'wav_loader.cpp', 'mixer.h', 'mixer.cpp'],
  'Networking': ['socket.h', 'socket.cpp', 'server.h', 'server.cpp', 'client.h', 'client.cpp'],
  'Assets': ['asset_loader.h', 'asset_loader.cpp', 'png_decoder.h', 'png_decoder.cpp', 'obj_loader.h', 'obj_loader.cpp'],
  'ECS': ['entity.h', 'entity.cpp', 'component.h', 'system.h', 'world.h', 'world.cpp'],
  'TD Scripting': ['token.h', 'lexer.h', 'lexer.cpp', 'parser.h', 'parser.cpp', 'ast.h', 'compiler.h', 'compiler.cpp', 'vm.h', 'vm.cpp'],
  'Editor': ['main.cpp', 'scene_panel.h', 'scene_panel.cpp', 'inspector_panel.h', 'inspector_panel.cpp', 'asset_browser.h', 'asset_browser.cpp', 'console_panel.h', 'console_panel.cpp', 'menu_bar.h', 'menu_bar.cpp'],
  'WASM': ['emscripten_main.cpp', 'js_bridge.js'],
  'Web': ['index.html', 'style.css', 'engine-wrapper.ts'],
  'Shaders': ['sprite.vert', 'sprite.frag', 'basic_3d.vert', 'basic_3d.frag'],
  'Examples': ['pong/main.cpp', 'platformer/main.cpp'],
  'Tests': ['test_math.cpp', 'test_ecs.cpp', 'test_physics.cpp'],
  'Build': ['Makefile', 'CMakeLists.txt', '.gitignore', 'README.md', 'td-engine-setup.iss']
};

const features = [
  { icon: '🎨', title: '2D/3D Rendering', desc: 'Sprite batching, meshes, lighting, materials' },
  { icon: '⚡', title: 'Physics Engine', desc: 'AABB collision, rigid bodies, spatial hashing' },
  { icon: '🔊', title: 'Audio System', desc: 'WAV loading, software mixing, waveOut API' },
  { icon: '🌐', title: 'Networking', desc: 'TCP/UDP sockets, client-server, interpolation' },
  { icon: '🧩', title: 'ECS Architecture', desc: 'Entity-Component-System for game objects' },
  { icon: '📜', title: 'TD Scripting', desc: 'Custom language with lexer, parser, and VM' },
];

function App() {
  const [activeTab, setActiveTab] = useState('overview');
  const [expandedCategory, setExpandedCategory] = useState<string | null>('Platform');

  return (
    <div className="min-h-screen bg-gradient-to-br from-slate-900 via-slate-800 to-slate-900 text-white">
      {/* Header */}
      <header className="bg-slate-900/80 backdrop-blur-sm border-b border-slate-700 sticky top-0 z-50">
        <div className="max-w-7xl mx-auto px-6 py-4">
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
              <div className="w-10 h-10 bg-gradient-to-br from-cyan-400 to-blue-500 rounded-lg flex items-center justify-center font-bold text-xl">
                TD
              </div>
              <div>
                <h1 className="text-xl font-bold">TD Engine</h1>
                <p className="text-sm text-slate-400">C++ Game Engine</p>
              </div>
            </div>
            <div className="flex gap-4">
              <a href="https://github.com" className="text-slate-400 hover:text-white transition-colors">
                GitHub
              </a>
              <a href="#docs" className="text-slate-400 hover:text-white transition-colors">
                Docs
              </a>
            </div>
          </div>
        </div>
      </header>

      {/* Hero Section */}
      <section className="py-20 px-6">
        <div className="max-w-7xl mx-auto text-center">
          <div className="inline-block px-4 py-1 bg-cyan-500/20 text-cyan-400 rounded-full text-sm font-medium mb-6">
            Zero External Dependencies
          </div>
          <h2 className="text-5xl font-bold mb-6 bg-gradient-to-r from-cyan-400 to-blue-500 bg-clip-text text-transparent">
            Complete 2D/3D Game Engine
          </h2>
          <p className="text-xl text-slate-400 max-w-2xl mx-auto mb-10">
            A complete game engine written from scratch in C/C++ with rendering, physics, 
            audio, networking, ECS architecture, and a custom scripting language.
          </p>
          <div className="flex justify-center gap-4">
            <button className="px-6 py-3 bg-gradient-to-r from-cyan-500 to-blue-500 rounded-lg font-semibold hover:opacity-90 transition-opacity">
              Get Started
            </button>
            <button className="px-6 py-3 bg-slate-700 rounded-lg font-semibold hover:bg-slate-600 transition-colors">
              View Source
            </button>
          </div>
        </div>
      </section>

      {/* Features Grid */}
      <section className="py-16 px-6 bg-slate-900/50">
        <div className="max-w-7xl mx-auto">
          <h3 className="text-2xl font-bold text-center mb-12">Engine Features</h3>
          <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-6">
            {features.map((feature, i) => (
              <div key={i} className="bg-slate-800/50 rounded-xl p-6 border border-slate-700 hover:border-cyan-500/50 transition-colors">
                <div className="text-4xl mb-4">{feature.icon}</div>
                <h4 className="text-lg font-semibold mb-2">{feature.title}</h4>
                <p className="text-slate-400 text-sm">{feature.desc}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Tabs Section */}
      <section className="py-16 px-6">
        <div className="max-w-7xl mx-auto">
          <div className="flex gap-4 mb-8 border-b border-slate-700">
            {['overview', 'files', 'code'].map(tab => (
              <button
                key={tab}
                onClick={() => setActiveTab(tab)}
                className={`px-4 py-2 font-medium capitalize transition-colors ${
                  activeTab === tab 
                    ? 'text-cyan-400 border-b-2 border-cyan-400' 
                    : 'text-slate-400 hover:text-white'
                }`}
              >
                {tab}
              </button>
            ))}
          </div>

          {activeTab === 'overview' && (
            <div className="grid lg:grid-cols-2 gap-8">
              <div>
                <h4 className="text-xl font-bold mb-4">Architecture</h4>
                <div className="bg-slate-800 rounded-xl p-6 font-mono text-sm">
                  <pre className="text-slate-300">{`
┌─────────────────────────────────┐
│          Game Code              │
├─────────────────────────────────┤
│         Engine API              │
├─────┬─────┬─────┬─────┬────────┤
│Rendr│Phys │Audio│ Net │ Assets │
├─────┴─────┴─────┴─────┴────────┤
│   Core (Math, Memory, Logger)   │
├─────────────────────────────────┤
│     Platform (Win32, OpenGL)    │
└─────────────────────────────────┘
                  `}</pre>
                </div>
              </div>
              <div>
                <h4 className="text-xl font-bold mb-4">Quick Start</h4>
                <div className="bg-slate-800 rounded-xl p-6 font-mono text-sm overflow-x-auto">
                  <pre className="text-slate-300">{`#include "td/platform/win32_window.h"
#include "td/renderer/gl_renderer.h"

int main() {
    td::Win32Window window;
    window.create({
        .title = "My Game",
        .width = 800,
        .height = 600
    });
    
    td::Renderer::get().init();
    
    while (!window.shouldClose()) {
        window.pollEvents();
        td::Renderer::get().clear(0.1f, 0.1f, 0.1f);
        window.swapBuffers();
    }
    
    return 0;
}`}</pre>
                </div>
              </div>
            </div>
          )}

          {activeTab === 'files' && (
            <div className="grid md:grid-cols-2 lg:grid-cols-3 gap-4">
              {Object.entries(engineStructure).map(([category, files]) => (
                <div key={category} className="bg-slate-800 rounded-xl overflow-hidden">
                  <button
                    onClick={() => setExpandedCategory(expandedCategory === category ? null : category)}
                    className="w-full px-4 py-3 flex items-center justify-between bg-slate-700/50 hover:bg-slate-700 transition-colors"
                  >
                    <span className="font-semibold">{category}</span>
                    <span className="text-slate-400 text-sm">{files.length} files</span>
                  </button>
                  {expandedCategory === category && (
                    <ul className="p-4 space-y-1">
                      {files.map((file, i) => (
                        <li key={i} className="text-slate-400 text-sm font-mono flex items-center gap-2">
                          <span className="text-cyan-400">📄</span>
                          {file}
                        </li>
                      ))}
                    </ul>
                  )}
                </div>
              ))}
            </div>
          )}

          {activeTab === 'code' && (
            <div className="bg-slate-800 rounded-xl p-6 font-mono text-sm overflow-x-auto">
              <div className="text-slate-400 mb-4">// Example: Pong Game using TD Engine</div>
              <pre className="text-slate-300">{`#include "td/ecs/world.h"
#include "td/renderer/sprite_batch.h"

td::World world;
td::SpriteBatch batch;

void gameInit() {
    batch.init();
    
    // Create paddle entity
    auto paddle = world.createEntity("Paddle");
    auto* pos = world.addComponent<td::PositionComponent>(paddle);
    pos->x = 50; pos->y = 300;
    
    auto* sprite = world.addComponent<td::SpriteComponent>(paddle);
    sprite->width = 15; sprite->height = 80;
    sprite->r = 1; sprite->g = 1; sprite->b = 1;
    
    // Create ball entity
    auto ball = world.createEntity("Ball");
    auto* ballPos = world.addComponent<td::PositionComponent>(ball);
    auto* ballVel = world.addComponent<td::VelocityComponent>(ball);
    ballVel->vx = 200; ballVel->vy = 150;
}

void gameUpdate(float dt) {
    // Query entities with Position and Velocity
    td::EntityId entities[100];
    auto mask = td::componentBit(td::ComponentType::Position) |
                td::componentBit(td::ComponentType::Velocity);
    int count = world.query(mask, entities, 100);
    
    for (int i = 0; i < count; i++) {
        auto* pos = world.getComponent<td::PositionComponent>(entities[i]);
        auto* vel = world.getComponent<td::VelocityComponent>(entities[i]);
        pos->x += vel->vx * dt;
        pos->y += vel->vy * dt;
    }
}`}</pre>
            </div>
          )}
        </div>
      </section>

      {/* Stats Section */}
      <section className="py-16 px-6 bg-slate-900/50">
        <div className="max-w-7xl mx-auto">
          <div className="grid grid-cols-2 md:grid-cols-4 gap-8">
            {[
              { value: '89', label: 'Source Files' },
              { value: '12', label: 'Subsystems' },
              { value: '0', label: 'Dependencies' },
              { value: 'C++17', label: 'Standard' }
            ].map((stat, i) => (
              <div key={i} className="text-center">
                <div className="text-4xl font-bold text-cyan-400 mb-2">{stat.value}</div>
                <div className="text-slate-400">{stat.label}</div>
              </div>
            ))}
          </div>
        </div>
      </section>

      {/* Footer */}
      <footer className="py-8 px-6 border-t border-slate-800">
        <div className="max-w-7xl mx-auto text-center text-slate-400">
          <p>TD Engine — A complete game engine written from scratch</p>
          <p className="text-sm mt-2">No STL for hot paths • OpenGL 3.3 Core • Win32 Platform Layer</p>
        </div>
      </footer>
    </div>
  );
}

export default App;
