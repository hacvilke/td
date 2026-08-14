// =============================================================================
// TD Engine - UI Toolkit v2 implementation (wave1-ui)
//
// Implements everything declared in ui_widgets.h: rendering bridge (UIContext),
// two-pass flexbox layout, hit-testing + input dispatch with hover/focus/drag,
// and the standard widget library (Container, Label, Button, Image, Slider,
// Checkbox, TextInput, ScrollView, ListView, Dropdown, Modal, Tooltip, Canvas).
//
// The original ui.h UINode/UICanvas skeleton is untouched.
// =============================================================================
#include "ui_widgets.h"
#include "../core/logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace td {

// ===========================================================================
// Helpers
// ===========================================================================
namespace {

inline UIColor clipColor(const UIColor& c, float alpha) {
    UIColor r = c;
    r.a *= alpha;
    return r;
}

inline UIRect intersectRect(const UIRect& a, const UIRect& b) {
    float x0 = std::max(a.x, b.x);
    float y0 = std::max(a.y, b.y);
    float x1 = std::min(a.x + a.w, b.x + b.w);
    float y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    UIRect r;
    r.x = x0; r.y = y0; r.w = x1 - x0; r.h = y1 - y0;
    return r;
}

inline bool rectVisible(const UIRect& r) {
    return r.w > 0.001f && r.h > 0.001f;
}

inline bool rectsOverlap(const UIRect& a, const UIRect& b) {
    return !(a.x + a.w <= b.x || b.x + b.w <= a.x ||
             a.y + a.h <= b.y || b.y + b.h <= a.y);
}

} // namespace

// ===========================================================================
// UIContext
// ===========================================================================
UIContext::~UIContext() {
    shutdown();
}

void UIContext::init(SpriteBatch* /*batch*/) {
    if (m_fontTexture) return;  // already built
    buildFontAtlas();
}

void UIContext::shutdown() {
    if (m_fontTexture) {
        m_fontTexture->destroy();
        delete m_fontTexture;
        m_fontTexture = nullptr;
    }
    m_clipStack.clear();
    m_batch = nullptr;
}

void UIContext::buildFontAtlas() {
    // Build a 96-glyph atlas laid out as 12 columns x 8 rows of 8x16 cells.
    // Atlas width  = 12 * 8  = 96 px
    // Atlas height = 8  * 16 = 128 px
    constexpr int ATLAS_COLS = 12;
    constexpr int ATLAS_ROWS = 8;
    constexpr int ATLAS_W = ATLAS_COLS * UI_FONT_GLYPH_W;
    constexpr int ATLAS_H = ATLAS_ROWS * UI_FONT_GLYPH_H;
    unsigned char pixels[(size_t)ATLAS_W * ATLAS_H * 4] = {0};
    for (int g = 0; g < UI_FONT_GLYPH_COUNT; g++) {
        int col = g % ATLAS_COLS;
        int row = g / ATLAS_COLS;
        const unsigned char* glyph = &UI_FONT_DATA[g * 16];
        for (int y = 0; y < UI_FONT_GLYPH_H; y++) {
            unsigned char rowBits = glyph[y];
            for (int x = 0; x < UI_FONT_GLYPH_W; x++) {
                if (rowBits & (0x80 >> x)) {
                    int px = col * UI_FONT_GLYPH_W + x;
                    int py = row * UI_FONT_GLYPH_H + y;
                    size_t idx = ((size_t)py * ATLAS_W + px) * 4;
                    pixels[idx + 0] = 255;
                    pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255;
                    pixels[idx + 3] = 255;
                }
            }
        }
    }
    m_fontTexture = new Texture();
    TextureConfig cfg;
    cfg.width  = ATLAS_W;
    cfg.height = ATLAS_H;
    cfg.channels = 4;
    cfg.minFilter = TextureFilter::Linear;
    cfg.magFilter = TextureFilter::Linear;
    cfg.wrapS = TextureWrap::ClampToEdge;
    cfg.wrapT = TextureWrap::ClampToEdge;
    if (!m_fontTexture->create(cfg, pixels)) {
        TD_LOG_WARN("UIContext: failed to build font atlas texture");
        delete m_fontTexture;
        m_fontTexture = nullptr;
    }
}

void UIContext::pushClip(const UIRect& r) {
    if (m_clipStack.empty()) {
        m_clipStack.push_back(r);
    } else {
        m_clipStack.push_back(intersectRect(m_clipStack.back(), r));
    }
}

void UIContext::popClip() {
    if (!m_clipStack.empty()) m_clipStack.pop_back();
}

UIRect UIContext::currentClip() const {
    if (m_clipStack.empty()) {
        // "Infinite" clip — everything is visible.
        UIRect r;
        r.x = -1e30f; r.y = -1e30f; r.w = 2e30f; r.h = 2e30f;
        return r;
    }
    return m_clipStack.back();
}

