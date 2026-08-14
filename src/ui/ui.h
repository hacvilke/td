// =============================================================================
// TD Engine - UI Toolkit v1 (Tier 2.1)
//
// Retained-mode UI for HUDs, menus, and editor panels. Inspired by Unity's
// UI Toolkit (UXML/USS) and Godot's Control nodes.
//
// Architecture:
//   - A UICanvas owns a tree of UINodes. Each UINode has a layout (rect),
//     style (colors/fonts/margins), children, and an optional onDraw callback.
//   - Layout is computed top-down: the canvas has a fixed size (the screen),
//     each child gets a rect relative to its parent. Two layout modes:
//       * ANCHOR (pixel-anchored to parent corners — for HUDs)
//       * VBOX/HBOX (stacked children with auto-sizing — for menus)
//   - Input: the canvas hit-tests mouse position against the node tree on
//     each mouse move/click, dispatching onClick/onHover callbacks.
//   - Rendering: the canvas draws itself via SpriteBatch in a single
//     pass at the end of the frame (after world rendering).
//
// Status: SKELETON. The UINode tree + layout math is here; the actual
// rendering bridge to SpriteBatch + the editor panel for visual authoring
// are tracked as Tier 2.1 in docs/MODULARITY_ROADMAP.md.
//
// The skeleton is enough to construct a HUD programmatically (see
// web/examples/voidrunner.js for the manual approach used today; this
// toolkit replaces that with a retained-mode API).
// =============================================================================
#pragma once
#include "../ecs/world.h"
#include "../core/math/vec2.h"
#include "../core/math/vec3.h"
#include "../core/logger.h"
#include "../core/signal.h"
#include <cstdint>
#include <cstring>

namespace td {

struct UIColor { float r, g, b, a; };
struct UIRect  { float x, y, w, h; };

enum class UILayoutMode : uint8_t {
    Anchor,  // rect.x/y are absolute pixels relative to parent
    VBox,    // children stacked vertically, auto-sized to content
    HBox,    // children stacked horizontally, auto-sized to content
};

enum class UIAnchor : uint8_t {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, MiddleCenter, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    Stretch,  // fill parent
};

struct UIStyle {
    UIColor bgColor     = {0, 0, 0, 0};       // transparent
    UIColor borderColor = {1, 1, 1, 0};        // no border
    float   borderWidth  = 0;
    float   cornerRadius = 0;
    float   padding      = 0;
    float   margin       = 0;
    float   fontSize     = 14;
    UIColor textColor    = {1, 1, 1, 1};
    bool    visible      = true;
};

class UINode {
public:
    UINode() = default;
    virtual ~UINode() {
        // Children are owned by the parent.
        for (int i = 0; i < m_childCount; i++) delete m_children[i];
    }

    // --- Tree ---------------------------------------------------------------
    UINode* addChild(UINode* child) {
        if (m_childCount >= MAX_CHILDREN) {
            TD_LOG_WARN("UINode: too many children (max %d)", MAX_CHILDREN);
            delete child;
            return nullptr;
        }
        m_children[m_childCount++] = child;
        child->m_parent = this;
        m_layoutDirty = true;
        return child;
    }
    void removeAllChildren() {
        for (int i = 0; i < m_childCount; i++) delete m_children[i];
        m_childCount = 0;
        m_layoutDirty = true;
    }
    int childCount() const { return m_childCount; }
    UINode* child(int i) const { return (i >= 0 && i < m_childCount) ? m_children[i] : nullptr; }
    UINode* parent() const { return m_parent; }

    // --- Style + layout -----------------------------------------------------
    UIStyle& style() { return m_style; }
    const UIStyle& style() const { return m_style; }

    void setRect(float x, float y, float w, float h) {
        m_rect = {x, y, w, h};
        m_layoutDirty = true;
    }
    const UIRect& rect() const { return m_rect; }
    const UIRect& computedRect() const { return m_computed; }

    void setLayoutMode(UILayoutMode m) { m_layoutMode = m; m_layoutDirty = true; }
    void setAnchor(UIAnchor a) { m_anchor = a; m_layoutDirty = true; }

    // --- Text ---------------------------------------------------------------
    void setText(const char* s) {
        strncpy(m_text, s, sizeof(m_text) - 1);
        m_text[sizeof(m_text) - 1] = '\0';
        m_layoutDirty = true;
    }
    const char* text() const { return m_text; }

    // --- Interaction --------------------------------------------------------
    // onClick is invoked when the user clicks inside this node's rect.
    // Hit-testing is top-down: the topmost (last-drawn) node with a
    // matching rect receives the click.
    void setOnClick(std::function<void()> cb) { m_onClick = std::move(cb); }

    // --- Layout pass --------------------------------------------------------
    // Compute the absolute rect from the parent's rect + this node's
    // layout mode + anchor. Recursively lays out children.
    void layout(const UIRect& parentRect) {
        if (m_layoutMode == UILayoutMode::Anchor) {
            applyAnchor(parentRect);
        } else {
            // For VBox/HBox, the rect.x/y are offsets; w/h come from
            // the parent or are auto-sized (TODO: auto-size needs a
            // measureText() pass; for now we use the explicit w/h).
            applyAnchor(parentRect);
        }
        // Layout children.
        if (m_layoutMode == UILayoutMode::VBox) {
            float cursorY = m_computed.y + m_style.padding;
            for (int i = 0; i < m_childCount; i++) {
                UIRect childRect = m_children[i]->m_rect;
                childRect.x = m_computed.x + m_style.padding;
                childRect.y = cursorY;
                m_children[i]->m_computed = childRect;
                m_children[i]->layout(m_computed);
                cursorY += childRect.h + m_style.padding;
            }
        } else if (m_layoutMode == UILayoutMode::HBox) {
            float cursorX = m_computed.x + m_style.padding;
            for (int i = 0; i < m_childCount; i++) {
                UIRect childRect = m_children[i]->m_rect;
                childRect.x = cursorX;
                childRect.y = m_computed.y + m_style.padding;
                m_children[i]->m_computed = childRect;
                m_children[i]->layout(m_computed);
                cursorX += childRect.w + m_style.padding;
            }
        } else {
            for (int i = 0; i < m_childCount; i++) {
                m_children[i]->layout(m_computed);
            }
        }
        m_layoutDirty = false;
    }

