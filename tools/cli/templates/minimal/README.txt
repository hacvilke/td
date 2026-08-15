__GAME_NAME__
=============

A game built with TD Engine.

Quick start:
  td serve .           # dev server at http://localhost:8080
  td build .           # builds WASM + assembles build/
  td test              # runs engine tests
  td bundle -path . -config bundle.json    # builds a Windows installer

Files:
  index.html       HTML host. Loads engine from runtime/, then runs game.js.
  game.js          Your game logic.
  bundle.json      Installer config (name, version, icon, publisher, ...).

Learn more:
  Engine repo:     https://github.com/hacvilke/td
