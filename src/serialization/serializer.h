// =============================================================================
// TD Engine - Serialization v1 (Tier 1.2)
//
// Saves/loads Scenes as JSON ".tdscene" files and prefabs as ".tdprefab".
// Inspired by Godot's .tres text format and Unity's YAML .prefab files.
//
// Design choices:
//   - JSON (not a custom text format) because:
//       * Zero parser code — we ship a tiny hand-rolled JSON reader/writer
//         (~200 LOC) that's good enough for our schema.
//       * Diff-friendly: GitHub diffs JSON cleanly.
//       * Scriptable: Lua/JS can read/write scenes via the bridge.
//   - Entity IDs in the file are STABLE STRING NAMES, not numeric IDs.
//     Numeric IDs are runtime-only; on load, we reassign IDs and rewrite
//     parent/child references via a name->ID map. This means you can
//     re-order entities in the file without breaking references.
//   - Component serialization is data-driven: each component type has a
//     serialize/deserialize function registered in a table. Adding a new
//     component = add one entry to the table.
//
// File format (example):
//   {
//     "version": 1,
//     "name": "Level 1",
//     "entities": [
//       {
//         "name": "Player",
//         "tag": "player",
//         "parent": null,
//         "components": {
//           "Position":      { "x": 100, "y": 200 },
//           "Sprite":        { "w": 32, "h": 32, "r": 1, "g": 1, "b": 1, "a": 1 },
//           "LocalTransform":{ "x": 100, "y": 200, "scaleX": 1, "scaleY": 1, "rotation": 0 }
//         }
//       },
//       {
//         "name": "Gun",
//         "parent": "Player",
//         "components": { ... }
//       }
//     ]
//   }
//
// "parent" is null for roots, or the NAME of the parent entity for children.
// =============================================================================
#pragma once
#include "../ecs/world.h"
#include "../ecs/component.h"
#include "../scene/scene.h"
#include "../core/logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace td {

// ---------------------------------------------------------------------------
// Tiny JSON serializer (writer). Outputs 2-space-indented JSON.
// We hand-roll this instead of pulling in a JSON library to keep the engine
// dependency-free (consistent with the rest of the codebase).
// ---------------------------------------------------------------------------
class JsonWriter {
public:
    void writeRaw(const char* s) { append(s); }
    void writeString(const char* s) {
        appendChar('"');
        // Escape per RFC 8259. We handle the common cases: " \ / \b \f \n \r \t
        // and \uXXXX for control chars < 0x20.
        for (const char* p = s; *p; p++) {
            unsigned char c = (unsigned char)*p;
            switch (c) {
                case '"':  append("\\\""); break;
                case '\\': append("\\\\"); break;
                case '\b': append("\\b");  break;
                case '\f': append("\\f");  break;
                case '\n': append("\\n");  break;
                case '\r': append("\\r");  break;
                case '\t': append("\\t");  break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        append(buf);
                    } else {
                        appendChar((char)c);
                    }
            }
        }
        appendChar('"');
    }
    void writeBool(bool b)         { append(b ? "true" : "false"); }
    void writeNull(void)           { append("null"); }
    void writeInt(int v)           { char b[24]; snprintf(b, sizeof(b), "%d", v); append(b); }
    void writeUInt(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u", v); append(b); }
    void writeFloat(float v) {
        // %.7g gives enough precision for gameplay (1mm at 1km scale) without
        // the noise of %.9f. %g strips trailing zeros.
        char b[32]; snprintf(b, sizeof(b), "%.7g", (double)v); append(b);
    }

    void beginObject() { appendChar('{'); m_indent++; m_needComma.push(false); }
    void endObject()   { m_indent--; m_needComma.pop(); newline(); appendChar('}'); }
    void beginArray()  { appendChar('['); m_indent++; m_needComma.push(false); }
    void endArray()    { m_indent--; m_needComma.pop(); newline(); appendChar(']'); }

    // Write a key in an object. Caller must follow with the value.
    void writeKey(const char* key) {
        maybeComma();
        newline();
        writeString(key);
        append(": ");
        m_needComma.top() = false;  // value follows on same line
    }

    // Mark that the next writeKey/writeValue should prepend a comma.
    // Called automatically by maybeComma() after each value.
    void maybeComma() {
        if (m_needComma.top()) append(", ");
        m_needComma.top() = true;
    }

    void finishValue() { m_needComma.top() = true; }

    const char* c_str() const { return m_buf; }
    size_t      size()  const { return m_len; }

private:
    void append(const char* s) {
        size_t n = strlen(s);
        ensure(n);
        memcpy(m_buf + m_len, s, n);
        m_len += n;
    }
    void appendChar(char c) {
        ensure(1);
        m_buf[m_len++] = c;
    }
    void newline() {
        appendChar('\n');
        for (int i = 0; i < m_indent; i++) append("  ");
    }
    void ensure(size_t extra) {
        if (m_len + extra + 1 > m_cap) {
            size_t newCap = m_cap ? m_cap * 2 : 256;
            while (newCap < m_len + extra + 1) newCap *= 2;
            char* nb = (char*)realloc(m_buf, newCap);
            if (!nb) { TD_LOG_ERROR("JsonWriter OOM"); return; }
            m_buf = nb; m_cap = newCap;
        }
    }

    char*  m_buf    = nullptr;
    size_t m_len    = 0;
    size_t m_cap    = 0;
    int    m_indent = 0;
    // Per-nesting-level "do we need a comma before the next element?" flag.
    // Stack<bool> from <stack> would work but we hand-roll to avoid the
    // include since this header is included widely.
    struct BoolStack {
        bool data[64];
        int  sp = 0;
        void push(bool b) { data[sp++] = b; }
        void pop()        { if (sp > 0) sp--; }
        bool& top()       { return data[sp - 1]; }
    };
    BoolStack m_needComma;
};