bool UIContext::clipRect(const UIRect& r, UIRect* outVisible) const {
    if (m_clipStack.empty()) {
        *outVisible = r;
        return rectVisible(r);
    }
    UIRect c = intersectRect(m_clipStack.back(), r);
    *outVisible = c;
    return rectVisible(c);
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------
void UIContext::drawRect(const UIRect& r, const UIColor& color) {
    if (!m_batch || color.a <= 0.001f) return;
    UIRect vis;
    if (!clipRect(r, &vis)) return;
    m_batch->drawQuad(vis.x, vis.y, vis.w, vis.h,
                      color.r, color.g, color.b, color.a, nullptr);
}

void UIContext::drawRectBorder(const UIRect& r, const UIColor& color, float thickness) {
    if (!m_batch || color.a <= 0.001f || thickness <= 0.001f) return;
    // Four thin rects (top, bottom, left, right).
    UIRect top    = { r.x, r.y, r.w, thickness };
    UIRect bottom = { r.x, r.y + r.h - thickness, r.w, thickness };
    UIRect left   = { r.x, r.y, thickness, r.h };
    UIRect right  = { r.x + r.w - thickness, r.y, thickness, r.h };
    drawRect(top, color);
    drawRect(bottom, color);
    drawRect(left, color);
    drawRect(right, color);
}

void UIContext::drawRoundedRect(const UIRect& r, const UIColor& color, float radius) {
    // Approximate rounded corners by drawing the central rect + four small
    // corner quads (no true SDF — keeps us within the SpriteBatch quad API).
    if (!m_batch || color.a <= 0.001f) return;
    if (radius < 1.0f) {
        drawRect(r, color);
        return;
    }
    float rad = std::min(radius, std::min(r.w, r.h) * 0.5f);
    UIRect center = { r.x + rad, r.y, r.w - 2 * rad, r.h };
    UIRect left   = { r.x, r.y + rad, rad, r.h - 2 * rad };
    UIRect right  = { r.x + r.w - rad, r.y + rad, rad, r.h - 2 * rad };
    drawRect(center, color);
    drawRect(left, color);
    drawRect(right, color);
    // Corners (small filled squares — visually close enough at small radii).
    UIRect tl = { r.x, r.y + rad * 0.5f, rad, rad };
    UIRect tr = { r.x + r.w - rad, r.y + rad * 0.5f, rad, rad };
    UIRect bl = { r.x, r.y + r.h - rad * 1.5f, rad, rad };
    UIRect br = { r.x + r.w - rad, r.y + r.h - rad * 1.5f, rad, rad };
    drawRect(tl, color);
    drawRect(tr, color);
    drawRect(bl, color);
    drawRect(br, color);
}

float UIContext::measureTextWidth(const char* text, float size) const {
    if (!text) return 0;
    float scale = size / (float)UI_FONT_GLYPH_H;
    float w = 0;
    for (const char* p = text; *p; ++p) {
        // Treat each char as one glyph cell wide.
        w += (float)UI_FONT_GLYPH_W * scale;
    }
    return w;
}

float UIContext::measureTextHeight(float size) const {
    return size;
}

void UIContext::drawText(const char* text, float x, float y, float size,
                         const UIColor& color) {
    if (!m_batch || !text || color.a <= 0.001f) return;
    if (!m_fontTexture) {
        // No GL yet (e.g. running in unit tests) — silently skip.
        return;
    }
    float scale = size / (float)UI_FONT_GLYPH_H;
    float glyphW = (float)UI_FONT_GLYPH_W * scale;
    float glyphH = (float)UI_FONT_GLYPH_H * scale;

    constexpr int ATLAS_COLS = 12;
    constexpr int ATLAS_ROWS = 8;
    constexpr float ATLAS_W = (float)(ATLAS_COLS * UI_FONT_GLYPH_W);
    constexpr float ATLAS_H = (float)(ATLAS_ROWS * UI_FONT_GLYPH_H);

    float cursorX = x;
    for (const char* p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        int glyphIdx = ch - UI_FONT_FIRST_ASCII;
        if (glyphIdx < 0 || glyphIdx >= UI_FONT_GLYPH_COUNT) {
            cursorX += glyphW;
            continue;
        }
        int col = glyphIdx % ATLAS_COLS;
        int row = glyphIdx / ATLAS_COLS;
        float u0 = (col * UI_FONT_GLYPH_W)        / ATLAS_W;
        float u1 = ((col + 1) * UI_FONT_GLYPH_W)  / ATLAS_W;
        float v0 = (row * UI_FONT_GLYPH_H)        / ATLAS_H;
        float v1 = ((row + 1) * UI_FONT_GLYPH_H)  / ATLAS_H;

        // Clip per-glyph so partial characters at the clip edge don't escape.
        UIRect glyphRect = { cursorX, y, glyphW, glyphH };
        UIRect vis;
        if (!clipRect(glyphRect, &vis)) {
            cursorX += glyphW;
            continue;
        }
        if (vis.w < glyphW || vis.h < glyphH) {
            // Partial clip — adjust UVs to draw only the visible portion.
            float fracX0 = (vis.x - cursorX) / glyphW;
            float fracX1 = (vis.x + vis.w - cursorX) / glyphW;
            float fracY0 = (vis.y - y) / glyphH;
            float fracY1 = (vis.y + vis.h - y) / glyphH;
            float nu0 = u0 + (u1 - u0) * fracX0;
            float nu1 = u0 + (u1 - u0) * fracX1;
            float nv0 = v0 + (v1 - v0) * fracY0;
            float nv1 = v0 + (v1 - v0) * fracY1;
            SpriteData s;
            s.x = vis.x; s.y = vis.y; s.width = vis.w; s.height = vis.h;
            s.u0 = nu0; s.v0 = nv0; s.u1 = nu1; s.v1 = nv1;
            s.r = color.r; s.g = color.g; s.b = color.b; s.a = color.a;
            s.originX = 0; s.originY = 0;
            m_batch->draw(s, m_fontTexture);
        } else {
            SpriteData s;
            s.x = cursorX; s.y = y; s.width = glyphW; s.height = glyphH;
            s.u0 = u0; s.v0 = v0; s.u1 = u1; s.v1 = v1;
            s.r = color.r; s.g = color.g; s.b = color.b; s.a = color.a;
            s.originX = 0; s.originY = 0;
            m_batch->draw(s, m_fontTexture);
        }
        cursorX += glyphW;
    }
}

void UIContext::drawTextAligned(const char* text, const UIRect& bounds, float size,
                                const UIColor& color, TextAlign align) {
    float textW = measureTextWidth(text, size);
    float x = bounds.x;
    if (align == TextAlign::Center) {
        x = bounds.x + (bounds.w - textW) * 0.5f;
    } else if (align == TextAlign::Right) {
        x = bounds.x + bounds.w - textW;
    }
    float y = bounds.y + (bounds.h - size) * 0.5f;
    drawText(text, x, y, size, color);
}

void UIContext::drawImage(const Texture* tex, const UIRect& r, const UIColor& tint) {
    if (!m_batch || !tex || tint.a <= 0.001f) return;
    UIRect vis;
    if (!clipRect(r, &vis)) return;
    // For simplicity, do not UV-crop partial clips for images — just draw
    // the visible sub-rect with full UVs (which will look stretched at the
    // edge). Acceptable for HUD usage.
    SpriteData s;
    s.x = vis.x; s.y = vis.y; s.width = vis.w; s.height = vis.h;
    s.u0 = 0; s.v0 = 0; s.u1 = 1; s.v1 = 1;
    s.r = tint.r; s.g = tint.g; s.b = tint.b; s.a = tint.a;
    s.originX = 0; s.originY = 0;
    m_batch->draw(s, tex);
}

void UIContext::drawImage9Slice(const Texture* tex, const UIRect& r, float slice,
                                const UIColor& tint) {
    if (!m_batch || !tex || tint.a <= 0.001f) return;
    if (slice <= 0.001f) {
        drawImage(tex, r, tint);
        return;
    }
    // Nine-slice: 4 corners (slice x slice), 4 edges, 1 center.
    // UVs assume the texture has equal-margin slice borders.
    float iw = (float)tex->getWidth();
    float ih = (float)tex->getHeight();
    if (iw <= 0 || ih <= 0) return;
    float su0 = slice / iw;
    float su1 = 1.0f - slice / iw;
    float sv0 = slice / ih;
    float sv1 = 1.0f - slice / ih;

    // We won't bother clipping each slice perfectly; rely on the caller
    // having set up a clip if needed.
    auto drawSlice = [&](float dx, float dy, float dw, float dh,
                         float u0, float v0, float u1, float v1) {
        if (dw <= 0 || dh <= 0) return;
        UIRect vis;
        if (!clipRect({dx, dy, dw, dh}, &vis)) return;
        SpriteData s;
        s.x = vis.x; s.y = vis.y; s.width = vis.w; s.height = vis.h;
        s.u0 = u0; s.v0 = v0; s.u1 = u1; s.v1 = v1;
        s.r = tint.r; s.g = tint.g; s.b = tint.b; s.a = tint.a;
        s.originX = 0; s.originY = 0;
        m_batch->draw(s, tex);
    };

    // Center
    drawSlice(r.x + slice, r.y + slice,
              r.w - 2 * slice, r.h - 2 * slice, su0, sv0, su1, sv1);
    // Top edge
    drawSlice(r.x + slice, r.y, r.w - 2 * slice, slice, su0, 0, su1, sv0);
    // Bottom edge
    drawSlice(r.x + slice, r.y + r.h - slice, r.w - 2 * slice, slice,
              su0, sv1, su1, 1);
    // Left edge
    drawSlice(r.x, r.y + slice, slice, r.h - 2 * slice, 0, sv0, su0, sv1);
    // Right edge
    drawSlice(r.x + r.w - slice, r.y + slice, slice, r.h - 2 * slice,
              su1, sv0, 1, sv1);
    // Corners
    drawSlice(r.x, r.y, slice, slice, 0, 0, su0, sv0);
    drawSlice(r.x + r.w - slice, r.y, slice, slice, su1, 0, 1, sv0);
    drawSlice(r.x, r.y + r.h - slice, slice, slice, 0, sv1, su0, 1);
    drawSlice(r.x + r.w - slice, r.y + r.h - slice, slice, slice,
              su1, sv1, 1, 1);
}

// ===========================================================================
// UIWidget base
// ===========================================================================
UIWidget::~UIWidget() {
    for (UIWidget* c : m_children) delete c;
    m_children.clear();
}

UIWidget* UIWidget::addChild(UIWidget* child) {
    if (!child) return nullptr;
    child->m_parent = this;
    m_children.push_back(child);
    m_dirty = true;
    return child;
}

void UIWidget::removeChild(UIWidget* child) {
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (*it == child) {
            m_children.erase(it);
            delete child;
            m_dirty = true;
            return;
        }
    }
}

