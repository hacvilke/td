#include "game_loop.h"
#include "../platform/win32_window.h"  // Win32Window definition for run()

namespace td {

void GameLoop::setCallbacks(InitCallback init, UpdateCallback update, RenderCallback render) {
    m_init = init;
    m_update = update;
    m_render = render;
}

void GameLoop::setShutdownCallback(ShutdownCallback shutdown) {
    m_shutdown = shutdown;
}

void GameLoop::run(Win32Window& window) {
    m_running = true;
    m_accumulator = 0;
    
    // Initialize
    if (m_init) {
        m_init();
    }
    
    // Main loop with fixed timestep
    while (!window.shouldClose() && m_running) {
        // Poll input and update timing
        window.pollEvents();
        
        float frameTime = window.time.deltaTime;
        
        // Clamp frame time to avoid spiral of death
        if (frameTime > 0.25f) {
            frameTime = 0.25f;
        }
        
        m_accumulator += frameTime;
        
        // Fixed timestep updates
        while (m_accumulator >= m_fixedStep) {
            if (m_update) {
                m_update(m_fixedStep);
            }
            m_accumulator -= m_fixedStep;
        }
        
        // Interpolation factor for smooth rendering
        float alpha = (float)(m_accumulator / m_fixedStep);
        
        // Render with interpolation factor
        if (m_render) {
            m_render(alpha);
        }
        
        // Swap buffers
        window.swapBuffers();
    }
    
    // Shutdown
    if (m_shutdown) {
        m_shutdown();
    }
    
    m_running = false;
}

void GameLoop::stop() {
    m_running = false;
}

} // namespace td
