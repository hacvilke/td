// =============================================================================
// TD Engine — Public API TypeScript definitions
// File: web/td_api.d.ts
//
// Type definitions for web/td_api.js. Import from a TS game:
//
//   import { TDEngine } from 'td-engine';
//   await TDEngine.init('game-canvas');
//   const id = TDEngine.ecs.create('Player');
//
// Or as a global (when loaded via <script src="td_api.js">):
//
//   declare global {
//     interface Window { TDEngine: typeof TDEngine; }
//   }
//
// API version (semver): 1.0.0
// Wire format (network): JSON-RPC over WebSocket text frames, see web/net_websocket.js
//
// Versioning contract:
//   - MAJOR bump: a function signature changes shape (arg count/types) or a
//     subsystem is removed. Existing games WILL break.
//   - MINOR bump: a new subsystem or a new function on an existing subsystem.
//   - PATCH bump: bug fix that does not change any signature.
//   - The engine reports its version via TDEngine.version (string) and
//     TDEngine.lifecycle.getVersion() (string, from the WASM module).
//   - The .d.ts is the source of truth for the JS-visible surface. If you
//     change web/td_api.js, you MUST change this file in the same commit.
// =============================================================================

// ---------------------------------------------------------------------------
// Shared primitives
// ---------------------------------------------------------------------------

/** 2D vector returned by position/velocity getters. */
export interface Vec2 {
  x: number;
  y: number;
}

/** RGBA color, each channel 0..1. */
export interface Color {
  r: number;
  g: number;
  b: number;
  a: number;
}

/** Entity handle. Stable across the entity's lifetime; recycled after destroy(). */
export type EntityId = number;

/** Opaque handle returned by TDEngine.script.load(). Free with .unload(). */
export type ScriptHandle = number;

/** Opaque pointer into WASM heap. Use TDEngine._free() to release. */
export type WasmPointer = number;

// ---------------------------------------------------------------------------
// Win32 virtual-key codes (mirrors input.Key in td_api.js)
// ---------------------------------------------------------------------------

export interface KeyMap {
  Backspace: 0x08; Tab: 0x09; Enter: 0x0D; Escape: 0x1B; Space: 0x20;
  Left: 0x25; Up: 0x26; Right: 0x27; Down: 0x28;
  A: 0x41; B: 0x42; C: 0x43; D: 0x44; E: 0x45; F: 0x46; G: 0x47;
  H: 0x48; I: 0x49; J: 0x4A; K: 0x4B; L: 0x4C; M: 0x4D; N: 0x4E;
  O: 0x4F; P: 0x50; Q: 0x51; R: 0x52; S: 0x53; T: 0x54; U: 0x55;
  V: 0x56; W: 0x57; X: 0x58; Y: 0x59; Z: 0x5A;
  Num0: 0x30; Num1: 0x31; Num2: 0x32; Num3: 0x33; Num4: 0x34;
  Num5: 0x35; Num6: 0x36; Num7: 0x37; Num8: 0x38; Num9: 0x39;
  F1: 0x70; F2: 0x71; F3: 0x72; F4: 0x73; F5: 0x74; F6: 0x75;
  F7: 0x76; F8: 0x77; F9: 0x78; F10: 0x79; F11: 0x7A; F12: 0x7B;
  Shift: 0x10; Control: 0x11; Alt: 0x12;
  [key: string]: number;
}

export interface MouseMap {
  Left: 0;
  Right: 1;
  Middle: 2;
  [key: string]: number;
}

// ---------------------------------------------------------------------------
// Subsystem: lifecycle
// ---------------------------------------------------------------------------

export interface TDLifecycle {
  /**
   * Boot the engine: load the WASM module, attach to the canvas, set up
   * WebGL2 context + audio + input listeners. Idempotent — calling again
   * after init is a no-op.
   *
   * @param canvasId DOM id of the <canvas> element to render into.
   * @returns Resolves when the engine is ready to accept frame calls.
   */
  init(canvasId: string): Promise<void>;

  /** Register a callback fired once after init() completes (or immediately if already ready). */
  onReady(cb: () => void): void;

  /** Tear down the engine: free WASM memory, close GL context, remove listeners. */
  shutdown(): void;

  /** Engine version string from the WASM module (e.g. "1.0.0"). */
  getVersion(): string;

  /** True between init() and shutdown(). */
  isReady(): boolean;

  /** Resize the render target. Called automatically on window resize; call manually for explicit resize. */
  resize(width: number, height: number): void;
}

// ---------------------------------------------------------------------------
// Subsystem: ECS
// ---------------------------------------------------------------------------

