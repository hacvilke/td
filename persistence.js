// =============================================================================
// TD Engine — Persistence Layer (Save / Load / Autosave)
// File: web/persistence.js
//
// Refresh-proof game state for the web. Web games lose everything on F5;
// this module gives them Godot-like ResourceSaver semantics: games register
// serializers for their own state, the engine handles storage + transport.
//
// Why game-registered serializers (not auto-serialize ECS):
//   The engine has setters for position/velocity/sprite/collider but only a
//   getter for position. Auto-serializing ECS would silently lose most state.
//   Games know their own state shape — let them own serialization. The
//   engine handles slot management, versioning, autosave, export/import.
//
// Public API:
//
//   TDPersistence.registerSerializer(name, serializeFn, deserializeFn)
//      Register a named serializer. serializeFn() returns a JSON-able object
//      (or null to skip). deserializeFn(data) restores state. Returns true
//      on success, false if name is invalid or already registered.
//
//   TDPersistence.unregisterSerializer(name)
//      Remove a serializer by name.
//
//   TDPersistence.save(slotName)
//      Call all registered serializers, wrap in a versioned envelope, write
//      to localStorage under 'td-save-<slotName>'. Returns the envelope
//      (or null on failure).
//
//   TDPersistence.load(slotName)
//      Read envelope from localStorage, call deserializers. Returns
//      { ok, restored: [...names], missing: [...names], error? }.
//
//   TDPersistence.list()
//      Returns array of { name, timestamp, sizeBytes, slotNames, version }.
//
//   TDPersistence.delete(slotName)
//      Remove a save slot. Returns true if removed, false if not found.
//
//   TDPersistence.exportJson(slotName)
//      Returns the envelope as a pretty-printed JSON string (for download
//      or share). Returns null if slot not found.
//
//   TDPersistence.importJson(jsonString, slotName)
//      Parse JSON, validate envelope, save under slotName. Does NOT call
//      deserializers — call load(slotName) afterwards. Returns true on
//      success, false on parse/validation failure.
//
//   TDPersistence.autosave(slotName, intervalMs)
//      Set up an interval that calls save(slotName) every intervalMs.
//      Returns a handle with .stop(). Only one autosave per slot; calling
//      again for the same slot resets the interval.
//
//   TDPersistence.stopAllAutosaves()
//      Cancel every active autosave interval.
//
//   TDPersistence.clearAll()
//      Wipe ALL save slots from localStorage. Destructive. Returns count.
//
//   TDPersistence.snapshot()
//      Returns { serializers: [...names], slots: [...list()], autosaves:
//      [...slotNames] } — for tests / inspection.
//
// Storage envelope format (versioned):
//   {
//     version: 1,
//     engine:  '<engine version or "unknown">',
//     savedAt: <Date.now()>,
//     slot:    '<slotName>',
//     data:    { <serializerName>: <serializer data>, ... }
//   }
//
// Strictly additive: if localStorage isn't available, falls back to an
// in-memory Map. If TDEngine isn't loaded, save/load still work — they
// just don't include engine version metadata.
// =============================================================================

