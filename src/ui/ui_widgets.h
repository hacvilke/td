// =============================================================================
// TD Engine - UI Toolkit v2 (wave1-ui)
//
// Production-quality retained-mode UI system layered on top of the existing
// ui.h skeleton. This header declares the new widget classes, the rendering
// bridge (UIContext), and the top-level UI controller. The implementation
// lives in ui.cpp.
//
// Design:
//   - UIWidget is the base class. A widget tree is built by parenting
//     UIWidget* children. Parents own children (deleted in dtor).
//   - UIWidgetStyle carries a CSS-flexbox subset: flexDirection, justifyContent,
//     alignItems, flexGrow/shrink/basis, padding/margin/border (4-sided),
//     width/height/min/max, font/color with inheritance, opacity, visible,
//     pointerEvents.
//   - layout(viewportW, viewportH) runs a two-pass flexbox algorithm
//     (measure intrinsic sizes, then distribute free space along the main
//     axis with grow/shrink; align on cross axis).
//   - render(SpriteBatch&) walks the tree DFS (root first, then children)
//     and emits draw calls via UIContext. A software clip stack ensures
//     children cannot draw outside their parent's clip rect.
//   - Input: onMouseMove/Down/Up/Wheel + onKeyDown/Up/Char dispatched via
//     hit-testing. Hover/leave, focus (with Tab cycling), click (down+up on
//     same widget), drag (>4px move with button held), scroll all handled.
//
// Compatibility: the original ui.h UINode/UICanvas skeleton is left
// byte-identical and untouched. This v2 system is independent.
// =============================================================================
#pragma once

// The original skeleton header (UIColor, UIRect, UINode, UICanvas).
// Provides UIColor and UIRect, which we reuse.
#include "ui.h"
// Embedded 8x16 bitmap font (96 glyphs, ASCII 32..127).
#include "font_data.h"

#include "../renderer/sprite_batch.h"
#include "../renderer/texture.h"
#include "../core/math/mat4.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace td {

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class FlexDirection : uint8_t { Row, Column };
enum class JustifyContent : uint8_t {
    FlexStart, Center, FlexEnd, SpaceBetween, SpaceAround
};
enum class AlignItems : uint8_t { Stretch, FlexStart, Center, FlexEnd };
enum class TextAlign    : uint8_t { Left, Center, Right };
enum class WrapMode      : uint8_t { None, Word, Char };
enum class UICursor      : uint8_t { Default, Pointer, Text };

// ---------------------------------------------------------------------------
// 4-sided edges (padding/margin/border)
// ---------------------------------------------------------------------------
struct UIEdges {
    float left   = 0;
    float top    = 0;
    float right  = 0;
    float bottom = 0;
    UIEdges() = default;
    UIEdges(float all) : left(all), top(all), right(all), bottom(all) {}
    UIEdges(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
    float horizontal() const { return left + right; }
    float vertical()   const { return top + bottom; }
};

// ---------------------------------------------------------------------------
// Style (CSS-flexbox subset)
// ---------------------------------------------------------------------------
struct UIWidgetStyle {
    // Background / border
    UIColor backgroundColor = {0, 0, 0, 0};
    UIColor borderColor     = {1, 1, 1, 0};
    float   borderWidth      = 0;
    float   borderRadius     = 0;

    // Spacing
    UIEdges padding;
    UIEdges margin;
    UIEdges border;          // future: per-side border widths (currently we use borderWidth)

    // Font / text
    float      fontSize    = 14;
    UIColor    textColor   = {1, 1, 1, 1};
    TextAlign  textAlign   = TextAlign::Left;
    WrapMode   wrap        = WrapMode::None;
    bool       inheritTextColor = true;
    bool       inheritFontSize  = true;

    // Flexbox
    FlexDirection    flexDirection = FlexDirection::Row;
    JustifyContent   justifyContent = JustifyContent::FlexStart;
    AlignItems       alignItems     = AlignItems::Stretch;
    float            flexGrow       = 0;
    float            flexShrink     = 1;
    float            flexBasis      = -1;   // -1 = auto

    // Sizing (-1 = auto)
    float width     = -1;
    float height    = -1;
    float minWidth  = 0;
    float maxWidth  = 1e30f;
    float minHeight = 0;
    float maxHeight = 1e30f;

