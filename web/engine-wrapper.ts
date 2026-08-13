// =============================================================================
// TD Engine - Gauntlet Part 7: TypeScript Public API
// File: web/engine-wrapper.ts
//
// The clean, typed API that web game developers use to make games with TD
// Engine in the browser. Wraps the lower-level TDBridge (wasm/js_bridge.js)
// with typed entity handles, an input mirror, and a per-frame game-logic
// callback.
//
// Usage:
//
//   import { TDEngine, Key } from './engine-wrapper';
//
//   const engine = new TDEngine();
//   await engine.init('game-canvas');
//
//   const player = engine.createEntity('Player');
//   player.setPosition(100, 100);
//   player.setSprite(32, 32, 1, 1, 1, 1);
//   player.setCollider(32, 32);
//
//   engine.onUpdate((dt, input) => {
//     if (input.isKeyDown(Key.D)) player.setPosition(player.x + dt * 200, player.y);
//     if (input.isKeyPressedThisFrame(Key.Space)) engine.spawn('Bullet');
//   });
//
// Keys use Win32 VK codes (matches the engine's td::Key:: namespace).
// =============================================================================

// -----------------------------------------------------------------------------
// Key codes - mirror td::Key:: in src/platform/platform.h (Win32 VK codes).
// Browser KeyboardEvent.keyCode produces the same numeric values, so no
// translation is needed.
// -----------------------------------------------------------------------------
export const Key = {
  Backspace: 0x08,
  Tab:       0x09,
  Enter:     0x0D,
  Shift:     0x10,
  Control:   0x11,
  Alt:       0x12,
  Escape:    0x1B,
  Space:     0x20,
  Left:      0x25,
  Up:        0x26,
  Right:     0x27,
  Down:      0x28,
  Delete:    0x2E,
  Num0: 0x30, Num1: 0x31, Num2: 0x32, Num3: 0x33, Num4: 0x34,
  Num5: 0x35, Num6: 0x36, Num7: 0x37, Num8: 0x38, Num9: 0x39,
  A: 0x41, B: 0x42, C: 0x43, D: 0x44, E: 0x45, F: 0x46, G: 0x47,
  H: 0x48, I: 0x49, J: 0x4A, K: 0x4B, L: 0x4C, M: 0x4D, N: 0x4E,
  O: 0x4F, P: 0x50, Q: 0x51, R: 0x52, S: 0x53, T: 0x54, U: 0x55,
  V: 0x56, W: 0x57, X: 0x58, Y: 0x59, Z: 0x5A,
  F1: 0x70, F2: 0x71, F3: 0x72, F4: 0x73, F5: 0x74, F6: 0x75,
  F7: 0x76, F8: 0x77, F9: 0x78, F10: 0x79, F11: 0x7A, F12: 0x7B,
} as const;

export type KeyCode = typeof Key[keyof typeof Key];

export const Mouse = { Left: 0, Right: 1, Middle: 2 } as const;
export type MouseButton = typeof Mouse[keyof typeof Mouse];

// -----------------------------------------------------------------------------
// InputState - per-frame snapshot passed to the user's update callback.
// -----------------------------------------------------------------------------
export interface InputState {
  /** True while the key (Win32 VK code) is held down. */
  isKeyDown(vk: number): boolean;
  /** True only on the first frame the key went down. */
  isKeyPressedThisFrame(vk: number): boolean;
  /** True only on the first frame the key went up. */
  isKeyUp(vk: number): boolean;
  /** True while the mouse button is held. */
  isMouseDown(button: number): boolean;
  /** Mouse position (canvas-relative, in pixels, with DPR applied). */
  mouse: {
    x: number;  y: number;
    dx: number; dy: number;  // delta since last frame
    left: boolean; right: boolean; middle: boolean;
  };
}

// -----------------------------------------------------------------------------
// EntityHandle - typed wrapper around an engine EntityId.
// -----------------------------------------------------------------------------
export class EntityHandle {
  /** The engine's internal entity id (uint32). Stable for the entity's lifetime. */
  public readonly id: number;

  /** Cached position (updated by setPosition/getPosition). */
  public x = 0;
  public y = 0;

  private _engine: TDEngine;

  constructor(engine: TDEngine, id: number) {
    this._engine = engine;
    this.id = id;
  }

  /** Set the entity's 2D world position. */
  setPosition(x: number, y: number): this {
    this.x = x; this.y = y;
    this._engine._call('td_entity_set_position', [this.id, x, y]);
    return this;
  }

