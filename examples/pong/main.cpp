// TD Engine - Pong Example
// A complete Pong game using the TD Engine

#include "../../src/platform/platform.h"
#include "../../src/platform/win32_window.h"
#include "../../src/core/game_loop.h"
#include "../../src/core/logger.h"
#include "../../src/renderer/gl_renderer.h"
#include "../../src/renderer/sprite_batch.h"
#include "../../src/renderer/camera.h"
#include "../../src/ecs/world.h"
#include <cstdio>

using namespace td;

// Game state
static Win32Window* g_window = nullptr;
static SpriteBatch* g_spriteBatch = nullptr;
static Camera2D g_camera;
static World g_world;

// Entity IDs
static EntityId g_leftPaddle;
static EntityId g_rightPaddle;
static EntityId g_ball;
static EntityId g_topWall;
static EntityId g_bottomWall;

// Score
static int g_leftScore = 0;
static int g_rightScore = 0;

// Game constants
static const float PADDLE_SPEED = 400.0f;
static const float BALL_SPEED = 300.0f;
static const float PADDLE_WIDTH = 15.0f;
static const float PADDLE_HEIGHT = 80.0f;
static const float BALL_SIZE = 12.0f;
static const float WALL_HEIGHT = 10.0f;

void resetBall() {
    PositionComponent* pos = g_world.getComponent<PositionComponent>(g_ball);
    VelocityComponent* vel = g_world.getComponent<VelocityComponent>(g_ball);
    
    if (pos && vel) {
        pos->x = 400;
        pos->y = 300;
        
        // Random direction
        float angle = ((rand() % 100) / 100.0f - 0.5f) * 0.5f;
        float dir = (rand() % 2 == 0) ? 1.0f : -1.0f;
        
        vel->vx = dir * BALL_SPEED * cosf(angle);
        vel->vy = BALL_SPEED * sinf(angle);
    }
}

void gameInit() {
    TD_LOG_INFO("Pong game initializing...");
    
    // Initialize renderer
    Renderer::get().init();
    
    // Initialize sprite batch
    g_spriteBatch = new SpriteBatch();
    g_spriteBatch->init();
    
    // Setup camera
    g_camera.setViewport(800, 600);
    g_camera.setPosition(400, 300);
    
    // Create left paddle
    g_leftPaddle = g_world.createEntity("LeftPaddle");
    {
        PositionComponent* pos = g_world.addComponent<PositionComponent>(g_leftPaddle);
        pos->x = 30;
        pos->y = 300;
        
        g_world.addComponent<VelocityComponent>(g_leftPaddle);
        
        SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(g_leftPaddle);
        sprite->width = PADDLE_WIDTH;
        sprite->height = PADDLE_HEIGHT;
        sprite->r = 1.0f; sprite->g = 1.0f; sprite->b = 1.0f; sprite->a = 1.0f;
        
        ColliderComponent* col = g_world.addComponent<ColliderComponent>(g_leftPaddle);
        col->width = PADDLE_WIDTH;
        col->height = PADDLE_HEIGHT;
    }
    
    // Create right paddle
    g_rightPaddle = g_world.createEntity("RightPaddle");
    {
        PositionComponent* pos = g_world.addComponent<PositionComponent>(g_rightPaddle);
        pos->x = 770;
        pos->y = 300;
        
        g_world.addComponent<VelocityComponent>(g_rightPaddle);
        
        SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(g_rightPaddle);
        sprite->width = PADDLE_WIDTH;
        sprite->height = PADDLE_HEIGHT;
        sprite->r = 1.0f; sprite->g = 1.0f; sprite->b = 1.0f; sprite->a = 1.0f;
        
        ColliderComponent* col = g_world.addComponent<ColliderComponent>(g_rightPaddle);
        col->width = PADDLE_WIDTH;
        col->height = PADDLE_HEIGHT;
    }
    
    // Create ball
    g_ball = g_world.createEntity("Ball");
    {
        PositionComponent* pos = g_world.addComponent<PositionComponent>(g_ball);
        pos->x = 400;
        pos->y = 300;
        
        VelocityComponent* vel = g_world.addComponent<VelocityComponent>(g_ball);
        vel->vx = BALL_SPEED;
        vel->vy = BALL_SPEED * 0.5f;
        
        SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(g_ball);
        sprite->width = BALL_SIZE;
        sprite->height = BALL_SIZE;
        sprite->r = 1.0f; sprite->g = 1.0f; sprite->b = 1.0f; sprite->a = 1.0f;
        
        ColliderComponent* col = g_world.addComponent<ColliderComponent>(g_ball);
        col->width = BALL_SIZE;
        col->height = BALL_SIZE;
    }
    
    // Create walls
    g_topWall = g_world.createEntity("TopWall");
    {
        PositionComponent* pos = g_world.addComponent<PositionComponent>(g_topWall);
        pos->x = 400;
        pos->y = 5;
        
        SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(g_topWall);
        sprite->width = 800;
        sprite->height = WALL_HEIGHT;
        sprite->r = 0.5f; sprite->g = 0.5f; sprite->b = 0.5f; sprite->a = 1.0f;
        
        ColliderComponent* col = g_world.addComponent<ColliderComponent>(g_topWall);
        col->width = 800;
        col->height = WALL_HEIGHT;
    }
    
    g_bottomWall = g_world.createEntity("BottomWall");
    {
        PositionComponent* pos = g_world.addComponent<PositionComponent>(g_bottomWall);
        pos->x = 400;
        pos->y = 595;
        
        SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(g_bottomWall);
        sprite->width = 800;
        sprite->height = WALL_HEIGHT;
        sprite->r = 0.5f; sprite->g = 0.5f; sprite->b = 0.5f; sprite->a = 1.0f;
        
        ColliderComponent* col = g_world.addComponent<ColliderComponent>(g_bottomWall);
        col->width = 800;
        col->height = WALL_HEIGHT;
    }
    
    TD_LOG_INFO("Pong game initialized");
}