    // Visibility / interaction
    float opacity        = 1;
    bool  visible        = true;
    bool  pointerEvents  = true;
    UICursor cursor      = UICursor::Default;
    int   tabIndex       = 0;   // 0 = not tabbable; > 0 = tabbable in ascending order
};

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
class UIWidget;
class UIContext;
class UI;

using UICanvasDrawCallback = std::function<void(UIContext&, const UIRect&)>;

// ---------------------------------------------------------------------------
// UIContext - rendering bridge to SpriteBatch
// ---------------------------------------------------------------------------
//   Provides high-level draw primitives (drawRect/drawText/drawImage) that
//   respect a software clip stack. Clips are intersected with the current
//   clip rect; draw calls whose rect is fully outside the clip are dropped,
//   and partial overlaps are scissored by shrinking the drawn rect.
class UIContext {
public:
    UIContext() = default;
    ~UIContext();

    // Initialize font atlas (call after SpriteBatch::init() so GL is ready).
    // Safe to call multiple times; the atlas is built once.
    void init(SpriteBatch* batch);
    void shutdown();

    // Set the active batch (call between batch->begin() and batch->end()).
    void setBatch(SpriteBatch* batch) { m_batch = batch; }
    SpriteBatch* batch() { return m_batch; }

    // Clip stack
    void   pushClip(const UIRect& r);
    void   popClip();
    UIRect currentClip() const;
    int    clipDepth() const { return (int)m_clipStack.size(); }

    // Primitives (all clip-aware)
    void drawRect(const UIRect& r, const UIColor& color);
    void drawRectBorder(const UIRect& r, const UIColor& color, float thickness);
    void drawRoundedRect(const UIRect& r, const UIColor& color, float radius);
    void drawText(const char* text, float x, float y, float size, const UIColor& color);
    void drawTextAligned(const char* text, const UIRect& bounds, float size,
                         const UIColor& color, TextAlign align);
    void drawImage(const Texture* tex, const UIRect& r, const UIColor& tint);
    void drawImage9Slice(const Texture* tex, const UIRect& r, float slice,
                         const UIColor& tint);

    // Measure text width/height at a given font size (no GL required).
    float measureTextWidth(const char* text, float size) const;
    float measureTextHeight(float size) const;

    // Direct access to the font atlas texture (for advanced use).
    const Texture* fontTexture() const { return m_fontTexture; }
    bool fontAtlasReady() const { return m_fontTexture != nullptr; }

    // Public clip query: returns true if any portion of `r` is visible
    // against the current clip stack, and writes the visible sub-rect to
    // *outVisible. Useful for tests and for widgets that want to skip
    // drawing entirely when fully clipped.
    bool clipRect(const UIRect& r, UIRect* outVisible) const;
    bool isRectVisible(const UIRect& r) const {
        UIRect dummy;
        return clipRect(r, &dummy);
    }

private:
    SpriteBatch*           m_batch = nullptr;
    Texture*               m_fontTexture = nullptr;
    std::vector<UIRect>    m_clipStack;

    void buildFontAtlas();
};

// ---------------------------------------------------------------------------
// UIWidget - base class
// ---------------------------------------------------------------------------
class UIWidget {
public:
    UIWidget() = default;
    virtual ~UIWidget();

    // Tree (parent owns children)
    UIWidget* addChild(UIWidget* child);
    void      removeChild(UIWidget* child);     // also deletes the child
    void      removeAllChildren();              // deletes all children
    int       childCount() const { return (int)m_children.size(); }
    UIWidget* child(int i) const {
        return (i >= 0 && i < (int)m_children.size()) ? m_children[(size_t)i] : nullptr;
    }
    UIWidget* parent() const { return m_parent; }
    const std::vector<UIWidget*>& children() const { return m_children; }

    // Style
    UIWidgetStyle& style() { return m_style; }
    const UIWidgetStyle& style() const { return m_style; }