export interface TDEcs {
  /** Create a new entity with the given debug name. Returns its stable id. */
  create(name?: string): EntityId;

  /** Destroy an entity. Its id becomes invalid; future calls with this id are no-ops. */
  destroy(id: EntityId): void;

  /** True if the id refers to a live entity. */
  isValid(id: EntityId): boolean;

  /** Current live entity count. */
  count(): number;

  /** Set the entity's world-space position (2D, pixels). */
  setPosition(id: EntityId, x: number, y: number): void;

  /** Read the entity's world-space position. */
  getPosition(id: EntityId): Vec2;

  /** Set the entity's linear velocity (pixels/sec). */
  setVelocity(id: EntityId, vx: number, vy: number): void;

  /**
   * Attach a colored quad sprite to the entity.
   * @param w,h  size in pixels
   * @param r,g,b,a  color channels, 0..1
   */
  setSprite(id: EntityId, w: number, h: number, r: number, g: number, b: number, a: number): void;

  /** Attach an axis-aligned box collider (used by physics). */
  setCollider(id: EntityId, w: number, h: number): void;
}

// ---------------------------------------------------------------------------
// Subsystem: input
// ---------------------------------------------------------------------------

export interface TDInput {
  /** True while the given Win32 VK code is held down. */
  isKeyDown(vk: number): boolean;

  /** True while the given mouse button (0=left, 1=right, 2=middle) is held. */
  isMouseDown(button: number): boolean;

  /** Current mouse position in canvas-space pixels. */
  getMousePos(): Vec2;

  /** Win32 virtual-key codes for convenience (TDEngine.input.Key.A, etc.). */
  Key: KeyMap;

  /** Mouse button indices. */
  Mouse: MouseMap;
}

// ---------------------------------------------------------------------------
// Subsystem: beat / rhythm
// ---------------------------------------------------------------------------

export interface TDBeat {
  /** Start a beat track on the given entity at the given BPM. */
  start(entityId: EntityId, bpm: number, windowHalfSec?: number): void;

  /** Stop the beat track on the given entity. */
  stop(entityId: EntityId): void;

  /** True if the current time is within ±windowHalfSec of a beat. */
  isOnBeat(entityId: EntityId): boolean;

  /** Number of beats elapsed since start(). */
  getCount(entityId: EntityId): number;

  /** Absolute time (sec) of the next beat. */
  getNextBeatTime(entityId: EntityId): number;

  /** Absolute time (sec) of the most recent beat. */
  getLastBeatTime(entityId: EntityId): number;

  /**
   * Register a player hit. Returns 0=miss, 1=good, 2=perfect.
   * @param strict  if true, only "perfect" counts.
   */
  registerHit(entityId: EntityId, strict?: boolean): 0 | 1 | 2;

  /** Current combo count (consecutive successful hits). */
  getCombo(entityId: EntityId): number;

  /** Best combo since the track started. */
  getBestCombo(entityId: EntityId): number;

  /** Reset combo to 0. */
  resetCombo(entityId: EntityId): void;

  /** Change BPM mid-track. */
  setBpm(entityId: EntityId, bpm: number): void;

  /**
   * Register a JS callback invoked on each beat. Returns a function pointer
   * you can pass to clearCallback (currently no clear API; the pointer leaks
   * but it's a no-op after the entity is destroyed).
   */
  setCallback(cb: (beatCount: number) => void): number;

  /** Play one of the preloaded WAV sounds (indexed by load order). */
  playSound(entityId: EntityId, wavIndex: number): void;
}

// ---------------------------------------------------------------------------
// Subsystem: scripting (tdscript VM)
// ---------------------------------------------------------------------------

export interface TDScript {
  /** Compile a script source. Returns a handle for call/unload. */
  load(src: string, name?: string): ScriptHandle;

  /**
   * Call a function in the loaded script.
   * @param handle   from load()
   * @param fnName   function name inside the script
   * @param argsJson JSON array of args, e.g. '["hello", 42]'
   * @returns JSON-encoded return value, or "null".
   */
  call(handle: ScriptHandle, fnName: string, argsJson?: string): string;

  /** Free the script handle. */
  unload(handle: ScriptHandle): void;
}

// ---------------------------------------------------------------------------
// Subsystem: i18n / localization
// ---------------------------------------------------------------------------

export interface TDI18n {
  /** Load a locale's strings from a JSON object: { "key": "translation", ... }. */
  load(locale: string, json: string): void;

  /** Switch the active locale. */
  setLocale(locale: string): void;

  /** Translate a key under the active locale. Returns the key itself if missing. */
  t(key: string): string;