void UIWidget::removeAllChildren() {
    for (UIWidget* c : m_children) delete c;
    m_children.clear();
    m_dirty = true;
}

UIColor UIWidget::resolvedTextColor() const {
    if (!m_style.inheritTextColor) return m_style.textColor;
    if (m_parent) return m_parent->resolvedTextColor();
    return m_style.textColor;
}

float UIWidget::resolvedFontSize() const {
    if (!m_style.inheritFontSize) return m_style.fontSize;
    if (m_parent) return m_parent->resolvedFontSize();
    return m_style.fontSize;
}

// ---------------------------------------------------------------------------
// Default measure: use explicit width/height or fall back to defaults.
// ---------------------------------------------------------------------------
void UIWidget::measure(float availW, float availH, float* outW, float* outH) {
    float w = m_style.width;
    float h = m_style.height;
    if (w < 0) w = std::min(availW, 100.0f);  // arbitrary default for auto
    if (h < 0) h = std::min(availH, 30.0f);

    // If we have children, our content size should encompass them.
    if (!m_children.empty()) {
        // Measure children first.
        float contentAvailW = availW - m_style.padding.horizontal();
        float contentAvailH = availH - m_style.padding.vertical();
        if (contentAvailW < 0) contentAvailW = 0;
        if (contentAvailH < 0) contentAvailH = 0;

        float main = 0, cross = 0;
        bool isRow = (m_style.flexDirection == FlexDirection::Row);
        for (UIWidget* c : m_children) {
            float cw, ch;
            c->measure(contentAvailW, contentAvailH, &cw, &ch);
            if (isRow) {
                main += cw;
                if (ch > cross) cross = ch;
            } else {
                main += ch;
                if (cw > cross) cross = cw;
            }
        }
        float contentMain = main + (isRow ? m_style.padding.horizontal()
                                          : m_style.padding.vertical());
        float contentCross = cross + (isRow ? m_style.padding.vertical()
                                            : m_style.padding.horizontal());
        if (m_style.width < 0)  w = isRow ? contentMain : contentCross;
        if (m_style.height < 0) h = isRow ? contentCross : contentMain;
    }

    // Clamp to min/max
    w = std::max(m_style.minWidth,  std::min(m_style.maxWidth,  w));
    h = std::max(m_style.minHeight, std::min(m_style.maxHeight, h));
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    *outW = w;
    *outH = h;
}

// ---------------------------------------------------------------------------
// Default arrange: run flexbox on children.
// ---------------------------------------------------------------------------
void UIWidget::arrange(const UIRect& rect) {
    m_rect = rect;

    if (m_children.empty()) {
        m_dirty = false;
        return;
    }

    const UIEdges& pad = m_style.padding;
    float contentX = rect.x + pad.left;
    float contentY = rect.y + pad.top;
    float contentW = rect.w - pad.horizontal();
    float contentH = rect.h - pad.vertical();
    if (contentW < 0) contentW = 0;
    if (contentH < 0) contentH = 0;

    bool isRow = (m_style.flexDirection == FlexDirection::Row);
    int n = (int)m_children.size();

    // Pass 1: compute flex base size (main-axis) for each child.
    std::vector<float> baseMain(n);
    std::vector<float> natCross(n);
    for (int i = 0; i < n; i++) {
        UIWidget* c = m_children[(size_t)i];
        float cw, ch;
        // Give children the cross-axis size to measure against.
        float availW = isRow ? contentW : contentW;
        float availH = isRow ? contentH : contentH;
        c->measure(availW, availH, &cw, &ch);
        if (isRow) {
            // Main = width, cross = height
            float bm = c->style().flexBasis;
            if (bm >= 0) {
                baseMain[i] = bm;
            } else if (c->style().width >= 0) {
                baseMain[i] = c->style().width;
            } else {
                baseMain[i] = cw;
            }
            natCross[i] = (c->style().height >= 0) ? c->style().height : ch;
        } else {
            float bm = c->style().flexBasis;
            if (bm >= 0) {
                baseMain[i] = bm;
            } else if (c->style().height >= 0) {
                baseMain[i] = c->style().height;
            } else {
                baseMain[i] = ch;
            }
            natCross[i] = (c->style().width >= 0) ? c->style().width : cw;
        }
        // Clamp base to min/max
        if (isRow) {
            baseMain[i] = std::max(c->style().minWidth,
                                   std::min(c->style().maxWidth, baseMain[i]));
        } else {
            baseMain[i] = std::max(c->style().minHeight,
                                   std::min(c->style().maxHeight, baseMain[i]));
        }
    }

    // Pass 2: distribute free space along main axis with grow/shrink.
    float totalBase = 0;
    for (int i = 0; i < n; i++) totalBase += baseMain[i];
    float mainSize = isRow ? contentW : contentH;
    float free = mainSize - totalBase;

    std::vector<float> finalMain(n);
    if (free >= 0) {
        float totalGrow = 0;
        for (int i = 0; i < n; i++) totalGrow += m_children[(size_t)i]->style().flexGrow;
        if (totalGrow > 0) {
            for (int i = 0; i < n; i++) {
                float g = m_children[(size_t)i]->style().flexGrow;
                finalMain[i] = baseMain[i] + free * (g / totalGrow);
            }
        } else {
            for (int i = 0; i < n; i++) finalMain[i] = baseMain[i];
        }
    } else {
        // Overflow: shrink proportional to flexShrink * baseSize.
        float totalShrink = 0;
        for (int i = 0; i < n; i++) {
            totalShrink += m_children[(size_t)i]->style().flexShrink * baseMain[i];
        }
        if (totalShrink > 0) {
            for (int i = 0; i < n; i++) {
                float s = m_children[(size_t)i]->style().flexShrink;
                if (s > 0 && baseMain[i] > 0) {
                    float factor = (s * baseMain[i]) / totalShrink;
                    finalMain[i] = baseMain[i] + free * factor;  // free is negative
                } else {
                    finalMain[i] = baseMain[i];
                }
            }
        } else {
            for (int i = 0; i < n; i++) finalMain[i] = baseMain[i];
        }
    }

    // Clamp final main to min/max
    for (int i = 0; i < n; i++) {
        UIWidget* c = m_children[(size_t)i];
        if (isRow) {
            finalMain[i] = std::max(c->style().minWidth,
                                    std::min(c->style().maxWidth, finalMain[i]));
        } else {
            finalMain[i] = std::max(c->style().minHeight,
                                    std::min(c->style().maxHeight, finalMain[i]));
        }
        if (finalMain[i] < 0) finalMain[i] = 0;
    }

    // Pass 3: compute cross sizes based on alignItems.
    float crossSize = isRow ? contentH : contentW;
    std::vector<float> finalCross(n);
    for (int i = 0; i < n; i++) {
        UIWidget* c = m_children[(size_t)i];
        if (m_style.alignItems == AlignItems::Stretch) {
            finalCross[i] = crossSize;
        } else {
            finalCross[i] = natCross[i];
        }
        if (isRow) {
            finalCross[i] = std::max(c->style().minHeight,
                                      std::min(c->style().maxHeight, finalCross[i]));
        } else {
            finalCross[i] = std::max(c->style().minWidth,
                                      std::min(c->style().maxWidth, finalCross[i]));
        }
        if (finalCross[i] < 0) finalCross[i] = 0;
    }

    // Pass 4: position along main axis based on justifyContent.
    float totalFinal = 0;
    for (int i = 0; i < n; i++) totalFinal += finalMain[i];
    float gap = 0;
    float lead = 0;
    float extraSpace = mainSize - totalFinal;
    if (extraSpace < 0) extraSpace = 0;
    switch (m_style.justifyContent) {
        case JustifyContent::FlexStart:
            lead = 0;
            gap = 0;
            break;
        case JustifyContent::FlexEnd:
            lead = extraSpace;
            gap = 0;
            break;
        case JustifyContent::Center:
            lead = extraSpace * 0.5f;
            gap = 0;
            break;
        case JustifyContent::SpaceBetween:
            lead = 0;
            gap = (n > 1) ? extraSpace / (float)(n - 1) : 0;
            break;
        case JustifyContent::SpaceAround:
            gap = (n > 0) ? extraSpace / (float)n : 0;
            lead = gap * 0.5f;
            break;
    }

    // Position children
    float cursor = (isRow ? contentX : contentY) + lead;
    for (int i = 0; i < n; i++) {
        UIWidget* c = m_children[(size_t)i];
        float mainPos = cursor;
        float crossPos;
        float crossMargin = 0;  // could incorporate margins here
        (void)crossMargin;
        if (m_style.alignItems == AlignItems::Stretch ||
            m_style.alignItems == AlignItems::FlexStart) {
            crossPos = (isRow ? contentY : contentX);
        } else if (m_style.alignItems == AlignItems::Center) {
            crossPos = (isRow ? contentY : contentX) +
                       (crossSize - finalCross[i]) * 0.5f;
        } else { // FlexEnd
            crossPos = (isRow ? contentY : contentX) +
                       (crossSize - finalCross[i]);
        }
        UIRect childRect;
        if (isRow) {
            childRect = { mainPos, crossPos, finalMain[i], finalCross[i] };
        } else {
            childRect = { crossPos, mainPos, finalCross[i], finalMain[i] };
        }
        c->arrange(childRect);
        cursor += finalMain[i] + gap;
    }

    m_dirty = false;
}

