{
  "name": "td-sandbox-showcase",
  "version": "1.0.0",
  "engineVersion": "2026.4.1",
  "config": {
    "renderBackend": "WebGL2",
    "targetFrameRate": 60,
    "wasmMemory": "default",
    "canvasId": "game-canvas"
  },
  "networking": {
    "defaultServer": "ws://127.0.0.1:8081/room",
    "tickRateHz": 60,
    "protocol": "WebSocket",
    "modes": ["RELIABLE_ORDERED"]
  },
  "entry": {
    "mainScript": "src/server/server_main.td",
    "clientScript": "examples/3d-showcase/game/game.js",
    "mainClass": "ServerPlayerHandler"
  },
  "showcase": {
    "subsystems": [
      "physics",
      "ecs",
      "input",
      "audio",
      "i18n",
      "persistence",
      "beat",
      "shaderGraph",
      "networking"
    ],
    "tutorialSteps": 8
  }
}
