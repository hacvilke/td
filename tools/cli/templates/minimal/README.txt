__GAME_NAME__
=============

A game built with TD Engine.

Quick start:
  td serve .           # dev server at http://localhost:8080 + game-net at :8081
  td build .           # builds WASM + assembles build/
  td test              # runs engine tests
  td script check src/server/server_main.td   # type-check your TDScript
  td script compile src/server/server_main.td -o build/server_main.js
  td bundle -path . -config bundle.json    # builds a Windows installer

Files:
  index.html       HTML host. Loads engine from runtime/, then runs game.js.
  game.js          Your client-side game logic.
  project.td       Project config (name, version, networking, entry points).
                   Like package.json, but for a TD Engine game.
  bundle.json      Installer config (name, version, icon, publisher, ...).
  src/server/
    server_main.td TDScript — server-authoritative gameplay logic.
                   Compiled to JS by `td script compile`. Loaded by the
                   game-net server during `td serve`. Has `replicated`
                   fields and `@rpc(reliable|unreliable)` methods.

TDScript quick reference:
  struct PlayerInput { uint32 entityId; float moveX; bool isJumping; }
  class ServerMain {
    replicated int32 playerHealth = 100;
    @rpc(reliable)
    public void processPlayerDamage(int32 dmg) { this.playerHealth = this.playerHealth - dmg; }
  }

Learn more:
  Engine repo:     https://github.com/hacvilke/td