void gameUpdate(float dt) {
    const InputState& input = g_window->input;
    
    // Left paddle input (W/S)
    {
        VelocityComponent* vel = g_world.getComponent<VelocityComponent>(g_leftPaddle);
        if (vel) {
            vel->vy = 0;
            if (input.keys[Key::W]) vel->vy = -PADDLE_SPEED;
            if (input.keys[Key::S]) vel->vy = PADDLE_SPEED;
        }
    }
    
    // Right paddle input (Up/Down)
    {
        VelocityComponent* vel = g_world.getComponent<VelocityComponent>(g_rightPaddle);
        if (vel) {
            vel->vy = 0;
            if (input.keys[Key::Up]) vel->vy = -PADDLE_SPEED;
            if (input.keys[Key::Down]) vel->vy = PADDLE_SPEED;
        }
    }
    
    // Update positions
    EntityId entities[] = { g_leftPaddle, g_rightPaddle, g_ball };
    for (EntityId ent : entities) {
        PositionComponent* pos = g_world.getComponent<PositionComponent>(ent);
        VelocityComponent* vel = g_world.getComponent<VelocityComponent>(ent);
        if (pos && vel) {
            pos->x += vel->vx * dt;
            pos->y += vel->vy * dt;
        }
    }
    
    // Clamp paddles to screen
    {
        PositionComponent* pos = g_world.getComponent<PositionComponent>(g_leftPaddle);
        if (pos) {
            if (pos->y < WALL_HEIGHT + PADDLE_HEIGHT/2) pos->y = WALL_HEIGHT + PADDLE_HEIGHT/2;
            if (pos->y > 600 - WALL_HEIGHT - PADDLE_HEIGHT/2) pos->y = 600 - WALL_HEIGHT - PADDLE_HEIGHT/2;
        }
    }
    {
        PositionComponent* pos = g_world.getComponent<PositionComponent>(g_rightPaddle);
        if (pos) {
            if (pos->y < WALL_HEIGHT + PADDLE_HEIGHT/2) pos->y = WALL_HEIGHT + PADDLE_HEIGHT/2;
            if (pos->y > 600 - WALL_HEIGHT - PADDLE_HEIGHT/2) pos->y = 600 - WALL_HEIGHT - PADDLE_HEIGHT/2;
        }
    }
    
    // Ball collision with walls
    {
        PositionComponent* ballPos = g_world.getComponent<PositionComponent>(g_ball);
        VelocityComponent* ballVel = g_world.getComponent<VelocityComponent>(g_ball);
        
        if (ballPos && ballVel) {
            // Top/bottom walls
            if (ballPos->y < WALL_HEIGHT + BALL_SIZE/2) {
                ballPos->y = WALL_HEIGHT + BALL_SIZE/2;
                ballVel->vy = -ballVel->vy;
            }
            if (ballPos->y > 600 - WALL_HEIGHT - BALL_SIZE/2) {
                ballPos->y = 600 - WALL_HEIGHT - BALL_SIZE/2;
                ballVel->vy = -ballVel->vy;
            }
            
            // Left paddle collision
            PositionComponent* leftPos = g_world.getComponent<PositionComponent>(g_leftPaddle);
            if (leftPos && ballVel->vx < 0) {
                if (ballPos->x - BALL_SIZE/2 < leftPos->x + PADDLE_WIDTH/2 &&
                    ballPos->x > leftPos->x &&
                    ballPos->y > leftPos->y - PADDLE_HEIGHT/2 &&
                    ballPos->y < leftPos->y + PADDLE_HEIGHT/2) {
                    
                    ballPos->x = leftPos->x + PADDLE_WIDTH/2 + BALL_SIZE/2;
                    ballVel->vx = -ballVel->vx;
                    
                    // Add spin based on hit position
                    float hitPos = (ballPos->y - leftPos->y) / (PADDLE_HEIGHT/2);
                    ballVel->vy += hitPos * 100.0f;
                }
            }
            
            // Right paddle collision
            PositionComponent* rightPos = g_world.getComponent<PositionComponent>(g_rightPaddle);
            if (rightPos && ballVel->vx > 0) {
                if (ballPos->x + BALL_SIZE/2 > rightPos->x - PADDLE_WIDTH/2 &&
                    ballPos->x < rightPos->x &&
                    ballPos->y > rightPos->y - PADDLE_HEIGHT/2 &&
                    ballPos->y < rightPos->y + PADDLE_HEIGHT/2) {
                    
                    ballPos->x = rightPos->x - PADDLE_WIDTH/2 - BALL_SIZE/2;
                    ballVel->vx = -ballVel->vx;
                    
                    float hitPos = (ballPos->y - rightPos->y) / (PADDLE_HEIGHT/2);
                    ballVel->vy += hitPos * 100.0f;
                }
            }
            
            // Score
            if (ballPos->x < 0) {
                g_rightScore++;
                resetBall();
            }
            if (ballPos->x > 800) {
                g_leftScore++;
                resetBall();
            }
        }
    }
    
    // Update title with score
    char title[128];
    snprintf(title, sizeof(title), "Pong - Player: %d  |  AI: %d", g_leftScore, g_rightScore);
    g_window->setTitle(title);
}