// ---------------------------------------------------------------------------
// Default paint: draw background + border + (no text by default).
// ---------------------------------------------------------------------------
void UIWidget::paint(UIContext& ctx) const {
    if (m_style.backgroundColor.a > 0.001f) {
        if (m_style.borderRadius > 0.5f) {
            ctx.drawRoundedRect(m_rect, m_style.backgroundColor, m_style.borderRadius);
        } else {
            ctx.drawRect(m_rect, m_style.backgroundColor);
        }
    }
    if (m_style.borderWidth > 0.001f && m_style.borderColor.a > 0.001f) {
        ctx.drawRectBorder(m_rect, m_style.borderColor, m_style.borderWidth);
    }
}

// ---------------------------------------------------------------------------
// Default hit-test: children first (reverse order), then self.
// ---------------------------------------------------------------------------
UIWidget* UIWidget::hitTest(float x, float y) {
    if (!m_style.visible) return nullptr;
    if (!m_style.pointerEvents) {
        // Still test children (so children of a non-interactive panel
        // remain interactive) but don't return self.
        for (int i = (int)m_children.size() - 1; i >= 0; i--) {
            UIWidget* hit = m_children[(size_t)i]->hitTest(x, y);
            if (hit) return hit;
        }
        return nullptr;
    }
    for (int i = (int)m_children.size() - 1; i >= 0; i--) {
        UIWidget* hit = m_children[(size_t)i]->hitTest(x, y);
        if (hit) return hit;
    }
    if (contains(x, y)) return this;
    return nullptr;
}

// ===========================================================================
// Label
// ===========================================================================
void Label::setText(const char* s) {
    m_text = s ? s : "";
    m_dirty = true;
}

void Label::measure(float availW, float availH, float* outW, float* outH) {
    float fs = resolvedFontSize();
    float tw = 0;
    if (!m_text.empty()) {
        // Approximate measure without a UIContext: 0.5 * fs per char.
        tw = (float)m_text.size() * fs * 0.5f;
    }
    if (m_style.width >= 0)       tw = m_style.width;
    else if (tw > availW)         tw = availW;
    float th = (m_style.height >= 0) ? m_style.height : fs * 1.2f;
    if (th > availH && availH > 0) th = availH;
    *outW = std::max(m_style.minWidth,  std::min(m_style.maxWidth,  tw));
    *outH = std::max(m_style.minHeight, std::min(m_style.maxHeight, th));
    if (*outW < 0) *outW = 0;
    if (*outH < 0) *outH = 0;
}

void Label::paint(UIContext& ctx) const {
    UIWidget::paint(ctx);
    if (m_text.empty()) return;
    UIColor color = resolvedTextColor();
    float fs = resolvedFontSize();
    ctx.drawTextAligned(m_text.c_str(), m_rect, fs, color, m_style.textAlign);
}

// ===========================================================================
// Button
// ===========================================================================
Button::Button() {
    m_style.height = 32;
    m_style.padding = UIEdges(8);
    m_style.backgroundColor = {0.25f, 0.25f, 0.28f, 1};
    m_style.borderColor = {0.5f, 0.5f, 0.55f, 1};
    m_style.borderWidth = 1;
    m_style.borderRadius = 4;
    m_style.cursor = UICursor::Pointer;
    m_style.tabIndex = 0;  // buttons aren't tabbable by default; caller can set
}

void Button::setText(const char* s) {
    m_text = s ? s : "";
    m_dirty = true;
}

bool Button::onMouseDown(float, float, int button) {
    if (m_disabled || button != 0) return false;
    m_pressed = true;
    return true;
}

bool Button::onMouseUp(float, float, int button) {
    if (m_disabled || button != 0) return false;
    m_pressed = false;
    return true;
}

bool Button::onClick(float, float, int button) {
    if (m_disabled || button != 0) return false;
    if (m_onClick) m_onClick();
    return true;
}

void Button::paint(UIContext& ctx) const {
    UIColor bg = m_style.backgroundColor;
    if (m_disabled)        bg = {0.15f, 0.15f, 0.17f, 1};
    else if (m_pressed)    bg = {0.15f, 0.15f, 0.18f, 1};
    else if (m_hovered)    bg = {0.35f, 0.35f, 0.40f, 1};
    UIRect r = m_rect;
    if (m_style.borderRadius > 0.5f) {
        ctx.drawRoundedRect(r, bg, m_style.borderRadius);
    } else {
        ctx.drawRect(r, bg);
    }
    if (m_style.borderWidth > 0.001f) {
        ctx.drawRectBorder(r, m_style.borderColor, m_style.borderWidth);
    }
    if (!m_text.empty()) {
        UIColor color = m_disabled ? UIColor{0.5f, 0.5f, 0.5f, 1}
                                   : resolvedTextColor();
        float fs = resolvedFontSize();
        ctx.drawTextAligned(m_text.c_str(), m_rect, fs, color, TextAlign::Center);
    }
}

// ===========================================================================
// UIImage
// ===========================================================================
void UIImage::paint(UIContext& ctx) const {
    if (m_texture) {
        if (m_slice > 0.001f) {
            ctx.drawImage9Slice(m_texture, m_rect, m_slice, m_tint);
        } else {
            ctx.drawImage(m_texture, m_rect, m_tint);
        }
    }
    UIWidget::paint(ctx);
}

// ===========================================================================
// Slider
// ===========================================================================
Slider::Slider() {
    m_style.height = 24;
    m_style.backgroundColor = {0.2f, 0.2f, 0.22f, 1};
    m_style.borderRadius = 4;
    m_value = 0;
}

