/**
 * TD Engine JavaScript Bridge
 * Connects the WASM engine module to the browser DOM
 */
(function() {
    'use strict';

    const TDEngine = {
        _module: null,
        _canvas: null,
        _gl: null,
        _ready: false,
        _onReady: null,
        _onLog: null,
        _animFrameId: null,
        _updateCallback: null,

        /**
         * Initialize the engine with a canvas element
         * @param {string} canvasId - The ID of the canvas element
         * @returns {Promise<void>}
         */
        async init(canvasId) {
            this._canvas = document.getElementById(canvasId);
            if (!this._canvas) {
                throw new Error(`Canvas element '${canvasId}' not found`);
            }

            // Create WebGL2 context
            this._gl = this._canvas.getContext('webgl2', {
                alpha: false,
                antialias: true,
                depth: true,
                stencil: false,
                preserveDrawingBuffer: false
            });

            if (!this._gl) {
                this._gl = this._canvas.getContext('webgl', {
                    alpha: false,
                    antialias: true,
                    depth: true
                });
            }

            if (!this._gl) {
                throw new Error('WebGL is not supported on this device');
            }

            // Setup keyboard and mouse event listeners
            this._setupInputListeners();

            // Load WASM module
            try {
                await this._loadWASM();
            } catch (e) {
                console.error('Failed to load WASM module:', e);
                throw e;
            }

            // Initialize engine
            const width = this._canvas.width;
            const height = this._canvas.height;

            if (this._module && this._module._td_init) {
                this._module._td_init(width, height);
            }

            this._ready = true;

            if (this._onReady) {
                this._onReady();
            }

            console.log('TD Engine initialized');
        },

        async _loadWASM() {
            // Report loading progress
            const progressEl = document.getElementById('progress-fill');

            const moduleConfig = {
                canvas: this._canvas,
                print: (text) => {
                    console.log('[TD]', text);
                    if (this._onLog) this._onLog(text);
                },
                printErr: (text) => {
                    console.error('[TD]', text);
                },
                onRuntimeInitialized: () => {
                    console.log('WASM runtime initialized');
                },
                locateFile: (path) => {
                    return path;
                }
            };

            // Simulate loading for the UI
            if (progressEl) {
                let progress = 0;
                const interval = setInterval(() => {
                    progress = Math.min(progress + 10, 90);
                    progressEl.style.width = progress + '%';
                }, 100);

                // In a real implementation, we'd fetch the .wasm file and track progress
                // For now, simulate the load completing
                setTimeout(() => {
                    clearInterval(interval);
                    progressEl.style.width = '100%';
                }, 1200);
            }

            this._module = moduleConfig;
        },

        _setupInputListeners() {
            // Keyboard
            document.addEventListener('keydown', (e) => {
                if (this._module && this._module._td_set_key) {
                    this._module._td_set_key(e.keyCode, 1);
                }
                // Prevent default for game keys
                if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' '].includes(e.key)) {
                    e.preventDefault();
                }
            });

            document.addEventListener('keyup', (e) => {
                if (this._module && this._module._td_set_key) {
                    this._module._td_set_key(e.keyCode, 0);
                }
            });

            // Mouse
            this._canvas.addEventListener('mousemove', (e) => {
                const rect = this._canvas.getBoundingClientRect();
                const x = e.clientX - rect.left;
                const y = e.clientY - rect.top;
                if (this._module && this._module._td_set_mouse) {
                    this._module._td_set_mouse(x, y, -1, 0);
                }
            });

            this._canvas.addEventListener('mousedown', (e) => {
                const rect = this._canvas.getBoundingClientRect();
                const x = e.clientX - rect.left;
                const y = e.clientY - rect.top;
                if (this._module && this._module._td_set_mouse) {
                    this._module._td_set_mouse(x, y, e.button, 1);
                }
            });

            this._canvas.addEventListener('mouseup', (e) => {
                const rect = this._canvas.getBoundingClientRect();
                const x = e.clientX - rect.left;
                const y = e.clientY - rect.top;
                if (this._module && this._module._td_set_mouse) {
                    this._module._td_set_mouse(x, y, e.button, 0);
                }
            });

            // Resize
            window.addEventListener('resize', () => {
                if (this._module && this._module._td_resize) {
                    this._module._td_resize(window.innerWidth, window.innerHeight);
                }
            });

            // Prevent context menu
            this._canvas.addEventListener('contextmenu', (e) => e.preventDefault());
        },

        /**
         * Load a scene from text data
         * @param {string} sceneText - Scene description text
         */
        loadScene(sceneText) {
            if (!this._module || !this._module._td_load_scene) return;

            // Allocate string in WASM memory
            const encoder = new TextEncoder();
            const bytes = encoder.encode(sceneText + '\0');

            if (this._module._malloc && this._module.HEAPU8) {
                const ptr = this._module._malloc(bytes.length);
                this._module.HEAPU8.set(bytes, ptr);
                this._module._td_load_scene(ptr);
                this._module._free(ptr);
            }
        },

        /**
         * Start the game loop
         */
        start() {
            const loop = () => {
                if (this._module && this._module._td_update) {
                    this._module._td_update();
                }
                if (this._updateCallback) {
                    this._updateCallback(1.0 / 60.0);
                }
                this._animFrameId = requestAnimationFrame(loop);
            };
            this._animFrameId = requestAnimationFrame(loop);
        },

        /**
         * Stop the game loop
         */
        stop() {
            if (this._animFrameId) {
                cancelAnimationFrame(this._animFrameId);
                this._animFrameId = null;
            }
        },

        /**
         * Set update callback
         * @param {function} callback - Called every frame with delta time
         */
        onUpdate(callback) {
            this._updateCallback = callback;
        },

        /**
         * Set ready callback
         * @param {function} callback - Called when engine is ready
         */
        onReady(callback) {
            this._onReady = callback;
            if (this._ready) callback();
        },

        /**
         * Set log callback
         * @param {function} callback - Called with log messages
         */
        onLog(callback) {
            this._onLog = callback;
        },

        /**
         * Shutdown the engine
         */
        shutdown() {
            this.stop();
            if (this._module && this._module._td_shutdown) {
                this._module._td_shutdown();
            }
            this._ready = false;
        },

        isReady() {
            return this._ready;
        }
    };

    // Export globally
    window.TDEngine = TDEngine;
})();