  /** True if the active locale is right-to-left (Arabic, Hebrew, etc.). */
  isRtl(): boolean;
}

// ---------------------------------------------------------------------------
// Subsystem: audio
// ---------------------------------------------------------------------------

export interface TDAudio {
  /** Resume the Web Audio context (required after a user gesture on most browsers). */
  resume(): void;

  /**
   * Fill a stereo int16 PCM buffer. Advanced use — the engine's bridge calls
   * this internally; you usually don't need to.
   * @param outPtr    WASM pointer to a int16 buffer of size numFrames*2.
   * @param numFrames number of stereo frames to fill.
   */
  fillBuffer(outPtr: WasmPointer, numFrames: number): void;
}

// ---------------------------------------------------------------------------
// Subsystem: touch
// ---------------------------------------------------------------------------

export interface TDTouch {
  /** Mark the start of a frame (clears per-frame state). Call once per frame. */
  beginFrame(): void;

  /** Report a touch starting. */
  start(id: number, x: number, y: number, pressure?: number): void;

  /** Report a moved touch. */
  move(id: number, x: number, y: number, pressure?: number): void;

  /** Report a touch ending. */
  end(id: number, x: number, y: number): void;

  /** Number of active touches. */
  count(): number;

  /** X of the idx-th touch (0..count-1). */
  x(idx?: number): number;

  /** Y of the idx-th touch (0..count-1). */
  y(idx?: number): number;

  /** Pinch scale factor (1.0 = no pinch). */
  pinchScale(): number;
}

// ---------------------------------------------------------------------------
// Subsystem: gamepad
// ---------------------------------------------------------------------------

export interface TDGamepad {
  /** Mark the start of a frame. Call once per frame. */
  beginFrame(): void;

  /** Report a gamepad connecting/disconnecting. */
  setConnected(idx: number, connected: boolean, id?: string, mapping?: string): void;

  /** Report a digital button state. */
  setButton(idx: number, btn: number, pressed: boolean): void;

  /** Report an analog button (trigger) value 0..1. */
  setAnalog(idx: number, btn: number, value: number): void;

  /** Report a stick axis value -1..1. */
  setAxis(idx: number, axis: number, value: number): void;

  /** True if the given button is currently pressed. */
  buttonPressed(idx: number, btn: number): boolean;

  /** Current value of the given stick axis (-1..1). */
  axis(idx: number, axis: number): number;
}

// ---------------------------------------------------------------------------
// Subsystem: shader graph
// ---------------------------------------------------------------------------

export interface TDShaderGraph {
  /** Compile the current shader graph with `nodeCount` nodes. Returns GLSL source. */
  compile(nodeCount: number): string;
}

// ---------------------------------------------------------------------------
// Low-level escape hatches
// ---------------------------------------------------------------------------

export interface TDEngineLowLevel {
  /** Allocate `n` bytes on the WASM heap. Returns a pointer; free with _free(). */
  _malloc(n: number): WasmPointer;

  /** Free a pointer previously allocated by _malloc() or _newStr(). */
  _free(ptr: WasmPointer): void;

  /**
   * Allocate, write a JS string as UTF-8, null-terminate, return the pointer.
   * Caller MUST _free() the pointer when done. Useful when cwrap's 'string'
   * arg type doesn't work for very long strings.
   */
  _newStr(s: string): WasmPointer;

  /**
   * Escape hatch: wrap any `td_*` C function by name. Returns a callable
   * that takes JS args and returns JS values per Emscripten's cwrap rules.
   *
   *   const setX = TDEngine._wrap('td_entity_set_x', null, ['number','number']);
   *   setX(entityId, 100);
   */
  _wrap(name: string, retType: string | null, argTypes: string[]): (...args: any[]) => any;
}

// ---------------------------------------------------------------------------
// Composed namespace
// ---------------------------------------------------------------------------

export interface TDEngineNamespace extends TDEngineLowLevel {
  // Subsystems
  lifecycle: TDLifecycle;
  ecs: TDEcs;
  input: TDInput;
  beat: TDBeat;
  script: TDScript;
  i18n: TDI18n;
  audio: TDAudio;
  touch: TDTouch;
  gamepad: TDGamepad;
  shaderGraph: TDShaderGraph;

  // Convenience accessors
  /** The low-level TDBridge object (engine internals). Prefer the subsystems above. */
  readonly bridge: any;

  /** The Emscripten Module object. Prefer the subsystems above. */
  readonly module: any;

  /** Re-export of TDDeprecated (deprecation warning helpers). */
  readonly deprecated: any | null;

