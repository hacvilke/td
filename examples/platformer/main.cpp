// TD Engine - Platformer Example
// A complete 2D platformer using the TD Engine

#include "../../src/platform/platform.h"
#include "../../src/platform/win32_window.h"
#include "../../src/core/game_loop.h"
#include "../../src/core/logger.h"
#include "../../src/renderer/gl_renderer.h"
#include "../../src/renderer/sprite_batch.h"
#include "../../src/renderer/camera.h"
#include "../../src/ecs/world.h"
#include "../../src/physics/aabb.h"
#include "../../src/physics/collision.h"
#include <cstdio>

using namespace td;

// Game state
static Win32Window* g_window = nullptr;
static SpriteBatch* g_spriteBatch = nullptr;
static Camera2D g_camera;
static World g_world;

// Entity IDs
static EntityId g_player;
static EntityId g_platforms[10];
static int g_platformCount = 0;
static EntityId g_enemies[5];
static int g_enemyCount = 0;

// Player state
static bool g_onGround = false;
static int g_score = 0;

// Game constants
static const float GRAVITY = 800.0f;
static const float PLAYER_SPEED = 200.0f;
static const float JUMP_FORCE = 400.0f;
static const float PLAYER_SIZE = 32.0f;
static const float ENEMY_SPEED = 50.0f;

EntityId createPlatform(float x, float y, float width, float height) {
    EntityId id = g_world.createEntity("Platform");
    
    PositionComponent* pos = g_world.addComponent<PositionComponent>(id);
    pos->x = x;
    pos->y = y;
    
    SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(id);
    sprite->width = width;
    sprite->height = height;
    sprite->r = 0.4f; sprite->g = 0.6f; sprite->b = 0.3f; sprite->a = 1.0f;
    sprite->originX = 0; sprite->originY = 0;
    
    ColliderComponent* col = g_world.addComponent<ColliderComponent>(id);
    col->width = width;
    col->height = height;
    col->offsetX = width / 2;
    col->offsetY = height / 2;
    
    RigidBodyComponent* rb = g_world.addComponent<RigidBodyComponent>(id);
    rb->isStatic = true;
    
    return id;
}

EntityId createEnemy(float x, float y, float width, float height) {
    EntityId id = g_world.createEntity("Enemy");
    
    PositionComponent* pos = g_world.addComponent<PositionComponent>(id);
    pos->x = x;
    pos->y = y;
    
    VelocityComponent* vel = g_world.addComponent<VelocityComponent>(id);
    vel->vx = ENEMY_SPEED;
    vel->vy = 0;
    
    SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(id);
    sprite->width = width;
    sprite->height = height;
    sprite->r = 0.8f; sprite->g = 0.2f; sprite->b = 0.2f; sprite->a = 1.0f;
    sprite->originX = 0; sprite->originY = 0;
    
    ColliderComponent* col = g_world.addComponent<ColliderComponent>(id);
    col->width = width;
    col->height = height;
    col->offsetX = width / 2;
    col->offsetY = height / 2;
    
    return id;
}

void gameInit() {
    TD_LOG_INFO("Platformer game initializing...");
    
    Renderer::get().init();
    
    g_spriteBatch = new SpriteBatch();
    g_spriteBatch->init();
    
    g_camera.setViewport(800, 600);
    g_camera.setPosition(400, 300);
    
    // Create player
    g_player = g_world.createEntity("Player");
    {
        PositionComponent* pos = g_world.addComponent<PositionComponent>(g_player);
        pos->x = 100;
        pos->y = 400;
        
        VelocityComponent* vel = g_world.addComponent<VelocityComponent>(g_player);
        vel->vx = 0;
        vel->vy = 0;
        
        SpriteComponent* sprite = g_world.addComponent<SpriteComponent>(g_player);
        sprite->width = PLAYER_SIZE;
        sprite->height = PLAYER_SIZE;
        sprite->r = 0.2f; sprite->g = 0.5f; sprite->b = 0.9f; sprite->a = 1.0f;
        sprite->originX = 0; sprite->originY = 0;
        
        ColliderComponent* col = g_world.addComponent<ColliderComponent>(g_player);
        col->width = PLAYER_SIZE;
        col->height = PLAYER_SIZE;
        col->offsetX = PLAYER_SIZE / 2;
        col->offsetY = PLAYER_SIZE / 2;
        
        RigidBodyComponent* rb = g_world.addComponent<RigidBodyComponent>(g_player);
        rb->useGravity = true;
        rb->mass = 1.0f;
    }
    
    // Create ground
    g_platforms[g_platformCount++] = createPlatform(0, 550, 800, 50);
    
    // Create platforms
    g_platforms[g_platformCount++] = createPlatform(100, 450, 150, 20);
    g_platforms[g_platformCount++] = createPlatform(350, 380, 150, 20);
    g_platforms[g_platformCount++] = createPlatform(550, 300, 150, 20);
    g_platforms[g_platformCount++] = createPlatform(250, 220, 150, 20);
    g_platforms[g_platformCount++] = createPlatform(50, 150, 150, 20);
    
    // Create enemies
    g_enemies[g_enemyCount++] = createEnemy(120, 420, 24, 24);
    g_enemies[g_enemyCount++] = createEnemy(360, 350, 24, 24);
    g_enemies[g_enemyCount++] = createEnemy(560, 270, 24, 24);
    
    TD_LOG_INFO("Platformer game initialized");
}

