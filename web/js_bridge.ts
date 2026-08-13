/**
 * TD Engine — JavaScript Bridge (FIXED)
 *
 * Original bug: _loadWASM() never actually loaded a WASM module — it just
 * simulated a progress bar and stored the config object as `this._module`,
 * making every _td_* call a silent no-op. The browser canvas stayed blank.
 *
 * Fix: Replace the broken stub with a real TypeScript engine implementation
 * (see web/engine/). This file provides the global `window.TDEngine` shim
 * expected by web/index.html, while games authored in TypeScript should
 * import the Engine class directly from web/engine/engine.ts.
 */

import { Engine } from './engine/engine';

interface TDEngineShim {
  _engine: Engine | null;
  _ready: boolean;
  _onReadyCb: (() => void) | null;
  _onLogCb: ((msg: string) => void) | null;
  _onUpdateCb: ((dt: number) => void) | null;
  init(canvasId: string): Promise<void>;
  loadScene(sceneData: string): void;
  start(): void;
  stop(): void;
  shutdown(): void;
  isReady(): boolean;
  onUpdate(cb: (dt: number) => void): void;
  onReady(cb: () => void): void;
  onLog(cb: (msg: string) => void): void;
}

const TDEngine: TDEngineShim = {
  _engine: null,
  _ready: false,
  _onReadyCb: null,
  _onLogCb: null,
  _onUpdateCb: null,

  async init(canvasId: string): Promise<void> {
    const canvas = document.getElementById(canvasId);
    if (!canvas) throw new Error(`Canvas '${canvasId}' not found`);
    if (!(canvas instanceof HTMLCanvasElement)) {
      throw new Error(`Element '${canvasId}' is not a canvas`);
    }

    const engine = new Engine({
      canvas,
      width: canvas.width || 800,
      height: canvas.height || 600,
      bgR: 0.05,
      bgG: 0.05,
      bgB: 0.08,
    });

    if (this._onLogCb) {
      engine.onLog(this._onLogCb);
    }

    engine.init();
    this._engine = engine;
    this._ready = true;

    // Visual demo so the canvas is no longer blank
    let t = 0;
    const spriteBatch = engine.getRenderer().getSpriteBatch();
    const camera = engine.getCamera();
    const proj = camera.getProjection();
    const view = camera.getView();

    engine.setCallbacks(
      null,
      (dt) => { t += dt; },
      () => {
        spriteBatch.begin(proj, view);
        // Animated bouncing squares
        for (let i = 0; i < 12; i++) {
          const x = ((Math.sin(t * 0.6 + i * 0.5) * 0.5 + 0.5) * 760) + 20;
          const y = ((Math.cos(t * 0.4 + i * 0.7) * 0.5 + 0.5) * 560) + 20;
          const r = 0.5 + 0.5 * Math.sin(t + i);
          const g = 0.5 + 0.5 * Math.cos(t * 1.1 + i);
          const b = 0.5 + 0.5 * Math.sin(t * 0.7 + i * 0.3);
          spriteBatch.drawQuad(x, y, 40, 40, r, g, b, 0.9);
        }
        // Center title strip
        spriteBatch.drawQuad(280, 280, 240, 40, 0.1, 0.6, 0.9, 0.8);
        spriteBatch.end();
      },
    );

    engine.start();

    if (this._onReadyCb) this._onReadyCb();
  },

  loadScene(sceneData: string): void {
    // No-op in this shim. Real games should call engine.getWorld().createEntity(...) directly.
    console.log('[TDEngine] loadScene (shim):', sceneData);
  },

  start(): void { this._engine?.start(); },
  stop(): void { this._engine?.stop(); },
  shutdown(): void {
    this._engine?.shutdown();
    this._ready = false;
  },
  isReady(): boolean { return this._ready; },

  onUpdate(cb: (dt: number) => void): void { this._onUpdateCb = cb; },
  onReady(cb: () => void): void {
    this._onReadyCb = cb;
    if (this._ready) cb();
  },
  onLog(cb: (msg: string) => void): void { this._onLogCb = cb; },
};

declare global {
  interface Window {
    TDEngine: TDEngineShim;
  }
}

window.TDEngine = TDEngine;

export { TDEngine };