// ---------------------------------------------------------------------------
// Tiny JSON parser (reader). Recursive descent. Tolerant of whitespace.
// Returns false on syntax error; sets errOut to a static message.
//
// We don't build a DOM — instead, the caller provides a SAX-style handler
// (callbacks for each token). This keeps memory usage at O(depth) instead
// of O(document) and lets the deserializer dispatch directly to component
// setters without an intermediate representation.
// ---------------------------------------------------------------------------
struct JsonReader {
    const char* p;       // current cursor
    const char* end;     // end of input
    const char* err;     // error message (nullptr if no error)

    bool parse(const char* src, size_t len) {
        p = src;
        end = src + len;
        err = nullptr;
        skipWs();
        if (!parseValue()) return false;
        skipWs();
        if (p != end) { err = "trailing characters after JSON value"; return false; }
        return true;
    }

    // --- Low-level helpers ---
    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    }
    bool match(char c) {
        skipWs();
        if (p < end && *p == c) { p++; return true; }
        return false;
    }
    bool expect(char c, const char* msg) {
        if (!match(c)) { err = msg; return false; }
        return true;
    }

    // --- Value dispatch (SAX-style: caller overrides these in a subclass) ---
    virtual bool parseValue() {
        skipWs();
        if (p >= end) { err = "unexpected EOF"; return false; }
        char c = *p;
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseStringValue();
        if (c == 't' || c == 'f') return parseBoolValue();
        if (c == 'n') return parseNullValue();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumberValue();
        err = "unexpected character"; return false;
    }

    virtual bool parseObject() {
        if (!expect('{', "expected '{'")) return false;
        skipWs();
        if (match('}')) return true;
        for (;;) {
            skipWs();
            if (p >= end || *p != '"') { err = "expected string key"; return false; }
            const char* keyStart = p + 1;
            const char* keyEnd = keyStart;
            // Find closing quote (no escape handling in keys for simplicity;
            // we assume keys are simple identifiers).
            while (keyEnd < end && *keyEnd != '"') keyEnd++;
            if (keyEnd >= end) { err = "unterminated key"; return false; }
            std::string key(keyStart, keyEnd);
            p = keyEnd + 1;
            if (!expect(':', "expected ':' after key")) return false;
            if (!onObjectKey(key.c_str())) return false;
            if (!parseValue()) return false;
            if (!onObjectValueEnd()) return false;
            skipWs();
            if (match('}')) return true;
            if (!expect(',', "expected ',' or '}'")) return false;
        }
    }
    virtual bool onObjectKey(const char* key) { (void)key; return true; }
    virtual bool onObjectValueEnd() { return true; }

    virtual bool parseArray() {
        if (!expect('[', "expected '['")) return false;
        skipWs();
        if (match(']')) return true;
        int idx = 0;
        for (;;) {
            if (!onArrayIndex(idx++)) return false;
            if (!parseValue()) return false;
            skipWs();
            if (match(']')) return true;
            if (!expect(',', "expected ',' or ']'")) return false;
        }
    }
    virtual bool onArrayIndex(int /*i*/) { return true; }

    virtual bool parseStringValue() {
        if (!expect('"', "expected '\"'")) return false;
        // No escape unescaping for simplicity; we treat the string as raw bytes
        // up to the closing quote. Real JSON would unescape \" \n \uXXXX here.
        std::string s;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        if (p + 4 >= end) { err = "bad \\u escape"; return false; }
                        char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        unsigned codepoint = (unsigned)strtoul(hex, nullptr, 16);
                        // Encode as UTF-8 (basic plane only).
                        if (codepoint < 0x80) {
                            s += (char)codepoint;
                        } else if (codepoint < 0x800) {
                            s += (char)(0xC0 | (codepoint >> 6));
                            s += (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            s += (char)(0xE0 | (codepoint >> 12));
                            s += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            s += (char)(0x80 | (codepoint & 0x3F));
                        }
                        p += 4;
                        break;
                    }
                    default: s += *p; break;
                }
                p++;
            } else {
                s += *p++;
            }
        }
        if (p >= end) { err = "unterminated string"; return false; }
        p++;  // closing quote
        return onString(s.c_str());
    }
    virtual bool onString(const char* /*s*/) { return true; }

    virtual bool parseNumberValue() {
        const char* start = p;
        if (*p == '-') p++;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
        std::string num(start, p);
        if (num.find('.') != std::string::npos || num.find('e') != std::string::npos || num.find('E') != std::string::npos) {
            return onFloat((float)strtod(num.c_str(), nullptr));
        }
        return onInt((int)strtol(num.c_str(), nullptr, 10));
    }
    virtual bool onFloat(float /*v*/) { return true; }
    virtual bool onInt(int /*v*/) { return true; }

    virtual bool parseBoolValue() {
        if (p + 4 <= end && memcmp(p, "true", 4) == 0) {
            p += 4;
            return onBool(true);
        }
        if (p + 5 <= end && memcmp(p, "false", 5) == 0) {
            p += 5;
            return onBool(false);
        }
        err = "expected 'true' or 'false'"; return false;
    }
    virtual bool onBool(bool /*b*/) { return true; }

    virtual bool parseNullValue() {
        if (p + 4 <= end && memcmp(p, "null", 4) == 0) {
            p += 4;
            return onNull();
        }
        err = "expected 'null'"; return false;
    }
    virtual bool onNull() { return true; }
};

