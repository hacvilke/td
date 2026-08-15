// =============================================================================
//  TD Bouncing Ball — minimal demo game for the EXE bundler
// =============================================================================
//  This is the smallest possible game that exercises the engine end-to-end:
//  boot, create entities, set sprites/colors, run a game loop, render.
//
//  It's intentionally tiny (~80 lines) so you can see the full pattern:
//    1. Wait for TDBridge to be ready (WASM loaded)
//    2. Init the engine on a canvas
//    3. Create entities (a few bouncing balls)
//    4. In the game loop: update positions, bounce off walls, render
//
//  When bundled with tools/bundler/bundle.py, this becomes a standalone
//  Windows .exe installer that runs the game in a WebView2 window.
// =============================================================================

(function () {
    'use strict';

    const W = 1280, H = 720;
    const BALL_COUNT = 12;
    const BALL_SIZE = 32;

    // ---- Boot ----------------------------------------------------------------
    function boot() {
        if (typeof TDBridge === 'undefined') {
            // Not loaded yet — retry in 50ms
            return setTimeout(boot, 50);
        }
        TDBridge.onReady(init);
    }

    async function init() {
        await TDEngine.init('game-canvas');

        // ---- Create balls ----------------------------------------------------
        const balls = [];
        for (let i = 0; i < BALL_COUNT; i++) {
            const e = TDEngine.ecs.create('Ball' + i);
            TDEngine.ecs.setPosition(e, Math.random() * W, Math.random() * H);
            TDEngine.ecs.setSprite(e, {
                width: BALL_SIZE,
                height: BALL_SIZE,
                r: Math.random(),
                g: Math.random(),
                b: Math.random(),
                a: 1,
            });
            balls.push({
                id: e,
                vx: (Math.random() - 0.5) * 200,
                vy: (Math.random() - 0.5) * 200,
            });
        }

        // ---- HUD -------------------------------------------------------------
        const fpsEl = document.getElementById('fps');
        const entitiesEl = document.getElementById('entities');
        let frameCount = 0;
        let lastFpsUpdate = performance.now();

        // ---- Game loop -------------------------------------------------------
        let lastTime = performance.now();
        function loop() {
            const now = performance.now();
            const dt = Math.min((now - lastTime) / 1000, 0.05); // clamp to 50ms
            lastTime = now;

            // Update ball positions, bounce off walls
            for (const b of balls) {
                let x = 0, y = 0;
                TDEngine.ecs.getPosition(b.id, (px, py) => { x = px; y = py; });
                x += b.vx * dt;
                y += b.vy * dt;
                if (x < 0) { x = 0; b.vx = -b.vx; }
                if (x > W - BALL_SIZE) { x = W - BALL_SIZE; b.vx = -b.vx; }
                if (y < 0) { y = 0; b.vy = -b.vy; }
                if (y > H - BALL_SIZE) { y = H - BALL_SIZE; b.vy = -b.vy; }
                TDEngine.ecs.setPosition(b.id, x, y);
            }

            // Render
            TDEngine.render.frame();

            // HUD update (every 500ms)
            frameCount++;
            if (now - lastFpsUpdate > 500) {
                const fps = Math.round(frameCount * 1000 / (now - lastFpsUpdate));
                fpsEl.textContent = fps;
                entitiesEl.textContent = balls.length;
                frameCount = 0;
                lastFpsUpdate = now;
            }

            requestAnimationFrame(loop);
        }
        requestAnimationFrame(loop);
    }

    // ---- Crash handler ------------------------------------------------------
    window.addEventListener('error', (e) => {
        const crash = document.getElementById('crash');
        const stack = document.getElementById('crash-stack');
        if (crash && stack) {
            stack.textContent = e.message + '\n\n' + (e.error ? e.error.stack : '');
            crash.style.display = 'flex';
        }
    });

    // ---- Go -----------------------------------------------------------------
    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', boot);
    } else {
        boot();
    }
})();