(function (global) {
  'use strict';

  // ---- serializer registry ---------------------------------------------------

  const serializers = new Map();  // name -> { serialize, deserialize }

  function registerSerializer(name, serializeFn, deserializeFn) {
    if (typeof name !== 'string' || !name) return false;
    if (typeof serializeFn !== 'function' || typeof deserializeFn !== 'function') return false;
    // Allow re-registration: replace existing. This is intentional — games
    // may hot-swap serializers during development.
    serializers.set(name, { serialize: serializeFn, deserialize: deserializeFn });
    return true;
  }

  function unregisterSerializer(name) {
    return serializers.delete(name);
  }

  // ---- storage backend (localStorage with in-memory fallback) ----------------

  const memoryStore = new Map();

  function storageKey(slotName) { return 'td-save-' + slotName; }

  function lsGet(slotName) {
    const k = storageKey(slotName);
    try {
      if (typeof localStorage !== 'undefined' && localStorage.getItem) {
        const raw = localStorage.getItem(k);
        return raw || null;
      }
    } catch (e) {}
    return memoryStore.has(k) ? memoryStore.get(k) : null;
  }

  function lsSet(slotName, value) {
    const k = storageKey(slotName);
    try {
      if (typeof localStorage !== 'undefined' && localStorage.setItem) {
        localStorage.setItem(k, value);
        return;
      }
    } catch (e) {}
    memoryStore.set(k, value);
  }

  function lsDelete(slotName) {
    const k = storageKey(slotName);
    let existed = false;
    try {
      if (typeof localStorage !== 'undefined' && localStorage.removeItem) {
        const before = localStorage.getItem(k);
        existed = before !== null;
        localStorage.removeItem(k);
        return existed;
      }
    } catch (e) {}
    existed = memoryStore.has(k);
    memoryStore.delete(k);
    return existed;
  }

  function lsListSlots() {
    const out = [];
    try {
      if (typeof localStorage !== 'undefined' && localStorage.length !== undefined) {
        for (let i = 0; i < localStorage.length; i++) {
          const k = localStorage.key(i);
          if (k && k.indexOf('td-save-') === 0) {
            out.push(k.slice('td-save-'.length));
          }
        }
        return out;
      }
    } catch (e) {}
    memoryStore.forEach(function (v, k) {
      if (k.indexOf('td-save-') === 0) out.push(k.slice('td-save-'.length));
    });
    return out;
  }

  // ---- engine version --------------------------------------------------------

  function engineVersion() {
    if (global.TDEngine && typeof global.TDEngine.lifecycle === 'object') {
      try { return global.TDEngine.lifecycle.getVersion(); } catch (e) {}
    }
    return 'unknown';
  }

  // ---- save / load -----------------------------------------------------------

  function save(slotName) {
    if (typeof slotName !== 'string' || !slotName) return null;
    const data = {};
    serializers.forEach(function (s, name) {
      try {
        const result = s.serialize();
        if (result !== null && result !== undefined) data[name] = result;
      } catch (e) {
        // Log but don't fail the whole save — other serializers should still run
        if (typeof console !== 'undefined' && console.warn) {
          console.warn('[TDPersistence] serializer "' + name + '" threw:', e);
        }
      }
    });
    const envelope = {
      version: 1,
      engine: engineVersion(),
      savedAt: Date.now(),
      slot: slotName,
      data: data,
    };
    try {
      lsSet(slotName, JSON.stringify(envelope));
      return envelope;
    } catch (e) {
      if (typeof console !== 'undefined' && console.error) {
        console.error('[TDPersistence] save failed:', e);
      }
      return null;
    }
  }

  function load(slotName) {
    if (typeof slotName !== 'string' || !slotName) {
      return { ok: false, restored: [], missing: [], error: 'invalid-slot-name' };
    }
    const raw = lsGet(slotName);
    if (!raw) {
      return { ok: false, restored: [], missing: [], error: 'slot-not-found' };
    }
    let envelope;
    try { envelope = JSON.parse(raw); }
    catch (e) { return { ok: false, restored: [], missing: [], error: 'parse-failed' }; }
    if (!envelope || typeof envelope !== 'object' || envelope.version !== 1 || !envelope.data) {
      return { ok: false, restored: [], missing: [], error: 'invalid-envelope' };
    }
    const restored = [];
    const missing = [];
    // Call deserializers for every registered serializer. If a serializer's
    // data isn't in the envelope, record it as missing (the game may have
    // been updated since the save was made).
    serializers.forEach(function (s, name) {
      if (Object.prototype.hasOwnProperty.call(envelope.data, name)) {
        try {
          s.deserialize(envelope.data[name]);
          restored.push(name);
        } catch (e) {
          if (typeof console !== 'undefined' && console.warn) {
            console.warn('[TDPersistence] deserialize "' + name + '" threw:', e);
          }
          missing.push(name);
        }
      } else {
        missing.push(name);
      }
    });
    return { ok: true, restored: restored, missing: missing, envelope: envelope };
  }

  function list() {
    const slots = lsListSlots();
    return slots.map(function (name) {
      const raw = lsGet(name);
      let timestamp = 0, slotNames = [], version = 0, sizeBytes = 0;
      if (raw) {
        sizeBytes = raw.length;
        try {
          const env = JSON.parse(raw);
          timestamp = env.savedAt || 0;
          slotNames = env.data ? Object.keys(env.data) : [];
          version = env.version || 0;
        } catch (e) {}
      }
      return { name: name, timestamp: timestamp, sizeBytes: sizeBytes, slotNames: slotNames, version: version };
    }).sort(function (a, b) { return b.timestamp - a.timestamp; });
  }

  function del(slotName) {
    return lsDelete(slotName);
  }

  function exportJson(slotName) {
    const raw = lsGet(slotName);
    if (!raw) return null;
    try {
      // Pretty-print for human readability
      return JSON.stringify(JSON.parse(raw), null, 2);
    } catch (e) { return raw; }
  }

  function importJson(jsonString, slotName) {
    if (typeof jsonString !== 'string' || typeof slotName !== 'string' || !slotName) return false;
    let envelope;
    try { envelope = JSON.parse(jsonString); }
    catch (e) { return false; }
    if (!envelope || typeof envelope !== 'object' || envelope.version !== 1 || !envelope.data) return false;
    // Re-stamp the slot name + savedAt so we know when it was imported
    envelope.slot = slotName;
    envelope.savedAt = Date.now();
    try {
      lsSet(slotName, JSON.stringify(envelope));
      return true;
    } catch (e) { return false; }
  }

  // ---- autosave --------------------------------------------------------------

  const autosaveTimers = new Map();  // slotName -> interval id

  function autosave(slotName, intervalMs) {
    if (typeof slotName !== 'string' || !slotName) return null;
    if (typeof intervalMs !== 'number' || intervalMs < 1000) intervalMs = 5000;  // floor at 1s
    // Clear existing timer for this slot
    if (autosaveTimers.has(slotName)) {
      clearInterval(autosaveTimers.get(slotName));
    }
    const id = setInterval(function () {
      save(slotName);
    }, intervalMs);
    autosaveTimers.set(slotName, id);
    return {
      slotName: slotName,
      intervalMs: intervalMs,
      stop: function () {
        if (autosaveTimers.has(slotName)) {
          clearInterval(autosaveTimers.get(slotName));
          autosaveTimers.delete(slotName);
        }
      },
    };
  }

  function stopAllAutosaves() {
    autosaveTimers.forEach(function (id) { clearInterval(id); });
    autosaveTimers.clear();
  }

  function clearAll() {
    const slots = lsListSlots();
    let count = 0;
    slots.forEach(function (name) {
      if (lsDelete(name)) count++;
    });
    return count;
  }

  function snapshot() {
    return {
      serializers: Array.from(serializers.keys()),
      slots: list(),
      autosaves: Array.from(autosaveTimers.keys()),
    };
  }

  // ---- Public API ------------------------------------------------------------

  const TDPersistence = {
    registerSerializer: registerSerializer,
    unregisterSerializer: unregisterSerializer,
    save: save,
    load: load,
    list: list,
    delete: del,
    exportJson: exportJson,
    importJson: importJson,
    autosave: autosave,
    stopAllAutosaves: stopAllAutosaves,
    clearAll: clearAll,
    snapshot: snapshot,
    version: '1.0.0',
  };

  global.TDPersistence = TDPersistence;

})(typeof window !== 'undefined' ? window : this);
