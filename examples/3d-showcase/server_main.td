// =============================================================================
// TD Sandbox — server-side TDScript example.
// -----------------------------------------------------------------------------
// This file demonstrates what the server-authoritative gameplay code for the
// showcase would look like in TDScript.  It's not actually loaded by the
// browser — it would be loaded by `td serve` and run in the server's VM.
//
// Compile and run with:
//   td script compile src/server/server_main.td
//   td serve
//
// The browser (examples/3d-showcase/) then connects to ws://127.0.0.1:8081/room
// and the runtime dispatches the @rpc methods below.
// =============================================================================

import "engine/networking";
import "engine/physics";
import "engine/log";

struct PlayerInputState {
    uint32 entityId;
    float  moveX;
    float  moveZ;
    boolean isJumping;
    boolean isFiring;
    float  cameraYaw;
    float  cameraPitch;
}

struct ProjectileSpawnRequest {
    float originX;
    float originY;
    float originZ;
    float dirX;
    float dirY;
    float dirZ;
    boolean onBeat;
}

class ServerPlayerHandler {
    // Replicated fields are pushed to all clients every networking.tickRateHz.
    replicated Vector3 playerPosition;
    replicated int32   playerHealth;
    replicated uint32  playerScore;
    replicated uint32  playerCombo;

    // Per-player state (not replicated).
    float  lastInputTick;
    uint32 inputCount;

    // Called once when the server boots.
    public void onServerStart() {
        Log.info("TD Sandbox server online.");
        Log.info("  tickRate: 60 Hz");
        Log.info("  protocol: WebSocket / JSON-RPC");
        this.playerHealth = 100;
        this.playerScore  = 0;
        this.playerCombo  = 0;
        this.playerPosition = Vector3(0.0, 2.0, 0.0);
    }

    // Called every fixed tick (60 Hz).  In a real game this would advance
    // the world simulation, check win conditions, etc.
    public void onTick(float dt) {
        // Apply gravity, integrate positions, resolve collisions.
        Physics.step(dt);
    }

    // Client-to-server RPC: receive this player's input frame.
    // @rpc(unreliable) means dropped packets are OK (input is high-frequency).
    @rpc(unreliable)
    public void receiveClientInput(PlayerInputState input, uint32 clientTick) {
        if (this.playerHealth <= 0) {
            return;
        }
        this.inputCount++;

        // Validate the proposed move (anti-cheat: cap move speed).
        Vector3 proposedMove = Vector3(input.moveX, 0.0, input.moveZ) * 0.16;
        if (proposedMove.length() > 0.5) {
            // Player is trying to move too fast — clamp.
            proposedMove = proposedMove.normalize() * 0.5;
        }

        Vector3 validationTarget = this.playerPosition + proposedMove;
        if (!Physics.checkCapsuleCollision(validationTarget, 0.4, 1.6)) {
            this.playerPosition = validationTarget;
        } else {
            this.triggerClientCorrection(this.playerPosition, clientTick);
        }

        if (input.isJumping) {
            Physics.applyImpulse(input.entityId, Vector3(0.0, 6.0, 0.0));
        }
    }

    // Client-to-server RPC: spawn an energy ball projectile.
    // @rpc(reliable) — projectile spawns MUST arrive or the game feels broken.
    @rpc(reliable)
    public void spawnProjectile(ProjectileSpawnRequest req) {
        uint32 projectileId = Physics.createBody(0.2, req.originX, req.originY, req.originZ);
        Physics.setSphereCollider(projectileId, 0.25);
        Physics.setRestitution(projectileId, 0.9);
        Physics.applyImpulse(projectileId, Vector3(req.dirX, req.dirY, req.dirZ) * 6.0);

        // On-beat shots get a bonus impulse (synergy with the rhythm system).
        if (req.onBeat) {
            Physics.applyImpulse(projectileId, Vector3(0.0, 2.0, 0.0));
            this.playerCombo += 1;
            this.playerScore += 100 * this.playerCombo;
        } else {
            this.playerCombo = 0;
            this.playerScore += 10;
        }
    }

    // Server-to-client RPC: tell the client their move was invalid and they
    // need to snap back to the server's authoritative position.
    private void triggerClientCorrection(Vector3 authoritativePos, uint32 clientTick) {
        Network.sendToClient("applyCorrection", {
            position = authoritativePos,
            tick     = clientTick,
        });
    }

    // Called when the player takes damage (e.g., from a falling prop).
    @rpc(reliable)
    public void processPlayerDamage(int32 weaponDamageValue) {
        this.playerHealth = Math.max(0, this.playerHealth - weaponDamageValue);
        if (this.playerHealth == 0) {
            this.handlePlayerDeath();
        }
    }

    private void handlePlayerDeath() {
        Log.warn("Player eliminated.");
        Network.broadcastNotification("A player has fallen.");
        // Respawn after 3 seconds.
        Network.schedule(3.0, () => {
            this.playerHealth = 100;
            this.playerPosition = Vector3(0.0, 2.0, 0.0);
            Network.sendToClient("respawn", { position = this.playerPosition });
        });
    }
}
