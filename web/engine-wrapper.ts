/**
 * TD Engine TypeScript Wrapper
 * Type-safe interface for the TD Engine WebAssembly module
 */

export interface Vec3 {
    x: number;
    y: number;
    z: number;
}

export interface EntityInfo {
    id: number;
    name: string;
    active: boolean;
}

export class EngineError extends Error {
    constructor(message: string) {
        super(message);
        this.name = 'EngineError';
    }
}

export type UpdateCallback = (dt: number) => void;
export type LogCallback = (message: string) => void;
export type ReadyCallback = () => void;

declare global {
    interface Window {
        TDEngine: {
            init(canvasId: string): Promise<void>;
            loadScene(sceneData: string): void;
            start(): void;
            stop(): void;
            shutdown(): void;
            onUpdate(callback: UpdateCallback): void;
            onReady(callback: ReadyCallback): void;
            onLog(callback: LogCallback): void;
            isReady(): boolean;
        };
    }
}

export class TDEngine {
    private canvasId: string;
    private ready: boolean = false;
    private running: boolean = false;
    private updateCallbacks: UpdateCallback[] = [];
    private logCallbacks: LogCallback[] = [];

    constructor(canvasId: string) {
        this.canvasId = canvasId;
    }

    async init(): Promise<void> {
        try {
            if (!window.TDEngine) {
                throw new EngineError('TDEngine JS bridge not loaded. Include js_bridge.js before this script.');
            }

            // Set up log forwarding
            window.TDEngine.onLog((msg: string) => {
                for (const cb of this.logCallbacks) {
                    cb(msg);
                }
            });

            await window.TDEngine.init(this.canvasId);
            this.ready = true;
        } catch (error) {
            throw new EngineError(`Failed to initialize engine: ${error}`);
        }
    }

    loadScene(sceneData: string): void {
        if (!this.ready) {
            throw new EngineError('Engine not initialized');
        }
        window.TDEngine.loadScene(sceneData);
    }

    onUpdate(callback: UpdateCallback): void {
        this.updateCallbacks.push(callback);

        // Set up the unified callback
        window.TDEngine.onUpdate((dt: number) => {
            for (const cb of this.updateCallbacks) {
                cb(dt);
            }
        });
    }

    onLog(callback: LogCallback): void {
        this.logCallbacks.push(callback);
    }

    onKeyDown(callback: (key: string, keyCode: number) => void): void {
        document.addEventListener('keydown', (e: KeyboardEvent) => {
            callback(e.key, e.keyCode);
        });
    }

    onKeyUp(callback: (key: string, keyCode: number) => void): void {
        document.addEventListener('keyup', (e: KeyboardEvent) => {
            callback(e.key, e.keyCode);
        });
    }

    start(): void {
        if (!this.ready) {
            throw new EngineError('Engine not initialized');
        }
        window.TDEngine.start();
        this.running = true;
    }

    stop(): void {
        window.TDEngine.stop();
        this.running = false;
    }

    shutdown(): void {
        this.stop();
        window.TDEngine.shutdown();
        this.ready = false;
    }

    isReady(): boolean {
        return this.ready;
    }

    isRunning(): boolean {
        return this.running;
    }
}

export default TDEngine;