void gameRender(float alpha) {
    (void)alpha;
    
    Renderer::get().clear(0.1f, 0.1f, 0.15f, 1.0f);
    Renderer::get().setViewport(0, 0, g_window->getWidth(), g_window->getHeight());
    
    Mat4 proj = g_camera.getProjection();
    Mat4 view = g_camera.getView();
    
    g_spriteBatch->begin(proj, view);
    
    // Draw center line
    for (int i = 0; i < 30; i++) {
        if (i % 2 == 0) {
            g_spriteBatch->drawQuad(395, i * 20.0f, 10, 15, 0.3f, 0.3f, 0.3f, 1.0f);
        }
    }
    
    // Draw all sprites
    EntityId entities[16];
    ComponentMask mask = componentBit(ComponentType::Position) | componentBit(ComponentType::Sprite);
    int count = g_world.queryActive(mask, entities, 16);
    
    for (int i = 0; i < count; i++) {
        PositionComponent* pos = g_world.getComponent<PositionComponent>(entities[i]);
        SpriteComponent* sprite = g_world.getComponent<SpriteComponent>(entities[i]);
        
        if (pos && sprite && sprite->visible) {
            SpriteData data;
            data.x = pos->x - sprite->width * sprite->originX;
            data.y = pos->y - sprite->height * sprite->originY;
            data.width = sprite->width;
            data.height = sprite->height;
            data.r = sprite->r;
            data.g = sprite->g;
            data.b = sprite->b;
            data.a = sprite->a;
            data.rotation = sprite->rotation;
            data.originX = sprite->originX;
            data.originY = sprite->originY;
            
            g_spriteBatch->draw(data, sprite->texture);
        }
    }
    
    g_spriteBatch->end();
}

int main() {
    // Initialize logger
    Logger::get().init("pong.log");
    
    // Create window
    WindowConfig config;
    config.title = "TD Engine - Pong";
    config.width = 800;
    config.height = 600;
    config.resizable = false;
    
    Win32Window window;
    if (!window.create(config)) {
        TD_LOG_ERROR("Failed to create window");
        return 1;
    }
    
    g_window = &window;
    
    // Run game loop
    GameLoop loop;
    loop.setCallbacks(gameInit, gameUpdate, gameRender);
    loop.setFixedStep(1.0f / 60.0f);
    loop.run(window);
    
    // Cleanup
    if (g_spriteBatch) {
        g_spriteBatch->shutdown();
        delete g_spriteBatch;
    }
    
    Renderer::get().shutdown();
    window.destroy();
    Logger::get().shutdown();
    
    return 0;
}
