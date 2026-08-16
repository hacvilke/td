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
//
// NOTE: the TDScript runtime (web/tdscript_runtime.js) currently exposes only
// Physics.checkVoxelCollision(pos) on the Physics global, plus Network.* and
// Log.* helpers. The richer Physics API (step, applyImpulse, createBody, ...)
// is implemented in the C++ engine and exposed to JS via TDEngine.physics.*,
// but not yet bridged into the TDScript runtime. Calls below that use only
// checkVoxelCollision will work; calls that need the broader Physics API are
// marked TODO and will return early until the bridge is added.
// =============================================================================

import "engine/networking";
import "engine/physics";
import "engine/log";

struct PlayerInputState {
    uint32 entityId;
    float  moveX;
    float  moveZ;
    bool   isJumping;
    bool   isFiring;
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
    bool  onBeat;
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
    //
    // TODO: Physics.step(dt) is not yet exposed to TDScript — the C++
    // engine's td_physics_step is reachable from JS via TDEngine.physics.step
    // but the TDScript runtime doesn't bridge it. The call below compiles
    // but is a no-op at runtime until the bridge lands.
    public void onTick(float dt) {
        // Physics.step(dt);  // TODO: bridge td_physics_step into tdscript_runtime
    }

    // Client-to-server RPC: receive this player's input frame.
    // @rpc(unreliable) means dropped packets are OK (input is high-frequency).
    @rpc(unreliable)
    public void receiveClientInput(PlayerInputState input, uint32 clientTick) {
        if (this.playerHealth <= 0) {
            return;
        }
        this.inputCount = this.inputCount + 1;

        // Validate the proposed move (anti-cheat: cap move speed).
        Vector3 proposedMove = Vector3(input.moveX, 0.0, input.moveZ) * 0.16;
        if (proposedMove.length() > 0.5) {
            // Player is trying to move too fast — clamp.
            proposedMove = proposedMove.normalize() * 0.5;
        }

        Vector3 validationTarget = this.playerPosition + proposedMove;
        // checkVoxelCollision is the one Physics method exposed to TDScript today.
        if (!Physics.checkVoxelCollision(validationTarget)) {
            this.playerPosition = validationTarget;
        } else {
            this.triggerClientCorrection(this.playerPosition, clientTick);
        }

        // TODO: bridge td_physics_apply_impulse into tdscript_runtime so
        // jumps can be authoritative. The line below compiles but is a no-op
        // at runtime until the bridge is added.
        // if (input.isJumping) {
        //     Physics.applyImpulse(input.entityId, Vector3(0.0, 6.0, 0.0));
        // }
    }

    // Client-to-server RPC: spawn an energy ball projectile.
    // @rpc(reliable) — projectile spawns MUST arrive or the game feels broken.
    //
    // TODO: the projectile physics (createBody, setSphereCollider, setRestitution,
    // applyImpulse) require the C++ td_physics_* bridge to be exposed to the
    // TDScript runtime. Currently only checkVoxelCollision is bridged.
    // For now we just track score/combo; the projectile itself is spawned
    // client-side and validated when the bridge lands.
    @rpc(reliable)
    public void spawnProjectile(ProjectileSpawnRequest req) {
        if (req.onBeat) {
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
        Network.sendToClient(0, "applyCorrection");
    }

    // Called when the player takes damage (e.g., from a falling prop).
    @rpc(reliable)
    public void processPlayerDamage(int32 weaponDamageValue) {
        int32 newHp = this.playerHealth - weaponDamageValue;
        if (newHp < 0) {
            newHp = 0;
        }
        this.playerHealth = newHp;
        if (this.playerHealth == 0) {
            this.handlePlayerDeath();
        }
    }

    private void handlePlayerDeath() {
        Log.warn("Player eliminated.");
        Network.broadcastNotification("A player has fallen.");
        // TODO: Network.schedule is not implemented in tdscript_runtime.js.
        // For now the client handles respawn locally; the server will reset
        // playerHealth/playerPosition on the next reconnect.
    }
}