  /** Re-export of TDServerRouter (server-side URL routing helpers). */
  readonly server: any | null;

  /** Semver string of this JS API layer (NOT the WASM module version). */
  readonly version: '1.0.0';
}

// ---------------------------------------------------------------------------
// Global declaration — when loaded via <script>, TDEngine lands on `window`.
// ---------------------------------------------------------------------------

declare global {
  interface Window {
    TDEngine: TDEngineNamespace;
    TDBridge: any;
    TDDeprecated: any;
    TDServerRouter: any;
    TDScriptRuntime: TDScriptRuntimeNamespace;
    TDClientBootstrap: { bootstrap: () => Promise<WebSocket | null> };
  }
  // eslint-disable-next-line no-var
  var TDEngine: TDEngineNamespace;
  // eslint-disable-next-line no-var
  var TDBridge: any;
  // eslint-disable-next-line no-var
  var TDScriptRuntime: TDScriptRuntimeNamespace;
  // eslint-disable-next-line no-var
  var TDClientBootstrap: { bootstrap: () => Promise<WebSocket | null> };
}

// =============================================================================
// TDScript Runtime — type definitions for the network-scripting runtime.
//
// Compiled TDScript code (.td → .js) runs against this runtime. Provides:
//   - Log, Network, Physics, Vector3, Math globals
//   - __td_rpc_register / __td_repl_register / __td_script_main hooks
//
// See: web/tdscript_runtime.js, src/scripting/tdscript/
// =============================================================================

interface TDScriptVector3 {
  x: number;
  y: number;
  z: number;
  add(o: TDScriptVector3): TDScriptVector3;
  sub(o: TDScriptVector3): TDScriptVector3;
  mul(s: number): TDScriptVector3;
  length(): number;
  normalized(): TDScriptVector3;
  toString(): string;
}

type TDScriptVector3Constructor = new (x?: number, y?: number, z?: number) => TDScriptVector3;

interface TDScriptLog {
  info(msg: string): void;
  warn(msg: string): void;
  error(msg: string): void;
}

interface TDScriptNetwork {
  /** Broadcast a notification string to all connected clients. */
  broadcastNotification(msg: string): void;
  /** Send a notification to one specific client by peerId. */
  sendToClient(peerId: number, msg: string): void;
  /** Push a replicated field update to all clients (server → clients). */
  broadcastState(field: string, value: any): void;
  /** Client → server RPC invocation. */
  callRpc(peerId: number, cls: string, method: string, args: any[], mode: 'reliable' | 'unreliable'): void;
  /** Dispatch an incoming RPC frame on the server side. */
  dispatchRpc(frame: any, peerId: number): boolean;
  /** Apply a replicated state update on the client side. */
  applyReplicated(frame: any): boolean;
  /** Last frame sent (for unit-test inspection when no transport is connected). */
  lastFrame?: any;
}

interface TDScriptPhysics {
  /** Returns true if the given position is inside a solid voxel. */
  checkVoxelCollision(pos: TDScriptVector3): boolean;
}

interface TDScriptRuntimeNamespace {
  Vector3: TDScriptVector3Constructor;
  Log: TDScriptLog;
  Physics: TDScriptPhysics;
  Network: TDScriptNetwork;
  Math: typeof Math;
  rpcTable: Map<string, { mode: 'reliable' | 'unreliable'; fn: (instance: any, args: any[]) => any }>;
  replTable: Map<string, string[]>;
  instances: Map<string, any>;
  __td_rpc_register(cls: string, method: string, mode: 'reliable' | 'unreliable', fn: (instance: any, args: any[]) => any): void;
  __td_repl_register(cls: string, fields: string[]): void;
  __td_script_main(className: string): any | null;
}

declare global {
  // eslint-disable-next-line no-var
  var Vector3: TDScriptVector3Constructor;
  // eslint-disable-next-line no-var
  var Log: TDScriptLog;
  // eslint-disable-next-line no-var
  var Physics: TDScriptPhysics;
  // eslint-disable-next-line no-var
  var Network: TDScriptNetwork;
  // eslint-disable-next-line no-var
  var __td_rpc_register: TDScriptRuntimeNamespace['__td_rpc_register'];
  // eslint-disable-next-line no-var
  var __td_repl_register: TDScriptRuntimeNamespace['__td_repl_register'];
  // eslint-disable-next-line no-var
  var __td_script_main: TDScriptRuntimeNamespace['__td_script_main'];
  // eslint-disable-next-line no-var
  var __td_net_send: (frame: any, opts: { broadcast?: boolean; peerId?: number }) => void;
}

export {};