void gameUpdate(float dt) {
    const InputState& input = g_window->input;
    
    PositionComponent* playerPos = g_world.getComponent<PositionComponent>(g_player);
    VelocityComponent* playerVel = g_world.getComponent<VelocityComponent>(g_player);
    ColliderComponent* playerCol = g_world.getComponent<ColliderComponent>(g_player);
    
    if (!playerPos || !playerVel || !playerCol) return;
    
    // Player input
    playerVel->vx = 0;
    if (input.keys[Key::Left] || input.keys[Key::A]) playerVel->vx = -PLAYER_SPEED;
    if (input.keys[Key::Right] || input.keys[Key::D]) playerVel->vx = PLAYER_SPEED;
    
    // Jump
    if ((input.keyPressed(Key::Space) || input.keyPressed(Key::Up) || input.keyPressed(Key::W)) && g_onGround) {
        playerVel->vy = -JUMP_FORCE;
        g_onGround = false;
    }
    
    // Apply gravity
    playerVel->vy += GRAVITY * dt;
    
    // Update player position
    playerPos->x += playerVel->vx * dt;
    playerPos->y += playerVel->vy * dt;
    
    // Reset ground flag
    g_onGround = false;
    
    // Player collision with platforms
    CollisionDetector detector;
    AABB playerAABB = AABB::fromMinSize(
        playerPos->x, playerPos->y,
        PLAYER_SIZE, PLAYER_SIZE
    );
    
    for (int i = 0; i < g_platformCount; i++) {
        PositionComponent* platPos = g_world.getComponent<PositionComponent>(g_platforms[i]);
        SpriteComponent* platSprite = g_world.getComponent<SpriteComponent>(g_platforms[i]);
        
        if (!platPos || !platSprite) continue;
        
        AABB platAABB = AABB::fromMinSize(
            platPos->x, platPos->y,
            platSprite->width, platSprite->height
        );
        
        CollisionResult result = detector.testAABB(playerAABB, platAABB);
        
        if (result.colliding) {
            // Resolve collision
            if (result.normalY < 0 && playerVel->vy > 0) {
                // Landing on top
                playerPos->y = platPos->y - PLAYER_SIZE;
                playerVel->vy = 0;
                g_onGround = true;
            }
            else if (result.normalY > 0 && playerVel->vy < 0) {
                // Hitting from below
                playerPos->y = platPos->y + platSprite->height;
                playerVel->vy = 0;
            }
            else if (result.normalX != 0) {
                // Side collision
                playerPos->x -= result.normalX * result.penetration;
            }
            
            // Update AABB for next collision
            playerAABB = AABB::fromMinSize(playerPos->x, playerPos->y, PLAYER_SIZE, PLAYER_SIZE);
        }
    }
    
    // Update enemies
    for (int i = 0; i < g_enemyCount; i++) {
        PositionComponent* enemyPos = g_world.getComponent<PositionComponent>(g_enemies[i]);
        VelocityComponent* enemyVel = g_world.getComponent<VelocityComponent>(g_enemies[i]);
        SpriteComponent* enemySprite = g_world.getComponent<SpriteComponent>(g_enemies[i]);
        
        if (!enemyPos || !enemyVel || !enemySprite) continue;
        
        // Move enemy
        enemyPos->x += enemyVel->vx * dt;
        
        // Simple AI: reverse at screen edges
        if (enemyPos->x < 0) {
            enemyPos->x = 0;
            enemyVel->vx = ENEMY_SPEED;
        }
        if (enemyPos->x > 800 - enemySprite->width) {
            enemyPos->x = 800 - enemySprite->width;
            enemyVel->vx = -ENEMY_SPEED;
        }
        
        // Check collision with player
        AABB enemyAABB = AABB::fromMinSize(
            enemyPos->x, enemyPos->y,
            enemySprite->width, enemySprite->height
        );
        
        CollisionResult result = detector.testAABB(playerAABB, enemyAABB);
        
        if (result.colliding) {
            // Check if player is jumping on enemy
            if (playerVel->vy > 0 && playerPos->y + PLAYER_SIZE - 10 < enemyPos->y + enemySprite->height / 2) {
                // Kill enemy
                g_world.setEntityEnabled(g_enemies[i], false);
                playerVel->vy = -JUMP_FORCE * 0.7f;
                g_score += 100;
                TD_LOG_INFO("Enemy killed! Score: %d", g_score);
            }
            else {
                // Player dies - reset
                playerPos->x = 100;
                playerPos->y = 400;
                playerVel->vx = 0;
                playerVel->vy = 0;
                TD_LOG_INFO("Player died!");
            }
        }
    }
    
    // Fall off screen - reset
    if (playerPos->y > 650) {
        playerPos->x = 100;
        playerPos->y = 400;
        playerVel->vx = 0;
        playerVel->vy = 0;
    }
    
    // Keep player on screen horizontally
    if (playerPos->x < 0) playerPos->x = 0;
    if (playerPos->x > 800 - PLAYER_SIZE) playerPos->x = 800 - PLAYER_SIZE;
    
    // Camera follows player (smoothly)
    float targetX = playerPos->x + PLAYER_SIZE / 2;
    float targetY = playerPos->y + PLAYER_SIZE / 2;
    Vec2 camPos = g_camera.getPosition();
    g_camera.setPosition(
        camPos.x + (targetX - camPos.x) * 5.0f * dt,
        camPos.y + (targetY - camPos.y) * 5.0f * dt
    );
    
    // Update window title
    char title[128];
    snprintf(title, sizeof(title), "Platformer - Score: %d", g_score);
    g_window->setTitle(title);
}

