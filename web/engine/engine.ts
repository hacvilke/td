/**
 * TD Engine — Browser entry point.
 *
 * This replaces the broken `wasm/js_bridge.js` + `web/engine-wrapper.ts`
 * (which never actually loaded a WASM module) with a real working
 * TypeScript implementation of the engine's public API.
 *
 * Mirrors the C++ engine architecture:
 *   - core/math    (math.ts)
 *   - ecs          (ecs.ts)
 *   - renderer     (renderer.ts)
 *   - platform     (input.ts)
 *   - core         (engine.ts ← this file = the GameLoop equivalent)
 */

import { Renderer } from './renderer';
import { Camera2D } from './camera';
import { Input } from './input';
import { World } from './ecs';
import { Mat4 } from './math';

export type InitCallback = () => void;
export type UpdateCallback = (dt: number) => void;
export type RenderCallback = (alpha: number) => void;
export type LogCallback = (message: string) => void;
export type ReadyCallback = () => void;

export interface EngineConfig {
  canvas: HTMLCanvasElement;
  width: number;
  height: number;
  bgR?: number;
  bgG?: number;
  bgB?: number;
}

export class Engine {
  private canvas: HTMLCanvasElement;
  private gl: WebGL2RenderingContext;
  private renderer: Renderer;
  private input: Input;
  private world: World;
  private camera: Camera2D;
  private width: number;
  private height: number;
  private bgR: number;
  private bgG: number;
  private bgB: number;

  private ready = false;
  private running = false;
  private animFrameId: number | null = null;

  private initCb: InitCallback | null = null;
  private updateCb: UpdateCallback | null = null;
  private renderCb: RenderCallback | null = null;
  private logCb: LogCallback | null = null;
  private readyCb: ReadyCallback | null = null;

  private lastTime = 0;
  private accumulator = 0;
  private fixedStep = 1 / 60;
  private currentDt = 0;

  constructor(config: EngineConfig) {
    this.canvas = config.canvas;
    this.width = config.width;
    this.height = config.height;
    this.bgR = config.bgR ?? 0.1;
    this.bgG = config.bgG ?? 0.1;
    this.bgB = config.bgB ?? 0.15;

    const gl = this.canvas.getContext('webgl2', {
      alpha: false,
      antialias: true,
      depth: true,
      stencil: false,
      premultipliedAlpha: false,
      preserveDrawingBuffer: false,
    });

    if (!gl) {
      throw new Error('WebGL2 is not supported on this device');
    }

    this.gl = gl;
    this.renderer = new Renderer(gl);
    this.input = new Input();
    this.world = new World();
    this.camera = new Camera2D();

    this.canvas.width = this.width;
    this.canvas.height = this.height;
    this.camera.setViewport(this.width, this.height);
  }

  init(): void {
    if (this.ready) return;
    this.renderer.init();
    this.input.attach(this.canvas);
    this.ready = true;
    this.log('TD Engine initialized (TypeScript port)');
    if (this.readyCb) this.readyCb();
    if (this.initCb) this.initCb();
  }

  setCallbacks(
    init: InitCallback | null,
    update: UpdateCallback | null,
    render: RenderCallback | null,
  ): void {
    this.initCb = init;
    this.updateCb = update;
    this.renderCb = render;
  }

  setFixedStep(step: number): void {
    this.fixedStep = step;
  }

  onReady(cb: ReadyCallback): void {
    this.readyCb = cb;
    if (this.ready) cb();
  }

  onLog(cb: LogCallback): void {
    this.logCb = cb;
  }

  private log(msg: string): void {
    if (this.logCb) this.logCb(msg);
  }

  start(): void {
    if (!this.ready) {
      throw new Error('Engine not initialized. Call init() first.');
    }
    if (this.running) return;
    this.running = true;
    this.lastTime = performance.now();
    this.accumulator = 0;
    const loop = (now: number) => {
      if (!this.running) return;

      let frameTime = (now - this.lastTime) / 1000;
      this.lastTime = now;
      if (frameTime > 0.25) frameTime = 0.25;

      // Fixed-step updates (like td::GameLoop in C++)
      this.accumulator += frameTime;
      while (this.accumulator >= this.fixedStep) {
        if (this.updateCb) this.updateCb(this.fixedStep);
        this.world.updateSystems(this.fixedStep);
        this.accumulator -= this.fixedStep;
      }
      this.currentDt = this.fixedStep;

      const alpha = this.accumulator / this.fixedStep;

      // Render
      this.renderer.clear(this.bgR, this.bgG, this.bgB);
      this.renderer.setViewport(0, 0, this.canvas.width, this.canvas.height);
      if (this.renderCb) this.renderCb(alpha);

      this.input.endFrame();
      this.animFrameId = requestAnimationFrame(loop);
    };
    this.animFrameId = requestAnimationFrame(loop);
  }

  stop(): void {
    this.running = false;
    if (this.animFrameId !== null) {
      cancelAnimationFrame(this.animFrameId);
      this.animFrameId = null;
    }
  }

  shutdown(): void {
    this.stop();
    this.input.detach();
    this.renderer.shutdown();
    this.world.clear();
    this.ready = false;
    this.log('TD Engine shutdown');
  }

  resize(width: number, height: number): void {
    this.width = width;
    this.height = height;
    this.canvas.width = width;
    this.canvas.height = height;
    this.camera.setViewport(width, height);
  }

  getGL(): WebGL2RenderingContext {
    return this.gl;
  }

  getRenderer(): Renderer {
    return this.renderer;
  }

  getInput(): Input {
    return this.input;
  }

  getWorld(): World {
    return this.world;
  }

  getCamera(): Camera2D {
    return this.camera;
  }

  getProjectionMatrix(): Mat4 {
    return this.camera.getProjection();
  }

  getViewMatrix(): Mat4 {
    return this.camera.getView();
  }

  getDelta(): number {
    return this.currentDt;
  }

  isReady(): boolean {
    return this.ready;
  }

  isRunning(): boolean {
    return this.running;
  }

  getWidth(): number {
    return this.width;
  }

  getHeight(): number {
    return this.height;
  }
}