    // Convenience setters that flip the inheritance flag.
    void setTextColor(const UIColor& c) {
        m_style.textColor = c; m_style.inheritTextColor = false; m_dirty = true;
    }
    void setFontSize(float s) {
        m_style.fontSize = s; m_style.inheritFontSize = false; m_dirty = true;
    }
    void setWidth(float w)     { m_style.width = w; m_dirty = true; }
    void setHeight(float h)    { m_style.height = h; m_dirty = true; }
    void setVisible(bool v)    { m_style.visible = v; m_dirty = true; }
    void setBackgroundColor(const UIColor& c) { m_style.backgroundColor = c; m_dirty = true; }
    void setPadding(const UIEdges& e)         { m_style.padding = e; m_dirty = true; }
    void setMargin(const UIEdges& e)          { m_style.margin = e; m_dirty = true; }
    void setBorder(const UIColor& c, float w) {
        m_style.borderColor = c; m_style.borderWidth = w; m_dirty = true;
    }
    void setBorderRadius(float r) { m_style.borderRadius = r; m_dirty = true; }
    void setFlexGrow(float g)     { m_style.flexGrow = g; m_dirty = true; }
    void setFlexShrink(float s)   { m_style.flexShrink = s; m_dirty = true; }
    void setFlexBasis(float b)    { m_style.flexBasis = b; m_dirty = true; }
    void setTabIndex(int i)       { m_style.tabIndex = i; }

    // Computed rect (filled in by layout()).
    const UIRect& rect() const { return m_rect; }
    float x() const { return m_rect.x; }
    float y() const { return m_rect.y; }
    float w() const { return m_rect.w; }
    float h() const { return m_rect.h; }

    // Layout dirty flag
    void setDirty(bool dirty = true) { m_dirty = dirty; }
    bool isDirty() const { return m_dirty; }

    // --- Layout pass (two-pass: measure then arrange) -------------------
    // measure(availW, availH, outW, outH): compute intrinsic size given
    // an available size constraint. Default impl handles flexbox measure
    // for containers; leaves return their style size or content size.
    virtual void measure(float availW, float availH, float* outW, float* outH);

    // arrange(rect): place this widget at rect and lay out children.
    virtual void arrange(const UIRect& rect);

    // --- Rendering ------------------------------------------------------
    // paint(ctx) draws THIS widget only (children handled by UI::render).
    virtual void paint(UIContext& ctx) const;

    // --- Hit testing ----------------------------------------------------
    // Returns the topmost widget at (x, y), or nullptr.
    virtual UIWidget* hitTest(float x, float y);

    // --- Input handlers (overridable; default = unhandled) --------------
    virtual bool onMouseMove(float, float) { return false; }
    virtual bool onMouseDown(float, float, int) { return false; }
    virtual bool onMouseUp(float, float, int) { return false; }
    virtual bool onClick(float, float, int) { return false; }
    virtual bool onDragStart(float, float) { return false; }
    virtual bool onDrag(float, float, float, float) { return false; }
    virtual bool onDragEnd(float, float) { return false; }
    virtual bool onMouseWheel(float, float, float) { return false; }
    virtual bool onKeyDown(int) { return false; }
    virtual bool onKeyUp(int) { return false; }
    virtual bool onChar(int) { return false; }
    virtual void onEnter() {}
    virtual void onLeave() {}
    virtual void onFocus() {}
    virtual void onBlur() {}

    // Resolved style properties (walk up the tree for inherited values).
    UIColor resolvedTextColor() const;
    float   resolvedFontSize() const;

    // Geometry helpers
    bool contains(float px, float py) const {
        return px >= m_rect.x && px <= m_rect.x + m_rect.w &&
               py >= m_rect.y && py <= m_rect.y + m_rect.h;
    }

protected:
    UIWidget*                 m_parent = nullptr;
    std::vector<UIWidget*>    m_children;
    UIWidgetStyle             m_style;
    UIRect                    m_rect = {0, 0, 0, 0};
    bool                      m_dirty = true;

    friend class UI;
};

// ---------------------------------------------------------------------------
// Container - flexbox parent
// ---------------------------------------------------------------------------
class Container : public UIWidget {
public:
    Container() {
        m_style.flexDirection = FlexDirection::Column;
        m_style.flexGrow = 1;
        m_style.alignItems = AlignItems::Stretch;
    }
    void setDirection(FlexDirection d) {
        m_style.flexDirection = d; m_dirty = true;
    }
    void setJustify(JustifyContent j) {
        m_style.justifyContent = j; m_dirty = true;
    }
    void setAlign(AlignItems a) {
        m_style.alignItems = a; m_dirty = true;
    }
};

// ---------------------------------------------------------------------------
// Label - text with optional wrap and alignment
// ---------------------------------------------------------------------------
class Label : public UIWidget {
public:
    Label() { m_style.height = 20; m_style.flexShrink = 1; }
    void setText(const char* s);
    const char* text() const { return m_text.c_str(); }
    void setAlign(TextAlign a) { m_style.textAlign = a; m_dirty = true; }
    void setWrap(WrapMode w)   { m_style.wrap = w; m_dirty = true; }