void gameRender(float alpha) {
    (void)alpha;
    
    Renderer::get().clear(0.15f, 0.15f, 0.2f, 1.0f);
    Renderer::get().setViewport(0, 0, g_window->getWidth(), g_window->getHeight());
    
    Mat4 proj = g_camera.getProjection();
    Mat4 view = g_camera.getView();
    
    g_spriteBatch->begin(proj, view);
    
    // Draw all sprites
    EntityId entities[64];
    ComponentMask mask = componentBit(ComponentType::Position) | componentBit(ComponentType::Sprite);
    int count = g_world.queryActive(mask, entities, 64);
    
    for (int i = 0; i < count; i++) {
        PositionComponent* pos = g_world.getComponent<PositionComponent>(entities[i]);
        SpriteComponent* sprite = g_world.getComponent<SpriteComponent>(entities[i]);
        
        if (pos && sprite && sprite->visible) {
            SpriteData data;
            data.x = pos->x;
            data.y = pos->y;
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
    Logger::get().init("platformer.log");
    
    WindowConfig config;
    config.title = "TD Engine - Platformer";
    config.width = 800;
    config.height = 600;
    config.resizable = false;
    
    Win32Window window;
    if (!window.create(config)) {
        TD_LOG_ERROR("Failed to create window");
        return 1;
    }
    
    g_window = &window;
    
    GameLoop loop;
    loop.setCallbacks(gameInit, gameUpdate, gameRender);
    loop.setFixedStep(1.0f / 60.0f);
    loop.run(window);
    
    if (g_spriteBatch) {
        g_spriteBatch->shutdown();
        delete g_spriteBatch;
    }
    
    Renderer::get().shutdown();
    window.destroy();
    Logger::get().shutdown();
    
    return 0;
}