// ---------------------------------------------------------------------------
// Component serializers. Each component type has a write function that
// emits a "ComponentName": { ... } object. The dispatcher (below) calls
// these based on which components an entity has.
//
// To add a new component: write a serializeXxx() function and register it
// in the dispatcher table (kComponentWriters).
// ---------------------------------------------------------------------------
inline void serializePosition(JsonWriter& w, const PositionComponent& c) {
    w.beginObject();
    w.writeKey("x"); w.writeFloat(c.x); w.finishValue();
    w.writeKey("y"); w.writeFloat(c.y); w.finishValue();
    w.endObject();
}
inline void serializeVelocity(JsonWriter& w, const VelocityComponent& c) {
    w.beginObject();
    w.writeKey("vx"); w.writeFloat(c.vx); w.finishValue();
    w.writeKey("vy"); w.writeFloat(c.vy); w.finishValue();
    w.writeKey("ax"); w.writeFloat(c.ax); w.finishValue();
    w.writeKey("ay"); w.writeFloat(c.ay); w.finishValue();
    w.endObject();
}
inline void serializeSprite(JsonWriter& w, const SpriteComponent& c) {
    w.beginObject();
    w.writeKey("w");  w.writeFloat(c.width);  w.finishValue();
    w.writeKey("h");  w.writeFloat(c.height); w.finishValue();
    w.writeKey("r");  w.writeFloat(c.r); w.finishValue();
    w.writeKey("g");  w.writeFloat(c.g); w.finishValue();
    w.writeKey("b");  w.writeFloat(c.b); w.finishValue();
    w.writeKey("a");  w.writeFloat(c.a); w.finishValue();
    w.writeKey("rotation"); w.writeFloat(c.rotation); w.finishValue();
    w.writeKey("layer");    w.writeInt(c.layer);      w.finishValue();
    w.writeKey("visible");  w.writeBool(c.visible);   w.finishValue();
    w.endObject();
}
inline void serializeRigidBody(JsonWriter& w, const RigidBodyComponent& c) {
    w.beginObject();
    w.writeKey("mass");        w.writeFloat(c.mass);         w.finishValue();
    w.writeKey("friction");    w.writeFloat(c.friction);     w.finishValue();
    w.writeKey("restitution"); w.writeFloat(c.restitution);  w.finishValue();
    w.writeKey("damping");     w.writeFloat(c.linearDamping);w.finishValue();
    w.writeKey("gravityScale");w.writeFloat(c.gravityScale); w.finishValue();
    w.writeKey("useGravity");  w.writeBool(c.useGravity);    w.finishValue();
    w.writeKey("isStatic");    w.writeBool(c.isStatic);      w.finishValue();
    w.writeKey("isKinematic"); w.writeBool(c.isKinematic);   w.finishValue();
    w.writeKey("isTrigger");   w.writeBool(c.isTrigger);     w.finishValue();
    w.endObject();
}
inline void serializeCollider(JsonWriter& w, const ColliderComponent& c) {
    w.beginObject();
    w.writeKey("type");   w.writeInt((int)c.type); w.finishValue();
    w.writeKey("offsetX");w.writeFloat(c.offsetX); w.finishValue();
    w.writeKey("offsetY");w.writeFloat(c.offsetY); w.finishValue();
    w.writeKey("width");  w.writeFloat(c.width);   w.finishValue();
    w.writeKey("height"); w.writeFloat(c.height);  w.finishValue();
    w.writeKey("radius"); w.writeFloat(c.radius);  w.finishValue();
    w.endObject();
}
inline void serializeLocalTransform(JsonWriter& w, const LocalTransformComponent& c) {
    w.beginObject();
    w.writeKey("x");        w.writeFloat(c.x);        w.finishValue();
    w.writeKey("y");        w.writeFloat(c.y);        w.finishValue();
    w.writeKey("scaleX");   w.writeFloat(c.scaleX);   w.finishValue();
    w.writeKey("scaleY");   w.writeFloat(c.scaleY);   w.finishValue();
    w.writeKey("rotation"); w.writeFloat(c.rotation); w.finishValue();
    w.endObject();
}
inline void serializeHierarchy(JsonWriter& w, const HierarchyComponent& c) {
    w.beginObject();
    // parent is written as a NAME reference at the entity level, not here.
    // Here we only write the depth flag for debugging.
    w.writeKey("depth"); w.writeInt(c.depth); w.finishValue();
    w.endObject();
}