  /** Sync this handle's x/y from the engine's PositionComponent. */
  refreshPosition(): this {
    const Module = this._engine._module!;
    const td_get = Module.cwrap('td_entity_get_position', null, ['number', 'number', 'number']);
    const xPtr = Module._malloc(8);  // 2 * float32
    const yPtr = Module._malloc(8);
    try {
      td_get(this.id, xPtr, yPtr);
      this.x = Module.HEAPF32[xPtr >> 2];
      this.y = Module.HEAPF32[yPtr >> 2];
    } finally {
      Module._free(xPtr);
      Module._free(yPtr);
    }
    return this;
  }

  /** Set the entity's 2D velocity (pixels/second). */
  setVelocity(vx: number, vy: number): this {
    this._engine._call('td_entity_set_velocity', [this.id, vx, vy]);
    return this;
  }

  /** Attach or replace a Sprite component. */
  setSprite(w: number, h: number,
            r = 1, g = 1, b = 1, a = 1): this {
    this._engine._call('td_entity_set_sprite', [this.id, w, h, r, g, b, a]);
    return this;
  }

  /** Attach or replace an AABB Collider component. */
  setCollider(w: number, h: number): this {
    this._engine._call('td_entity_set_collider', [this.id, w, h]);
    return this;
  }

  /** Destroy this entity. The handle becomes invalid after this call. */
  destroy(): void {
    this._engine._call('td_entity_destroy', [this.id]);
  }

  /** Check whether the entity still exists in the world. */
  isValid(): boolean {
    return Boolean(this._engine._call('td_entity_is_valid', [this.id]));
  }
}

// -----------------------------------------------------------------------------
// Callback signatures
// -----------------------------------------------------------------------------
export type UpdateCallback = (dt: number, input: InputState) => void;
export type LogCallback = (entry: { level: 'info' | 'warn' | 'error'; message: string }) => void;

// -----------------------------------------------------------------------------
// TDBridge shape (loaded by wasm/js_bridge.js)
// -----------------------------------------------------------------------------
interface TDBridgeLike {
  ready: boolean;
  wasmExports: any | null;
  canvas: HTMLCanvasElement | null;
  onReady(cb: () => void): void;
  onLog(cb: (entry: { level: 'info' | 'warn' | 'error'; message: string }) => void): void;
  init(canvasId: string): Promise<void>;
  loadScene(sceneText: string): void;
  setKey(vkCode: number, pressed: boolean): void;
  setMouse(x: number, y: number, leftDown: boolean, rightDown: boolean): void;
  shutdown(): void;
  resumeAudio(): void;
  readWasmString(ptr: number): string;
}

// =============================================================================
// TDEngine - the main public class
// =============================================================================
export class TDEngine {
  /** Reference to the global TDBridge loaded by wasm/js_bridge.js. */
  public bridge: TDBridgeLike | null = null;

  /** The canvas element the engine is rendering to. */
  public canvas: HTMLCanvasElement | null = null;

  private _updateCallback: UpdateCallback | null = null;
  private _logCallbacks: LogCallback[] = [];

  // Input mirror - state tracked locally so user code can poll it cheaply.
  private _keys: Uint8Array = new Uint8Array(256);          // 0=up, 1=down
  private _keysPrev: Uint8Array = new Uint8Array(256);
  private _keysPressedThisFrame: Set<number> = new Set();
  private _keysReleasedThisFrame: Set<number> = new Set();
  private _mouseButtons: Uint8Array = new Uint8Array(8);
  private _mouseX = 0; private _mouseY = 0;
  private _mouseDx = 0; private _mouseDy = 0;

  private _running = false;
  private _lastTime = 0;
  private _frameBound: () => void;

  constructor() {
    this._frameBound = this._loop.bind(this);
  }

  // -------------------------------------------------------------------------
  // init(canvasId) -> Promise<void>
  // -------------------------------------------------------------------------
  async init(canvasId: string): Promise<void> {
    const bridge = (typeof window !== 'undefined' && (window as any).TDBridge) || null;
    if (!bridge) {
      throw new Error('TDBridge not found - include wasm/js_bridge.js before engine-wrapper.ts');
    }
    this.bridge = bridge as TDBridgeLike;

    bridge.onLog((entry) => this._dispatchLog(entry));

    await bridge.init(canvasId);
    this.canvas = bridge.canvas;

    this._setupInput();

    // Start the local update loop. The WASM main loop runs in parallel
    // (Emscripten rAF) and handles rendering + fixed-step physics; this loop
    // just gives user code a per-frame callback to drive game logic.
    this._running = true;
    this._lastTime = performance.now();
    requestAnimationFrame(this._frameBound);
  }