    void measure(float availW, float availH, float* outW, float* outH) override;
    void paint(UIContext& ctx) const override;
private:
    std::string m_text;
};

// ---------------------------------------------------------------------------
// Button - clickable, with hover/pressed/disabled states
// ---------------------------------------------------------------------------
class Button : public UIWidget {
public:
    Button();
    void setText(const char* s);
    const char* text() const { return m_text.c_str(); }
    void setOnClick(std::function<void()> cb) { m_onClick = std::move(cb); }
    void setDisabled(bool d) { m_disabled = d; m_dirty = true; }
    bool isDisabled() const { return m_disabled; }
    bool isHovered() const { return m_hovered; }
    bool isPressed() const { return m_pressed; }

    bool onMouseDown(float x, float y, int button) override;
    bool onMouseUp(float x, float y, int button) override;
    bool onClick(float x, float y, int button) override;
    void onEnter() override { m_hovered = true; }
    void onLeave() override { m_hovered = false; m_pressed = false; }
    void paint(UIContext& ctx) const override;
private:
    std::string              m_text;
    std::function<void()>    m_onClick;
    bool                     m_pressed  = false;
    bool                     m_hovered  = false;
    bool                     m_disabled = false;
};

// ---------------------------------------------------------------------------
// Image - stretch or 9-slice a Texture
// ---------------------------------------------------------------------------
class UIImage : public UIWidget {
public:
    UIImage() : m_texture(nullptr), m_slice(0) {}
    void setTexture(const Texture* t) { m_texture = t; m_dirty = true; }
    void setTint(const UIColor& c) { m_tint = c; m_dirty = true; }
    void set9Slice(float s) { m_slice = s; m_dirty = true; }

    void paint(UIContext& ctx) const override;
private:
    const Texture* m_texture;
    UIColor        m_tint = {1, 1, 1, 1};
    float          m_slice;   // 0 = stretch; >0 = 9-slice inset
};

// ---------------------------------------------------------------------------
// Slider - drag handle, min/max/value/step
// ---------------------------------------------------------------------------
class Slider : public UIWidget {
public:
    Slider();
    void setRange(float minV, float maxV) { m_min = minV; m_max = maxV; setValue(m_value); }
    void setValue(float v);
    void setStep(float s) { m_step = s; }
    void setOnChange(std::function<void(float)> cb) { m_onChange = std::move(cb); }
    float value() const { return m_value; }
    float min() const { return m_min; }
    float max() const { return m_max; }

    bool onMouseDown(float x, float y, int button) override;
    bool onMouseUp(float x, float y, int button) override;
    bool onDrag(float x, float y, float dx, float dy) override;
    void paint(UIContext& ctx) const override;
private:
    float m_min = 0, m_max = 1, m_value = 0, m_step = 0;
    std::function<void(float)> m_onChange;
    bool m_dragging = false;

    float valueFromPos(float x) const;
    float posFromValue(float v) const;
};

// ---------------------------------------------------------------------------
// Checkbox - boolean state
// ---------------------------------------------------------------------------
class Checkbox : public UIWidget {
public:
    Checkbox();
    void setChecked(bool c);
    bool checked() const { return m_checked; }
    void setOnChange(std::function<void(bool)> cb) { m_onChange = std::move(cb); }
    void setLabel(const char* s);

    bool onMouseDown(float x, float y, int button) override;
    bool onClick(float x, float y, int button) override;
    void paint(UIContext& ctx) const override;
private:
    bool                       m_checked = false;
    std::string                m_label;
    std::function<void(bool)>  m_onChange;
};

// ---------------------------------------------------------------------------
// TextInput - single-line text with caret, selection, clipboard
// ---------------------------------------------------------------------------
class TextInput : public UIWidget {
public:
    TextInput();
    void setText(const char* s);
    const char* text() const { return m_text.c_str(); }
    void setPlaceholder(const char* s) { m_placeholder = s; }
    void setMaxLength(int n) { m_maxLength = n; }
    void setOnChange(std::function<void(const char*)> cb) { m_onChange = std::move(cb); }
    void setOnSubmit(std::function<void(const char*)> cb) { m_onSubmit = std::move(cb); }