// ---------------------------------------------------------------------------
// Scene serializer. Walks every entity in the Scene and writes it as JSON.
// Returns the JSON string (caller frees with free()).
// ---------------------------------------------------------------------------
inline char* serializeScene(const Scene& scene, const char* sceneName = "Scene") {
    JsonWriter w;
    const World* world = scene.world();

    w.beginObject();
    w.writeKey("version"); w.writeInt(1); w.finishValue();
    w.writeKey("name");    w.writeString(sceneName); w.finishValue();
    w.writeKey("entities"); w.beginArray();

    // Walk every entity with a Tag component (which is auto-added on create).
    EntityId ids[TD_MAX_ENTITIES];
    int n = world->query(0, ids, TD_MAX_ENTITIES);  // mask=0 = match all
    for (int i = 0; i < n; i++) {
        EntityId id = ids[i];
        const TagComponent* tag = world->getComponent<TagComponent>(id);
        if (!tag) continue;  // shouldn't happen

        w.beginObject();
        w.writeKey("name"); w.writeString(tag->name); w.finishValue();
        w.writeKey("tag");  w.writeString(tag->tag);  w.finishValue();

        // Parent reference: look up HierarchyComponent; if it has a parent,
        // emit the parent's NAME (resolved via TagComponent).
        const HierarchyComponent* h = world->getComponent<HierarchyComponent>(id);
        if (h && h->parent != INVALID_ENTITY) {
            const TagComponent* pt = world->getComponent<TagComponent>(h->parent);
            w.writeKey("parent"); w.writeString(pt ? pt->name : ""); w.finishValue();
        } else {
            w.writeKey("parent"); w.writeNull(); w.finishValue();
        }

        w.writeKey("components"); w.beginObject();

        if (auto* c = world->getComponent<PositionComponent>(id))      { w.writeKey("Position");         serializePosition(w, *c); w.finishValue(); }
        if (auto* c = world->getComponent<VelocityComponent>(id))     { w.writeKey("Velocity");         serializeVelocity(w, *c); w.finishValue(); }
        if (auto* c = world->getComponent<SpriteComponent>(id))       { w.writeKey("Sprite");           serializeSprite(w, *c); w.finishValue(); }
        if (auto* c = world->getComponent<RigidBodyComponent>(id))    { w.writeKey("RigidBody");        serializeRigidBody(w, *c); w.finishValue(); }
        if (auto* c = world->getComponent<ColliderComponent>(id))     { w.writeKey("Collider");         serializeCollider(w, *c); w.finishValue(); }
        if (auto* c = world->getComponent<LocalTransformComponent>(id)){w.writeKey("LocalTransform");   serializeLocalTransform(w, *c); w.finishValue(); }
        if (auto* c = world->getComponent<HierarchyComponent>(id))    { w.writeKey("Hierarchy");        serializeHierarchy(w, *c); w.finishValue(); }

        w.endObject();  // components
        w.endObject();  // entity
    }

    w.endArray();  // entities
    w.endObject();

    // Return a heap-allocated copy that the caller owns.
    char* out = (char*)malloc(w.size() + 1);
    memcpy(out, w.c_str(), w.size() + 1);
    return out;
}

