{
  "name": "__GAME_NAME__",
  "version": "1.0.0",
  "engineVersion": "1.0.0",
  "description": "A TD Engine game",
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
    "modes": ["RELIABLE_ORDERED", "UNRELIABLE"]
  },
  "entry": {
    "clientScript": "game.js",
    "serverScript": "src/server/server_main.td",
    "mainClass": "ServerMain"
  },
  "assets": {
    "root": "assets"
  }
}