  // -------------------------------------------------------------------------
  // Register the user's per-frame update callback.
  // -------------------------------------------------------------------------
  onUpdate(callback: UpdateCallback): void {
    this._updateCallback = callback;
  }

  // -------------------------------------------------------------------------
  // Register a log subscriber.
  // -------------------------------------------------------------------------
  onLog(callback: LogCallback): void {
    if (typeof callback === 'function') this._logCallbacks.push(callback);
  }

  // -------------------------------------------------------------------------
  // Entity factory. Returns a typed handle, or null if creation failed.
  // -------------------------------------------------------------------------
  createEntity(name = 'Entity'): EntityHandle | null {
    const Module = this._module!;
    const td_create = Module.cwrap('td_create_entity', 'number', ['string']);
    const id = td_create(name);
    if (id === 0xFFFFFFFF || id === 0) return null;
    return new EntityHandle(this, id);
  }

  // -------------------------------------------------------------------------
  // Load a scene from a text string. Format:
  //   entity Player {
  //     position { x: 100 y: 100 }
  //     velocity { x: 0   y: 0 }
  //     sprite   { w: 32 h: 32 r: 1 g: 1 b: 1 a: 1 }
  //     collider { w: 32 h: 32 }
  //   }
  // -------------------------------------------------------------------------
  loadScene(sceneText: string): void {
    if (!this.bridge || !this.bridge.ready) {
      throw new Error('TDEngine not ready');
    }
    this.bridge.loadScene(sceneText);
  }

  // -------------------------------------------------------------------------
  // Pause/resume the local update loop. The WASM main loop continues to
  // render so the canvas does not freeze.
  // -------------------------------------------------------------------------
  pause():  void { this._running = false; }
  resume(): void {
    if (this._running) return;
    this._running = true;
    this._lastTime = performance.now();
    requestAnimationFrame(this._frameBound);
  }

  /** Tear down the engine and free all WASM-side resources. */
  shutdown(): void {
    this._running = false;
    if (this.bridge) this.bridge.shutdown();
  }

  /** Number of entities currently in the world. */
  getEntityCount(): number {
    const td_count = this._module!.cwrap('td_get_entity_count', 'number', []);
    return td_count();
  }

  // -------------------------------------------------------------------------
  // Internal: shortcuts to the Emscripten Module.
  // -------------------------------------------------------------------------
  private get _module(): any | null {
    return this.bridge?.wasmExports ?? null;
  }

  private _wasmCallCache: Map<string, Function> = new Map();
  /** @internal Used by EntityHandle - invokes a td_* C function via cwrap. */
  _call(name: string, args: any[]): any {
    const Module = this._module;
    if (!Module) throw new Error('WASM module not loaded');
    let fn = this._wasmCallCache.get(name);
    if (!fn) {
      // Hard-coded signatures for every exported td_* function.
      const sigs: Record<string, [string, string[]]> = {
        'td_create_entity':         ['number', ['string']],
        'td_entity_set_position':   [null, ['number', 'number', 'number']],
        'td_entity_get_position':   [null, ['number', 'number', 'number']],
        'td_entity_set_velocity':   [null, ['number', 'number', 'number']],
        'td_entity_set_sprite':     [null, ['number', 'number', 'number', 'number', 'number', 'number', 'number']],
        'td_entity_set_collider':   [null, ['number', 'number', 'number']],
        'td_entity_destroy':        [null, ['number']],
        'td_entity_is_valid':       ['boolean', ['number']],
        'td_get_entity_count':      ['number', []],
      };
      const sig = sigs[name];
      if (!sig) throw new Error(`Unknown WASM export: ${name}`);
      fn = Module.cwrap(name, sig[0], sig[1]);
      this._wasmCallCache.set(name, fn);
    }
    return fn(...args);
  }