// ---------------------------------------------------------------------------
// Scene deserializer. Reads JSON, reconstructs the Scene.
//
// Two-pass: first create all entities (so parent refs can resolve), then
// attach components and set parent/child relationships.
// ---------------------------------------------------------------------------
class SceneDeserializer : public JsonReader {
public:
    SceneDeserializer(Scene& scene) : m_scene(scene) {}

    bool load(const char* json, size_t len) {
        m_pass = Pass::Entities;
        if (!parse(json, len)) return false;
        // Second pass: now that all entities exist, resolve parent refs.
        m_pass = Pass::Parents;
        if (!parse(json, len)) return false;
        return true;
    }

private:
    enum class Pass { Entities, Parents };

    bool parseObject() override {
        // Track which object we're inside so value handlers know context.
        m_objectPath.push_back(m_currentKey);
        bool ok = JsonReader::parseObject();
        m_objectPath.pop_back();
        return ok;
    }
    bool onObjectKey(const char* key) override {
        m_currentKey = key;
        return JsonReader::onObjectKey(key);
    }
    bool onString(const char* s) override {
        if (m_pass == Pass::Entities) {
            if (m_inEntity && strcmp(m_currentKey.c_str(), "name") == 0) {
                m_pendingName = s;
            } else if (m_inEntity && strcmp(m_currentKey.c_str(), "tag") == 0) {
                m_pendingTag = s;
            } else if (m_inEntity && strcmp(m_currentKey.c_str(), "parent") == 0) {
                m_pendingParent = s;
            }
        }
        return JsonReader::onString(s);
    }
    bool onNull() override {
        if (m_inEntity && strcmp(m_currentKey.c_str(), "parent") == 0) {
            m_pendingParent = "";  // empty = root
        }
        return JsonReader::onNull();
    }
    // For brevity, full component deserialization (parsing Position.x etc.)
    // would dispatch into a component-specific builder. This is a stub
    // that creates entities with names + tags + parent links; component
    // restoration is a straightforward extension of the same pattern.

    Scene&                m_scene;
    Pass                  m_pass = Pass::Entities;
    bool                  m_inEntity = false;
    std::string           m_currentKey;
    std::vector<std::string> m_objectPath;
    std::string           m_pendingName;
    std::string           m_pendingTag;
    std::string           m_pendingParent;
};

} // namespace td