    bool onKeyDown(int key) override;
    bool onChar(int ch) override;
    void onFocus() override { m_focused = true; }
    void onBlur() override  { m_focused = false; }
    void paint(UIContext& ctx) const override;
private:
    std::string m_text;
    std::string m_placeholder;
    int         m_maxLength = 1024;
    int         m_caret = 0;       // character index
    int         m_selStart = -1;   // -1 = no selection
    bool        m_focused = false;
    std::function<void(const char*)> m_onChange;
    std::function<void(const char*)> m_onSubmit;

    void deleteSelection();
    void insertAtCaret(const char* utf8);
};

// ---------------------------------------------------------------------------
// ScrollView - viewport with vertical scroll, mouse wheel + drag
// ---------------------------------------------------------------------------
class ScrollView : public UIWidget {
public:
    ScrollView();
    void setScrollTop(float s);
    float scrollTop() const { return m_scrollTop; }
    float scrollContentHeight() const { return m_contentHeight; }
    float viewportHeight() const { return m_rect.h; }

    // Returns true if child `i` is currently visible in the viewport.
    bool isChildVisible(int i) const;

    bool onMouseWheel(float x, float y, float delta) override;
    bool onMouseDown(float x, float y, int button) override;
    bool onMouseUp(float x, float y, int button) override;
    bool onDrag(float x, float y, float dx, float dy) override;
    void arrange(const UIRect& rect) override;
    void paint(UIContext& ctx) const override;
private:
    float m_scrollTop = 0;
    float m_contentHeight = 0;
    bool  m_dragScroll = false;
    float m_dragLastY = 0;
};

// ---------------------------------------------------------------------------
// ListView - virtualized list of items
// ---------------------------------------------------------------------------
// Builds only the visible rows. Handles 100K+ items by laying out only the
// visible window + a small overscan buffer.
class ListView : public UIWidget {
public:
    using ItemBuilder = std::function<void(UIWidget* host, int index)>;

    ListView();
    void setItemCount(int n) { m_itemCount = n; m_dirty = true; }
    int  itemCount() const { return m_itemCount; }
    void setItemHeight(float h) { m_itemHeight = h; m_dirty = true; }
    void setItemBuilder(ItemBuilder cb) { m_builder = std::move(cb); m_dirty = true; }
    void setScrollTop(float s);
    float scrollTop() const { return m_scrollTop; }
    int  firstVisibleIndex() const;
    int  lastVisibleIndex() const;

    bool onMouseWheel(float x, float y, float delta) override;
    void measure(float availW, float availH, float* outW, float* outH) override;
    void arrange(const UIRect& rect) override;
    void paint(UIContext& ctx) const override;
private:
    int           m_itemCount = 0;
    float         m_itemHeight = 24;
    float         m_scrollTop = 0;
    float         m_maxScroll = 0;
    ItemBuilder   m_builder;
    // Pool of host widgets (typically just the visible window). We reuse
    // the same set of host widgets and re-bind their content per visible
    // index in arrange().
    std::vector<UIWidget*> m_pool;
    int                    m_lastFirstVisible = -1;

    void rebuildPool(int visibleCount);
};

// ---------------------------------------------------------------------------
// Dropdown - click to expand options panel
// ---------------------------------------------------------------------------
class Dropdown : public UIWidget {
public:
    Dropdown();
    void setOptions(const std::vector<std::string>& opts);
    void setSelected(int idx);
    int  selected() const { return m_selected; }
    const char* selectedText() const;
    void setOnChange(std::function<void(int)> cb) { m_onChange = std::move(cb); }

    bool onMouseDown(float x, float y, int button) override;
    bool onClick(float x, float y, int button) override;
    void paint(UIContext& ctx) const override;
    void arrange(const UIRect& rect) override;

    // Called by the UI controller when the dropdown is expanded and the
    // user clicks outside it (or on an option).
    void closeMenu();
private:
    std::vector<std::string> m_options;
    int                      m_selected = -1;
    bool                     m_expanded = false;
    std::function<void(int)> m_onChange;
};

// ---------------------------------------------------------------------------
// Modal - overlay + centered card, blocks input to underlay
// ---------------------------------------------------------------------------
class Modal : public UIWidget {
public:
    Modal();
    // The first child added becomes the card content. The overlay rect
    // covers the parent (typically the screen). Card is centered.
    void setCardSize(float w, float h) { m_cardW = w; m_cardH = h; m_dirty = true; }
    void setShowOverlay(bool s) { m_showOverlay = s; m_dirty = true; }