  // -------------------------------------------------------------------------
  // _loop() - per-frame callback driven by requestAnimationFrame.
  // -------------------------------------------------------------------------
  private _loop(): void {
    if (!this._running) return;

    const now = performance.now();
    let dt = (now - this._lastTime) / 1000.0;
    this._lastTime = now;
    if (dt > 0.25) dt = 0.25;

    const inputState: InputState = {
      isKeyDown: (vk: number) => this._keys[vk] === 1,
      isKeyPressedThisFrame: (vk: number) => this._keysPressedThisFrame.has(vk),
      isKeyUp: (vk: number) => this._keysReleasedThisFrame.has(vk),
      isMouseDown: (btn: number) => this._mouseButtons[btn] === 1,
      mouse: {
        x: this._mouseX, y: this._mouseY,
        dx: this._mouseDx, dy: this._mouseDy,
        left:   this._mouseButtons[Mouse.Left]   === 1,
        right:  this._mouseButtons[Mouse.Right]  === 1,
        middle: this._mouseButtons[Mouse.Middle] === 1,
      },
    };

    if (this._updateCallback) {
      try {
        this._updateCallback(dt, inputState);
      } catch (err) {
        console.error('TD Engine update callback threw:', err);
        this._dispatchLog({ level: 'error', message: 'Update callback: ' + (err as Error).message });
      }
    }

    // Clear per-frame edge-triggered state.
    this._keysPressedThisFrame.clear();
    this._keysReleasedThisFrame.clear();
    this._mouseDx = 0; this._mouseDy = 0;

    // Snapshot previous-frame key state for next frame's isKey* helpers.
    this._keysPrev.set(this._keys);

    requestAnimationFrame(this._frameBound);
  }

  // -------------------------------------------------------------------------
  // _setupInput() - browser listeners that mirror state into our local arrays.
  // -------------------------------------------------------------------------
  private _setupInput(): void {
    if (!this.canvas) return;

    // Browser key events use e.keyCode (Win32 VK code) - same as the engine.
    document.addEventListener('keydown', (e) => {
      const vk = e.keyCode;
      if (vk < 0 || vk >= 256) return;
      if (this._keys[vk] === 0) {
        this._keysPressedThisFrame.add(vk);
      }
      this._keys[vk] = 1;
      if (this.bridge) this.bridge.setKey(vk, true);

      // Prevent default for game-relevant keys (arrow scroll, space page-down).
      if (vk === 0x20 || (vk >= 0x25 && vk <= 0x28) ||
          (vk >= 0x41 && vk <= 0x5A)) {
        e.preventDefault();
      }
    });

    document.addEventListener('keyup', (e) => {
      const vk = e.keyCode;
      if (vk < 0 || vk >= 256) return;
      this._keys[vk] = 0;
      this._keysReleasedThisFrame.add(vk);
      if (this.bridge) this.bridge.setKey(vk, false);
    });

    this.canvas.addEventListener('mousemove', (e) => {
      const rect = this.canvas!.getBoundingClientRect();
      const dpr = window.devicePixelRatio || 1;
      const nx = (e.clientX - rect.left) * dpr;
      const ny = (e.clientY - rect.top)  * dpr;
      this._mouseDx = nx - this._mouseX;
      this._mouseDy = ny - this._mouseY;
      this._mouseX = nx;
      this._mouseY = ny;
      if (this.bridge) {
        this.bridge.setMouse(
          this._mouseX, this._mouseY,
          this._mouseButtons[Mouse.Left]  === 1,
          this._mouseButtons[Mouse.Right] === 1,
        );
      }
    });

    this.canvas.addEventListener('mousedown', (e) => {
      if (e.button >= 0 && e.button < 8) this._mouseButtons[e.button] = 1;
      if (this.bridge) {
        this.bridge.setMouse(
          this._mouseX, this._mouseY,
          this._mouseButtons[Mouse.Left]  === 1,
          this._mouseButtons[Mouse.Right] === 1,
        );
      }
      e.preventDefault();
    });

    this.canvas.addEventListener('mouseup', (e) => {
      if (e.button >= 0 && e.button < 8) this._mouseButtons[e.button] = 0;
      if (this.bridge) {
        this.bridge.setMouse(
          this._mouseX, this._mouseY,
          this._mouseButtons[Mouse.Left]  === 1,
          this._mouseButtons[Mouse.Right] === 1,
        );
      }
    });

    this.canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  private _dispatchLog(entry: { level: 'info' | 'warn' | 'error'; message: string }): void {
    for (const cb of this._logCallbacks) {
      try { cb(entry); } catch (e) { /* swallow */ }
    }
  }
}

// -----------------------------------------------------------------------------
// Global export for non-module usage (plain <script> tag).
// When compiled to JS and loaded as a script, attaches TDEngine to window.
// -----------------------------------------------------------------------------
if (typeof window !== 'undefined') {
  (window as any).TDEngine = TDEngine;
  (window as any).EntityHandle = EntityHandle;
  (window as any).Key = Key;
  (window as any).Mouse = Mouse;
}

export default TDEngine;
