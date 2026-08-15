// =============================================================================
// TDScript — server_main.td
//
// This is the server-authoritative gameplay script for your TD Engine game.
// It runs in two contexts:
//   - In the browser (client-side prediction) — compiled to JS by `td script compile`
//   - On the Node.js dedicated server (`td serve` boots a WebSocket server
//     that loads this file via the TDScript runtime).
//
// The `replicated` keyword marks fields that the server automatically
// broadcasts to all clients on mutation.
// The `@rpc(reliable)` / `@rpc(unreliable)` decorators mark methods that
// can be invoked across the network.
// =============================================================================

import "engine/networking";
import "engine/physics";

// Player input struct — sent from client to server each tick.
struct PlayerInputState {
    uint32 entityId;
    float moveX;
    float moveZ;
    bool isJumping;
}

// The main server-side player handler. Instantiated once per connected client.
class ServerMain {

    // Replicated fields: server is the source of truth; clients receive pushes.
    replicated Vector3 playerPosition;
    replicated int32 playerHealth;

    public void onServerStart() {
        Log.info("TD Engine server authority protocol online.");
        this.playerHealth = 100;
        this.playerPosition = Vector3(0.0, 64.0, 0.0);
    }

    // Handles rapid real-time player inputs.
    // Marked @rpc(unreliable) so dropped packets don't block newer inputs.
    @rpc(unreliable)
    public void receiveClientInput(PlayerInputState input, uint32 clientTick) {
        if (this.playerHealth <= 0) {
            return;
        }

        // Server-authoritative movement validation
        Vector3 proposedMove = Vector3(input.moveX, 0.0, input.moveZ) * 0.16;
        Vector3 validationTarget = this.playerPosition + proposedMove;

        if (!Physics.checkVoxelCollision(validationTarget)) {
            // Setting a replicated field auto-broadcasts to all clients
            this.playerPosition = validationTarget;
        } else {
            // Force a client correction
            this.triggerClientCorrection(this.playerPosition, clientTick);
        }
    }

    // Reliable RPC — combat events must be delivered exactly once.
    @rpc(reliable)
    public void processPlayerDamage(int32 weaponDamageValue) {
        this.playerHealth = Math.max(0, this.playerHealth - weaponDamageValue);

        if (this.playerHealth == 0) {
            this.handlePlayerDeath();
        }
    }

    private void handlePlayerDeath() {
        Log.warn("Player entity has been neutralized by server validation check.");
        Network.broadcastNotification("A player has fallen in battle.");
    }

    private void triggerClientCorrection(Vector3 position, uint32 clientTick) {
        // The replicated setter on playerPosition will broadcast the correction
        // automatically; this method exists for logging / future extension.
        Log.info("Correcting client tick " + clientTick.toString());
    }
}