void Slider::setValue(float v) {
    if (m_max > m_min) {
        v = std::max(m_min, std::min(m_max, v));
        if (m_step > 0) {
            float steps = std::round((v - m_min) / m_step);
            v = m_min + steps * m_step;
        }
    }
    m_value = v;
}

float Slider::valueFromPos(float x) const {
    if (m_rect.w < 1) return m_min;
    float t = (x - m_rect.x) / m_rect.w;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return m_min + t * (m_max - m_min);
}

float Slider::posFromValue(float v) const {
    if (m_max <= m_min) return m_rect.x;
    float t = (v - m_min) / (m_max - m_min);
    return m_rect.x + t * m_rect.w;
}

bool Slider::onMouseDown(float x, float y, int button) {
    if (button != 0) return false;
    (void)y;
    m_dragging = true;
    float v = valueFromPos(x);
    float old = m_value;
    setValue(v);
    if (m_onChange && m_value != old) m_onChange(m_value);
    return true;
}

bool Slider::onMouseUp(float, float, int button) {
    if (button != 0) return false;
    m_dragging = false;
    return true;
}

bool Slider::onDrag(float x, float y, float, float) {
    (void)y;
    if (!m_dragging) return false;
    float v = valueFromPos(x);
    float old = m_value;
    setValue(v);
    if (m_onChange && m_value != old) m_onChange(m_value);
    return true;
}

void Slider::paint(UIContext& ctx) const {
    // Track
    UIColor trackColor = m_style.backgroundColor;
    if (m_style.borderRadius > 0.5f) {
        ctx.drawRoundedRect(m_rect, trackColor, m_style.borderRadius);
    } else {
        ctx.drawRect(m_rect, trackColor);
    }
    // Filled portion
    float handleX = posFromValue(m_value);
    UIColor fill = {0.35f, 0.55f, 0.95f, 1};
    UIRect fillRect = { m_rect.x, m_rect.y, handleX - m_rect.x, m_rect.h };
    if (fillRect.w > 0) {
        if (m_style.borderRadius > 0.5f) {
            ctx.drawRoundedRect(fillRect, fill, m_style.borderRadius);
        } else {
            ctx.drawRect(fillRect, fill);
        }
    }
    // Handle (circle-ish square)
    float handleR = m_rect.h * 0.5f;
    UIRect handle = { handleX - handleR, m_rect.y, handleR * 2, m_rect.h };
    UIColor handleColor = {1, 1, 1, 1};
    ctx.drawRect(handle, handleColor);
}

// ===========================================================================
// Checkbox
// ===========================================================================
Checkbox::Checkbox() {
    m_style.height = 24;
    m_style.width = 120;
    m_style.padding = UIEdges(0, 0, 8, 0);
    m_style.cursor = UICursor::Pointer;
}

void Checkbox::setChecked(bool c) {
    if (m_checked == c) return;
    m_checked = c;
    if (m_onChange) m_onChange(m_checked);
}

void Checkbox::setLabel(const char* s) {
    m_label = s ? s : "";
    m_dirty = true;
}

bool Checkbox::onMouseDown(float, float, int button) {
    return button == 0;
}

bool Checkbox::onClick(float, float, int button) {
    if (button != 0) return false;
    setChecked(!m_checked);
    return true;
}

void Checkbox::paint(UIContext& ctx) const {
    float boxSize = m_rect.h;
    UIRect box = { m_rect.x, m_rect.y, boxSize, boxSize };
    UIColor bg = m_checked ? UIColor{0.35f, 0.55f, 0.95f, 1}
                           : UIColor{0.2f, 0.2f, 0.22f, 1};
    ctx.drawRect(box, bg);
    ctx.drawRectBorder(box, UIColor{0.5f, 0.5f, 0.55f, 1}, 1);
    if (m_checked) {
        // Draw a simple "X" / check by drawing two thin lines as rects.
        UIColor tick = {1, 1, 1, 1};
        UIRect tick1 = { m_rect.x + boxSize * 0.2f, m_rect.y + boxSize * 0.5f,
                         boxSize * 0.6f, 2 };
        UIRect tick2 = { m_rect.x + boxSize * 0.2f, m_rect.y + boxSize * 0.5f,
                         2, boxSize * 0.3f };
        ctx.drawRect(tick1, tick);
        ctx.drawRect(tick2, tick);
    }
    if (!m_label.empty()) {
        UIRect textRect = { m_rect.x + boxSize + 4, m_rect.y,
                            m_rect.w - boxSize - 4, m_rect.h };
        UIColor color = resolvedTextColor();
        float fs = resolvedFontSize();
        ctx.drawTextAligned(m_label.c_str(), textRect, fs, color, TextAlign::Left);
    }
}

// ===========================================================================
// TextInput
// ===========================================================================
TextInput::TextInput() {
    m_style.height = 28;
    m_style.padding = UIEdges(6, 4, 6, 4);
    m_style.backgroundColor = {0.15f, 0.15f, 0.17f, 1};
    m_style.borderColor = {0.4f, 0.4f, 0.45f, 1};
    m_style.borderWidth = 1;
    m_style.cursor = UICursor::Text;
    m_style.tabIndex = 1;
}

void TextInput::setText(const char* s) {
    m_text = s ? s : "";
    m_caret = (int)m_text.size();
    m_selStart = -1;
    m_dirty = true;
}

void TextInput::insertAtCaret(const char* utf8) {
    if (!utf8) return;
    if ((int)m_text.size() >= m_maxLength) return;
    m_text.insert((size_t)m_caret, utf8);
    m_caret++;
    m_dirty = true;
}

void TextInput::deleteSelection() {
    if (m_selStart < 0 || m_selStart == m_caret) return;
    int a = std::min(m_selStart, m_caret);
    int b = std::max(m_selStart, m_caret);
    m_text.erase((size_t)a, (size_t)(b - a));
    m_caret = a;
    m_selStart = -1;
    m_dirty = true;
}

bool TextInput::onKeyDown(int key) {
    switch (key) {
        case UI::Key_Backspace:
            if (m_selStart >= 0 && m_selStart != m_caret) {
                deleteSelection();
            } else if (m_caret > 0) {
                m_text.erase((size_t)(m_caret - 1), 1);
                m_caret--;
                m_dirty = true;
            }
            if (m_onChange) m_onChange(m_text.c_str());
            return true;
        case UI::Key_Delete:
            if (m_selStart >= 0 && m_selStart != m_caret) {
                deleteSelection();
            } else if (m_caret < (int)m_text.size()) {
                m_text.erase((size_t)m_caret, 1);
                m_dirty = true;
            }
            if (m_onChange) m_onChange(m_text.c_str());
            return true;
        case UI::Key_Enter:
            if (m_onSubmit) m_onSubmit(m_text.c_str());
            return true;
        case UI::Key_Left:
            if (m_caret > 0) m_caret--;
            return true;
        case UI::Key_Right:
            if (m_caret < (int)m_text.size()) m_caret++;
            return true;
        case UI::Key_Home:
            m_caret = 0;
            return true;
        case UI::Key_End:
            m_caret = (int)m_text.size();
            return true;
        default:
            return false;
    }
}