    // --- Hit testing --------------------------------------------------------
    // Returns the topmost node whose rect contains (x, y), or nullptr.
    // Children are drawn on top of parents, so we test children first
    // (reverse order so the last-added wins ties).
    virtual UINode* hitTest(float x, float y) {
        if (!m_style.visible) return nullptr;
        for (int i = m_childCount - 1; i >= 0; i--) {
            UINode* hit = m_children[i]->hitTest(x, y);
            if (hit) return hit;
        }
        if (x >= m_computed.x && x <= m_computed.x + m_computed.w &&
            y >= m_computed.y && y <= m_computed.y + m_computed.h) {
            return this;
        }
        return nullptr;
    }

    // --- Render -------------------------------------------------------------
    // Override to draw this node. Default: draws a filled rect with the
    // background color, then the text. Called after all children.
    // (The actual draw call is delegated to a UIBatch interface so the
    // toolkit works with both the desktop GL renderer and the WASM
    // SpriteBatch. TODO Tier 2.1: wire this up.)
    virtual void draw(/* UIBatch& b */) const {
        // Stub. Real impl: b.fillRect(m_computed, m_style.bgColor);
        //                b.drawBorder(m_computed, m_style.borderColor, m_style.borderWidth);
        //                if (m_text[0]) b.drawText(m_text, m_computed, m_style);
    }

private:
    void applyAnchor(const UIRect& parent) {
        switch (m_anchor) {
            case UIAnchor::TopLeft:
                m_computed.x = parent.x + m_rect.x;
                m_computed.y = parent.y + m_rect.y;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::TopCenter:
                m_computed.x = parent.x + (parent.w - m_rect.w) * 0.5f;
                m_computed.y = parent.y + m_rect.y;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::TopRight:
                m_computed.x = parent.x + parent.w - m_rect.w - m_rect.x;
                m_computed.y = parent.y + m_rect.y;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::BottomLeft:
                m_computed.x = parent.x + m_rect.x;
                m_computed.y = parent.y + parent.h - m_rect.h - m_rect.y;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::BottomCenter:
                m_computed.x = parent.x + (parent.w - m_rect.w) * 0.5f;
                m_computed.y = parent.y + parent.h - m_rect.h - m_rect.y;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::BottomRight:
                m_computed.x = parent.x + parent.w - m_rect.w - m_rect.x;
                m_computed.y = parent.y + parent.h - m_rect.h - m_rect.y;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::MiddleCenter:
                m_computed.x = parent.x + (parent.w - m_rect.w) * 0.5f;
                m_computed.y = parent.y + (parent.h - m_rect.h) * 0.5f;
                m_computed.w = m_rect.w;
                m_computed.h = m_rect.h;
                break;
            case UIAnchor::Stretch:
                m_computed.x = parent.x + m_rect.x;
                m_computed.y = parent.y + m_rect.y;
                m_computed.w = parent.w - m_rect.x - m_rect.w;  // w = right margin
                m_computed.h = parent.h - m_rect.y - m_rect.h;
                break;
            default:
                m_computed = m_rect;
                break;
        }
    }

    static constexpr int MAX_CHILDREN = 32;

    UINode*       m_parent = nullptr;
    UINode*       m_children[MAX_CHILDREN] = {};
    int           m_childCount = 0;

    UIStyle       m_style;
    UIRect        m_rect      = {0, 0, 100, 30};
    UIRect        m_computed  = {0, 0, 100, 30};
    UILayoutMode  m_layoutMode = UILayoutMode::Anchor;
    UIAnchor      m_anchor     = UIAnchor::TopLeft;
    bool          m_layoutDirty = true;

    char          m_text[128] = {};

    std::function<void()> m_onClick;
};

// A UICanvas is the root of a UI tree. It owns the root UINode and provides
// the screen-sized rect that anchors the layout pass.
class UICanvas {
public:
    UICanvas() : m_root(new UINode()) {
        m_root->setLayoutMode(UILayoutMode::Anchor);
        m_root->setAnchor(UIAnchor::Stretch);
        m_root->setRect(0, 0, 0, 0);  // stretch = fill parent (screen)
    }
    ~UICanvas() { delete m_root; }

    UINode* root() { return m_root; }

    void resize(float w, float h) {
        m_screenW = w; m_screenH = h;
        m_root->setRect(0, 0, 0, 0);  // stretch fill
        UIRect screen = {0, 0, w, h};
        m_root->layout(screen);
    }

    void draw() {
        // Recursive draw would walk the tree, calling draw() on each node.
        // Stub: real impl wires into SpriteBatch.
        // m_root->drawRecursive(b);
    }

    UINode* hitTest(float x, float y) {
        return m_root->hitTest(x, y);
    }

    void handleClick(float x, float y) {
        UINode* hit = hitTest(x, y);
        if (hit) {
            // The node's onClick is private; in the real impl we'd expose
            // a public invokeOnClick() or store callbacks in a side-table.
            // For the stub, just log.
            (void)hit;
        }
    }

private:
    UINode* m_root;
    float   m_screenW = 800;
    float   m_screenH = 600;
};

} // namespace td