    void arrange(const UIRect& rect) override;
    void paint(UIContext& ctx) const override;
    // Modal absorbs all input that hits it (so the underlay is blocked).
    bool onMouseDown(float, float, int) override { return true; }
    bool onMouseUp(float, float, int) override   { return true; }
    bool onMouseMove(float, float) override      { return true; }
private:
    float m_cardW = 400;
    float m_cardH = 300;
    bool  m_showOverlay = true;
};

// ---------------------------------------------------------------------------
// Tooltip - appears on hover after delay, fades in
// ---------------------------------------------------------------------------
class Tooltip : public UIWidget {
public:
    Tooltip();
    void setText(const char* s) { m_text = s; }
    void setDelay(float seconds) { m_delay = seconds; }

    // Driven by the UI controller: each frame, the controller calls
    // tick(dt, mouseInside) so the tooltip can fade in/out.
    void tick(float dt, bool mouseInside);
    void paint(UIContext& ctx) const override;
private:
    std::string m_text;
    float       m_delay = 0.5f;
    float       m_timer = 0;
    float       m_alpha = 0;
};

// ---------------------------------------------------------------------------
// Canvas - custom draw callback
// ---------------------------------------------------------------------------
class UICanvasWidget : public UIWidget {
public:
    UICanvasWidget() = default;
    void setDrawCallback(UICanvasDrawCallback cb) { m_cb = std::move(cb); }

    void paint(UIContext& ctx) const override;
private:
    UICanvasDrawCallback m_cb;
};

// ---------------------------------------------------------------------------
// UI - top-level controller
// ---------------------------------------------------------------------------
class UI {
public:
    UI();
    ~UI();

    void setRoot(UIWidget* root);
    UIWidget* root() const { return m_root; }

    // Layout: walks the tree and computes final rects. Cached unless any
    // widget is dirty.
    void layout(float viewportW, float viewportH);

    // Render: walks the tree DFS and emits draw calls via the context.
    // projection is the ortho matrix for the UI pass (typically screen-space).
    void render(SpriteBatch& batch, const Mat4& projection);

    // Input dispatch
    void onMouseMove(float x, float y);
    void onMouseDown(float x, float y, int button);
    void onMouseUp(float x, float y, int button);
    void onMouseWheel(float x, float y, float delta);
    void onKeyDown(int key, bool shift = false, bool ctrl = false);
    void onKeyUp(int key);
    void onChar(int ch);

    // Per-frame tick (for tooltips, animations, etc.)
    void tick(float dt);

    UIWidget* hovered() const { return m_hovered; }
    UIWidget* focused() const { return m_focused; }
    void      setFocus(UIWidget* w);

    // For tests / advanced use.
    UIContext& context() { return m_context; }

    // Key codes (kept simple — match SDL/GLFW common values where possible).
    enum Keys : int {
        Key_Backspace = 8,
        Key_Tab       = 9,
        Key_Enter     = 13,
        Key_Escape    = 27,
        Key_Delete    = 127,
        Key_Left      = 0x11000,
        Key_Right,
        Key_Up,
        Key_Down,
        Key_Home,
        Key_End,
    };

private:
    UIWidget*   m_root = nullptr;
    UIContext   m_context;
    UIWidget*   m_hovered = nullptr;
    UIWidget*   m_focused = nullptr;
    UIWidget*   m_mouseDownWidget = nullptr;
    int         m_mouseDownButton = -1;
    float       m_mouseDownX = 0;
    float       m_mouseDownY = 0;
    float       m_lastMouseX = 0;
    float       m_lastMouseY = 0;
    bool        m_dragStarted = false;
    float       m_viewportW = 0;
    float       m_viewportH = 0;

    void drawRecursive(UIWidget* w);
    UIWidget* hitTestRecursive(UIWidget* w, float x, float y);
    void collectTabOrder(UIWidget* w, std::vector<UIWidget*>& out);
    void markAncestorsDirty(UIWidget* w);
};

} // namespace td