bool TextInput::onChar(int ch) {
    if (ch < 32 || ch == 127) return false;  // control chars
    if ((int)m_text.size() >= m_maxLength) return false;
    char buf[5] = {};
    if (ch < 0x80) {
        buf[0] = (char)ch;
    } else {
        // Encode UTF-8 (basic plane).
        if (ch < 0x800) {
            buf[0] = (char)(0xC0 | (ch >> 6));
            buf[1] = (char)(0x80 | (ch & 0x3F));
        } else {
            buf[0] = (char)(0xE0 | (ch >> 12));
            buf[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
            buf[2] = (char)(0x80 | (ch & 0x3F));
        }
    }
    insertAtCaret(buf);
    if (m_onChange) m_onChange(m_text.c_str());
    return true;
}

void TextInput::paint(UIContext& ctx) const {
    UIColor bg = m_style.backgroundColor;
    UIColor border = m_focused ? UIColor{0.55f, 0.75f, 1.0f, 1}
                                : m_style.borderColor;
    UIRect r = m_rect;
    ctx.drawRect(r, bg);
    ctx.drawRectBorder(r, border, m_style.borderWidth);

    // Draw text or placeholder
    const std::string& shown = m_text.empty() ? m_placeholder : m_text;
    if (!shown.empty()) {
        UIColor color = m_text.empty() ? UIColor{0.5f, 0.5f, 0.55f, 1}
                                       : resolvedTextColor();
        float fs = resolvedFontSize();
        UIRect textRect = { r.x + m_style.padding.left,
                            r.y + m_style.padding.top,
                            r.w - m_style.padding.horizontal(),
                            r.h - m_style.padding.vertical() };
        ctx.drawTextAligned(shown.c_str(), textRect, fs, color, TextAlign::Left);
    }
    // Caret
    if (m_focused) {
        float fs = resolvedFontSize();
        // Approximate caret x: padding + (caret chars * charWidth)
        float charW = fs * 0.5f;
        float cx = r.x + m_style.padding.left + (float)m_caret * charW;
        UIRect caret = { cx, r.y + 4, 1, r.h - 8 };
        ctx.drawRect(caret, UIColor{1, 1, 1, 0.8f});
    }
}

// ===========================================================================
// ScrollView
// ===========================================================================
ScrollView::ScrollView() {
    m_style.backgroundColor = {0.1f, 0.1f, 0.12f, 1};
    m_style.flexDirection = FlexDirection::Column;
    m_style.flexGrow = 1;
}

bool ScrollView::isChildVisible(int i) const {
    if (i < 0 || i >= (int)m_children.size()) return false;
    const UIRect& cr = m_children[(size_t)i]->rect();
    // Visible if any portion of the child's rect overlaps the viewport rect.
    return rectsOverlap(cr, m_rect);
}

bool ScrollView::onMouseWheel(float, float, float delta) {
    float newScroll = m_scrollTop - delta * 40.0f;
    if (newScroll < 0) newScroll = 0;
    float maxScroll = m_contentHeight - m_rect.h;
    if (maxScroll < 0) maxScroll = 0;
    if (newScroll > maxScroll) newScroll = maxScroll;
    if (newScroll != m_scrollTop) {
        m_scrollTop = newScroll;
        m_dirty = true;
        return true;
    }
    return false;
}

void ScrollView::setScrollTop(float s) {
    if (s < 0) s = 0;
    float maxScroll = m_contentHeight - m_rect.h;
    if (maxScroll < 0) maxScroll = 0;
    if (s > maxScroll) s = maxScroll;
    if (s != m_scrollTop) {
        m_scrollTop = s;
        m_dirty = true;
    }
}

bool ScrollView::onMouseDown(float x, float y, int button) {
    if (button != 0) return false;
    // Start drag-to-scroll only if click is on the scrollbar gutter
    // (right 12 px) or anywhere outside interactive children. For
    // simplicity we let the click fall through to children, and only
    // start drag-scroll if the user grabs the scrollbar.
    float barX = m_rect.x + m_rect.w - 12;
    if (x >= barX && x <= m_rect.x + m_rect.w) {
        m_dragScroll = true;
        m_dragLastY = y;
        return true;
    }
    return false;
}

bool ScrollView::onMouseUp(float, float, int button) {
    if (button != 0) return false;
    if (m_dragScroll) {
        m_dragScroll = false;
        return true;
    }
    return false;
}

bool ScrollView::onDrag(float, float y, float, float dy) {
    if (!m_dragScroll) return false;
    (void)y;
    float maxScroll = m_contentHeight - m_rect.h;
    if (maxScroll <= 0) return false;
    // Translate viewport drag to content scroll.
    float newScroll = m_scrollTop - dy * (maxScroll / std::max(1.0f, m_rect.h));
    if (newScroll < 0) newScroll = 0;
    if (newScroll > maxScroll) newScroll = maxScroll;
    if (newScroll != m_scrollTop) {
        m_scrollTop = newScroll;
        m_dirty = true;
    }
    return true;
}

void ScrollView::arrange(const UIRect& rect) {
    m_rect = rect;

    // Measure total content height (sum of children heights in column mode).
    float contentH = 0;
    float contentW = rect.w;
    for (UIWidget* c : m_children) {
        float cw, ch;
        c->measure(contentW, rect.h, &cw, &ch);
        contentH += ch;
    }
    m_contentHeight = contentH;
    float maxScroll = contentH - rect.h;
    if (maxScroll < 0) maxScroll = 0;
    if (m_scrollTop > maxScroll) m_scrollTop = maxScroll;

    // Lay out children in a column starting at -scrollTop.
    float cursorY = rect.y - m_scrollTop;
    for (UIWidget* c : m_children) {
        float cw, ch;
        c->measure(contentW, rect.h, &cw, &ch);
        UIRect childRect = { rect.x, cursorY, contentW, ch };
        c->arrange(childRect);
        cursorY += ch;
    }
    m_dirty = false;
}

void ScrollView::paint(UIContext& ctx) const {
    UIWidget::paint(ctx);
    // Scrollbar
    float maxScroll = m_contentHeight - m_rect.h;
    if (maxScroll > 0) {
        float barH = (m_rect.h / m_contentHeight) * m_rect.h;
        if (barH < 12) barH = 12;
        float barY = m_rect.y + (m_scrollTop / maxScroll) * (m_rect.h - barH);
        UIRect bar = { m_rect.x + m_rect.w - 8, barY, 6, barH };
        ctx.drawRect(bar, UIColor{0.5f, 0.5f, 0.55f, 0.8f});
    }
}

// ===========================================================================
// ListView (virtualized)
// ===========================================================================
ListView::ListView() {
    m_style.backgroundColor = {0.1f, 0.1f, 0.12f, 1};
    m_style.flexGrow = 1;
}

void ListView::setScrollTop(float s) {
    float maxScroll = (float)m_itemCount * m_itemHeight - m_rect.h;
    if (maxScroll < 0) maxScroll = 0;
    if (s < 0) s = 0;
    if (s > maxScroll) s = maxScroll;
    if (s != m_scrollTop) {
        m_scrollTop = s;
        m_dirty = true;
    }
    m_maxScroll = maxScroll;
}

int ListView::firstVisibleIndex() const {
    if (m_itemHeight <= 0) return 0;
    int idx = (int)(m_scrollTop / m_itemHeight);
    if (idx < 0) idx = 0;
    return idx;
}

int ListView::lastVisibleIndex() const {
    if (m_itemHeight <= 0) return 0;
    int first = firstVisibleIndex();
    int visibleCount = (int)(m_rect.h / m_itemHeight) + 2;  // +2 for partial rows
    int last = first + visibleCount;
    if (last >= m_itemCount) last = m_itemCount - 1;
    return last;
}

bool ListView::onMouseWheel(float, float, float delta) {
    float newScroll = m_scrollTop - delta * m_itemHeight * 2;
    setScrollTop(newScroll);
    return true;
}

void ListView::measure(float availW, float availH, float* outW, float* outH) {
    float w = (m_style.width >= 0) ? m_style.width : availW;
    float h = (m_style.height >= 0) ? m_style.height : availH;
    *outW = std::max(m_style.minWidth,  std::min(m_style.maxWidth,  w));
    *outH = std::max(m_style.minHeight, std::min(m_style.maxHeight, h));
    if (*outW < 0) *outW = 0;
    if (*outH < 0) *outH = 0;
}

void ListView::rebuildPool(int visibleCount) {
    // Adjust pool size to visibleCount + small overscan.
    int target = visibleCount + 2;
    while ((int)m_pool.size() < target) {
        UIWidget* host = new UIWidget();
        m_pool.push_back(host);
    }
    while ((int)m_pool.size() > target) {
        delete m_pool.back();
        m_pool.pop_back();
    }
}

void ListView::arrange(const UIRect& rect) {
    m_rect = rect;
    if (m_itemCount <= 0 || m_itemHeight <= 0) {
        m_dirty = false;
        return;
    }
    float maxScroll = (float)m_itemCount * m_itemHeight - rect.h;
    if (maxScroll < 0) maxScroll = 0;
    m_maxScroll = maxScroll;
    if (m_scrollTop > maxScroll) m_scrollTop = maxScroll;

    int first = firstVisibleIndex();
    int last  = lastVisibleIndex();
    int visibleCount = last - first + 1;
    if (visibleCount < 0) visibleCount = 0;

    rebuildPool(visibleCount);

    // Re-bind pool slots to visible indices.
    if (first != m_lastFirstVisible && m_builder) {
        for (int i = 0; i < visibleCount; i++) {
            int itemIdx = first + i;
            if (itemIdx >= m_itemCount) break;
            UIWidget* host = m_pool[(size_t)i];
            host->removeAllChildren();
            m_builder(host, itemIdx);
        }
        m_lastFirstVisible = first;
    } else if (m_lastFirstVisible < 0 && m_builder) {
        // First arrange: bind everything.
        for (int i = 0; i < visibleCount; i++) {
            int itemIdx = first + i;
            if (itemIdx >= m_itemCount) break;
            UIWidget* host = m_pool[(size_t)i];
            host->removeAllChildren();
            m_builder(host, itemIdx);
        }
        m_lastFirstVisible = first;
    }

    // Position pool slots.
    for (int i = 0; i < visibleCount; i++) {
        int itemIdx = first + i;
        if (itemIdx >= m_itemCount) break;
        float y = rect.y + (float)itemIdx * m_itemHeight - m_scrollTop;
        UIRect slot = { rect.x, y, rect.w, m_itemHeight };
        m_pool[(size_t)i]->arrange(slot);
    }
    m_dirty = false;
}

void ListView::paint(UIContext& ctx) const {
    UIWidget::paint(ctx);
    // Draw each pooled slot.
    for (UIWidget* host : m_pool) {
        if (rectsOverlap(host->rect(), m_rect)) {
            // The host itself has no visible style; just paint its children.
            for (UIWidget* c : host->children()) {
                if (rectsOverlap(c->rect(), m_rect)) {
                    c->paint(ctx);
                }
            }
        }
    }
}

// ===========================================================================
// Dropdown
// ===========================================================================
Dropdown::Dropdown() {
    m_style.height = 28;
    m_style.backgroundColor = {0.2f, 0.2f, 0.22f, 1};
    m_style.borderColor = {0.4f, 0.4f, 0.45f, 1};
    m_style.borderWidth = 1;
    m_style.cursor = UICursor::Pointer;
}

void Dropdown::setOptions(const std::vector<std::string>& opts) {
    m_options = opts;
    if (m_selected >= (int)m_options.size()) m_selected = -1;
    m_dirty = true;
}

void Dropdown::setSelected(int idx) {
    if (idx < 0 || idx >= (int)m_options.size()) return;
    if (m_selected == idx) return;
    m_selected = idx;
    if (m_onChange) m_onChange(m_selected);
}

const char* Dropdown::selectedText() const {
    if (m_selected < 0 || m_selected >= (int)m_options.size()) return "";
    return m_options[(size_t)m_selected].c_str();
}

bool Dropdown::onMouseDown(float, float, int button) {
    if (button != 0) return false;
    return true;
}

bool Dropdown::onClick(float, float, int button) {
    if (button != 0) return false;
    m_expanded = !m_expanded;
    return true;
}

void Dropdown::closeMenu() {
    m_expanded = false;
}

void Dropdown::arrange(const UIRect& rect) {
    UIWidget::arrange(rect);
}

void Dropdown::paint(UIContext& ctx) const {
    UIRect r = m_rect;
    ctx.drawRect(r, m_style.backgroundColor);
    ctx.drawRectBorder(r, m_style.borderColor, m_style.borderWidth);
    if (m_selected >= 0) {
        UIColor color = resolvedTextColor();
        float fs = resolvedFontSize();
        UIRect textRect = { r.x + 6, r.y, r.w - 24, r.h };
        ctx.drawTextAligned(selectedText(), textRect, fs, color, TextAlign::Left);
    }
    // Down-arrow indicator (a small square).
    UIRect arrow = { r.x + r.w - 14, r.y + r.h * 0.4f, 8, 4 };
    ctx.drawRect(arrow, UIColor{0.7f, 0.7f, 0.75f, 1});
    if (m_expanded) {
        // Draw options panel below the dropdown. (For a real implementation
        // this would be a floating overlay; here we draw inline.)
        float optH = m_style.height;
        for (size_t i = 0; i < m_options.size(); i++) {
            UIRect optRect = { r.x, r.y + r.h + (float)i * optH, r.w, optH };
            UIColor bg = ((int)i == m_selected)
                ? UIColor{0.35f, 0.55f, 0.95f, 1}
                : UIColor{0.2f, 0.2f, 0.22f, 1};
            ctx.drawRect(optRect, bg);
            ctx.drawTextAligned(m_options[i].c_str(), optRect,
                                resolvedFontSize(), resolvedTextColor(),
                                TextAlign::Left);
        }
    }
}

// ===========================================================================
// Modal
// ===========================================================================
Modal::Modal() {
    m_style.backgroundColor = {0, 0, 0, 0.4f};
    m_style.flexGrow = 1;
}

void Modal::arrange(const UIRect& rect) {
    m_rect = rect;
    if (m_children.empty()) {
        m_dirty = false;
        return;
    }
    // Center the first child as the "card".
    UIWidget* card = m_children[0];
    float cw = m_cardW;
    float ch = m_cardH;
    float cx = rect.x + (rect.w - cw) * 0.5f;
    float cy = rect.y + (rect.h - ch) * 0.5f;
    UIRect cardRect = { cx, cy, cw, ch };
    card->arrange(cardRect);
    // Other children get arranged as normal flex children (rarely used).
    for (size_t i = 1; i < m_children.size(); i++) {
        m_children[i]->arrange(rect);
    }
    m_dirty = false;
}

void Modal::paint(UIContext& ctx) const {
    if (m_showOverlay) {
        ctx.drawRect(m_rect, m_style.backgroundColor);
    }
    if (!m_children.empty()) {
        m_children[0]->paint(ctx);
    }
}

// ===========================================================================
// Tooltip
// ===========================================================================
Tooltip::Tooltip() {
    m_style.backgroundColor = {0, 0, 0, 0.85f};
    m_style.padding = UIEdges(6, 4, 6, 4);
    m_style.borderRadius = 3;
    m_style.visible = false;
}

void Tooltip::tick(float dt, bool mouseInside) {
    if (mouseInside) {
        m_timer += dt;
        if (m_timer >= m_delay) {
            m_alpha = std::min(1.0f, m_alpha + dt * 6);
        }
    } else {
        m_timer = 0;
        m_alpha = std::max(0.0f, m_alpha - dt * 6);
    }
    m_style.visible = (m_alpha > 0.01f);
}

void Tooltip::paint(UIContext& ctx) const {
    if (!m_style.visible || m_alpha < 0.01f) return;
    UIColor bg = m_style.backgroundColor;
    bg.a *= m_alpha;
    if (m_style.borderRadius > 0.5f) {
        ctx.drawRoundedRect(m_rect, bg, m_style.borderRadius);
    } else {
        ctx.drawRect(m_rect, bg);
    }
    UIColor tc = UIColor{1, 1, 1, m_alpha};
    float fs = resolvedFontSize();
    ctx.drawTextAligned(m_text.c_str(), m_rect, fs, tc, TextAlign::Left);
}

// ===========================================================================
// UICanvasWidget (custom draw)
// ===========================================================================
void UICanvasWidget::paint(UIContext& ctx) const {
    UIWidget::paint(ctx);
    if (m_cb) m_cb(ctx, m_rect);
}

// ===========================================================================
// UI controller
// ===========================================================================
UI::UI() {
    // Font atlas is built lazily in render() so we don't touch GL when the
    // UI controller is constructed in a non-rendering context (e.g. unit
    // tests for layout / input).
}

UI::~UI() {
    m_context.shutdown();
    delete m_root;
}

void UI::setRoot(UIWidget* root) {
    delete m_root;
    m_root = root;
    m_hovered = nullptr;
    m_focused = nullptr;
    m_mouseDownWidget = nullptr;
}

void UI::markAncestorsDirty(UIWidget* w) {
    while (w) {
        w->setDirty(true);
        w = w->parent();
    }
}

void UI::layout(float viewportW, float viewportH) {
    m_viewportW = viewportW;
    m_viewportH = viewportH;
    if (!m_root) return;
    // Always re-layout for now; the dirty flag is used to skip measure work
    // inside individual widgets (a real cache would track subtree dirty).
    UIRect rootRect = {0, 0, viewportW, viewportH};
    m_root->arrange(rootRect);
}

void UI::drawRecursive(UIWidget* w) {
    if (!w || !w->style().visible) return;

    UIRect savedClip = m_context.currentClip();
    UIRect newClip = intersectRect(savedClip, w->rect());
    m_context.pushClip(newClip);

    // Apply opacity to all of this widget's draws by temporarily scaling
    // the alpha channel through the colors. (Simplified: we just pass
    // opacity through to the widget's paint, which is responsible for
    // applying it.)
    w->paint(m_context);

    for (UIWidget* c : w->children()) {
        drawRecursive(c);
    }
    m_context.popClip();
}

void UI::render(SpriteBatch& batch, const Mat4& projection) {
    if (!m_root) return;
    // Lazily build the font atlas on the first render call (GL is ready now).
    if (!m_context.fontAtlasReady()) {
        m_context.init(&batch);
    }
    m_context.setBatch(&batch);
    batch.begin(projection, Mat4::identity());
    // Reset clip stack to the full viewport.
    while (m_context.clipDepth() > 0) m_context.popClip();
    UIRect screen = {0, 0, m_viewportW, m_viewportH};
    m_context.pushClip(screen);
    drawRecursive(m_root);
    m_context.popClip();
    batch.end();
    m_context.setBatch(nullptr);
}

UIWidget* UI::hitTestRecursive(UIWidget* w, float x, float y) {
    if (!w) return nullptr;
    return w->hitTest(x, y);
}

void UI::onMouseMove(float x, float y) {
    m_lastMouseX = x;
    m_lastMouseY = y;
    UIWidget* hit = hitTestRecursive(m_root, x, y);
    if (hit != m_hovered) {
        if (m_hovered) m_hovered->onLeave();
        m_hovered = hit;
        if (m_hovered) m_hovered->onEnter();
    }
    if (hit) hit->onMouseMove(x, y);

    // Drag detection
    if (m_mouseDownWidget && m_mouseDownButton == 0 && !m_dragStarted) {
        float dx = x - m_mouseDownX;
        float dy = y - m_mouseDownY;
        if (dx * dx + dy * dy > 16.0f) {  // 4px threshold
            m_dragStarted = true;
            m_mouseDownWidget->onDragStart(x, y);
        }
    }
    if (m_dragStarted && m_mouseDownWidget) {
        m_mouseDownWidget->onDrag(x, y, x - m_lastMouseX, y - m_lastMouseY);
    }
}

void UI::onMouseDown(float x, float y, int button) {
    UIWidget* hit = hitTestRecursive(m_root, x, y);
    if (hit) {
        hit->onMouseDown(x, y, button);
        m_mouseDownWidget = hit;
        m_mouseDownButton = button;
        m_mouseDownX = x;
        m_mouseDownY = y;
        m_dragStarted = false;
        // Auto-focus clickable widgets (those that handle input).
        // For tabbable widgets, focus on click.
        if (hit->style().tabIndex > 0) {
            setFocus(hit);
        }
    } else {
        m_mouseDownWidget = nullptr;
        m_mouseDownButton = -1;
    }
}

void UI::onMouseUp(float x, float y, int button) {
    UIWidget* hit = hitTestRecursive(m_root, x, y);
    if (m_dragStarted && m_mouseDownWidget) {
        m_mouseDownWidget->onDragEnd(x, y);
        m_dragStarted = false;
    }
    if (hit) {
        hit->onMouseUp(x, y, button);
        if (hit == m_mouseDownWidget && button == m_mouseDownButton) {
            hit->onClick(x, y, button);
        }
    }
    m_mouseDownWidget = nullptr;
    m_mouseDownButton = -1;
}

void UI::onMouseWheel(float x, float y, float delta) {
    UIWidget* hit = hitTestRecursive(m_root, x, y);
    // Walk up to find a widget that handles the wheel.
    UIWidget* w = hit;
    while (w) {
        if (w->onMouseWheel(x, y, delta)) return;
        w = w->parent();
    }
}

void UI::collectTabOrder(UIWidget* w, std::vector<UIWidget*>& out) {
    if (!w) return;
    if (w->style().tabIndex > 0) out.push_back(w);
    for (UIWidget* c : w->children()) collectTabOrder(c, out);
}

void UI::setFocus(UIWidget* w) {
    if (m_focused == w) return;
    if (m_focused) m_focused->onBlur();
    m_focused = w;
    if (m_focused) m_focused->onFocus();
}

void UI::onKeyDown(int key, bool shift, bool ctrl) {
    (void)ctrl;
    if (key == Key_Tab) {
        // Cycle focus through tabbable widgets.
        std::vector<UIWidget*> order;
        collectTabOrder(m_root, order);
        if (order.empty()) return;
        // Sort by tabIndex (stable so DFS order is preserved within same index).
        std::stable_sort(order.begin(), order.end(),
            [](UIWidget* a, UIWidget* b) {
                return a->style().tabIndex < b->style().tabIndex;
            });
        if (!m_focused) {
            setFocus(shift ? order.back() : order.front());
        } else {
            auto it = std::find(order.begin(), order.end(), m_focused);
            if (it == order.end()) {
                setFocus(shift ? order.back() : order.front());
            } else {
                int idx = (int)(it - order.begin());
                if (shift) idx = (idx - 1 + (int)order.size()) % (int)order.size();
                else       idx = (idx + 1) % (int)order.size();
                setFocus(order[(size_t)idx]);
            }
        }
        return;
    }
    if (m_focused) {
        m_focused->onKeyDown(key);
    }
}

void UI::onKeyUp(int key) {
    if (m_focused) m_focused->onKeyUp(key);
}

void UI::onChar(int ch) {
    if (m_focused) m_focused->onChar(ch);
}

void UI::tick(float /*dt*/) {
    // Per-frame updates for animations / tooltips go here.
    // Currently the Tooltip widget is driven by its own tick() method,
    // which the application can call directly.
}

} // namespace td
