// =============================================================================
// TD Engine - UI Toolkit tests (wave1-ui)
//
// Tests the v2 UI system declared in src/ui/ui_widgets.h and implemented in
// src/ui/ui.cpp. Covers:
//   1. Flexbox layout produces non-zero sizes with children inside parents.
//   2. Hit-testing + click dispatch (onClick fires when clicked, not when
//      clicked outside).
//   3. Slider drag updates value within range and fires onChange.
//   4. TextInput typing + backspace.
//   5. ScrollView virtualization (only N children visible at once, scrolling
//      changes which are visible).
//   6. ListView with 10000 items lays out in <1ms (virtualization).
//   7. Style inheritance (parent textColor propagates to child).
//
// Build (Linux, direct g++):
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc  tests/test_ui.cpp src/ui/ui.cpp
//       src/renderer/sprite_batch.cpp src/renderer/texture.cpp
//       src/renderer/gl_shader.cpp  tests/stub_logger.cpp tests/stub_gl.cpp
//       -o build/test_ui
// =============================================================================
#include "../src/ui/ui_widgets.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace td;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST(name, cond) do { \
    if (cond) { printf("PASS: %s\n", name); g_testsPassed++; } \
    else      { printf("FAIL: %s\n", name); g_testsFailed++; } \
} while (0)

static bool rectInside(const UIRect& outer, const UIRect& inner) {
    return inner.x >= outer.x - 0.5f &&
           inner.y >= outer.y - 0.5f &&
           inner.x + inner.w <= outer.x + outer.w + 0.5f &&
           inner.y + inner.h <= outer.y + outer.h + 0.5f;
}

// ---------------------------------------------------------------------------
// Test 1: Flexbox layout produces non-zero sizes with children inside parents
// ---------------------------------------------------------------------------
static void testLayout() {
    printf("\n=== Test 1: Flexbox Layout ===\n");

    UI ui;
    Container* root = new Container();
    root->setDirection(FlexDirection::Column);
    root->setWidth(800);
    root->setHeight(600);

    Label* label1 = new Label();
    label1->setText("Hello");
    root->addChild(label1);

    Button* btn = new Button();
    btn->setText("Click");
    root->addChild(btn);

    Container* row = new Container();
    row->setDirection(FlexDirection::Row);
    row->setFlexGrow(1);
    Label* rowL1 = new Label();
    rowL1->setText("A");
    rowL1->setFlexGrow(1);
    Label* rowL2 = new Label();
    rowL2->setText("B");
    rowL2->setFlexGrow(1);
    row->addChild(rowL1);
    row->addChild(rowL2);
    root->addChild(row);

    ui.setRoot(root);
    ui.layout(800, 600);

    const UIRect& rRoot = root->rect();
    const UIRect& rL1   = label1->rect();
    const UIRect& rBtn  = btn->rect();
    const UIRect& rRow  = row->rect();
    const UIRect& rRL1  = rowL1->rect();
    const UIRect& rRL2  = rowL2->rect();

    TEST("root non-zero", rRoot.w > 0 && rRoot.h > 0);
    TEST("label1 non-zero", rL1.w > 0 && rL1.h > 0);
    TEST("button non-zero", rBtn.w > 0 && rBtn.h > 0);
    TEST("row container non-zero", rRow.w > 0 && rRow.h > 0);
    TEST("row label1 non-zero", rRL1.w > 0 && rRL1.h > 0);
    TEST("row label2 non-zero", rRL2.w > 0 && rRL2.h > 0);

    TEST("label1 inside root", rectInside(rRoot, rL1));
    TEST("button inside root", rectInside(rRoot, rBtn));
    TEST("row inside root", rectInside(rRoot, rRow));
    TEST("row label1 inside row", rectInside(rRow, rRL1));
    TEST("row label2 inside row", rectInside(rRow, rRL2));

    // With flexGrow=1 on the row labels, they should split the row width.
    TEST("row labels share width (each ~half)",
         rRL1.w > rRow.w * 0.3f && rRL2.w > rRow.w * 0.3f);
}

// ---------------------------------------------------------------------------
// Test 2: Hit-testing + click dispatch
// ---------------------------------------------------------------------------
static void testClick() {
    printf("\n=== Test 2: Hit-Test Click ===\n");

    UI ui;
    Container* root = new Container();
    root->setDirection(FlexDirection::Column);
    root->setWidth(200);
    root->setHeight(100);

    Button* btn = new Button();
    btn->setText("Click me");
    btn->setHeight(40);
    int clicks = 0;
    btn->setOnClick([&]() { clicks++; });
    root->addChild(btn);

    ui.setRoot(root);
    ui.layout(200, 100);

    const UIRect& rBtn = btn->rect();
    float cx = rBtn.x + rBtn.w * 0.5f;
    float cy = rBtn.y + rBtn.h * 0.5f;

    // Click inside the button.
    ui.onMouseDown(cx, cy, 0);
    ui.onMouseUp(cx, cy, 0);
    TEST("click inside button fires onClick", clicks == 1);

    // Click outside the button (below it).
    float outsideY = rBtn.y + rBtn.h + 20;
    ui.onMouseDown(cx, outsideY, 0);
    ui.onMouseUp(cx, outsideY, 0);
    TEST("click outside button does not fire onClick", clicks == 1);

    // Click inside button again to make sure it still fires.
    ui.onMouseDown(cx, cy, 0);
    ui.onMouseUp(cx, cy, 0);
    TEST("second click inside button fires onClick", clicks == 2);
}

// ---------------------------------------------------------------------------
// Test 3: Slider drag updates value within range, fires onChange
// ---------------------------------------------------------------------------
static void testSlider() {
    printf("\n=== Test 3: Slider Drag ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(300);
    root->setHeight(50);

    Slider* slider = new Slider();
    slider->setRange(0.0f, 1.0f);
    slider->setValue(0.2f);
    slider->setStep(0.01f);
    int changeCount = 0;
    float lastValue = slider->value();
    slider->setOnChange([&](float v) {
        lastValue = v;
        changeCount++;
    });
    root->addChild(slider);

    ui.setRoot(root);
    ui.layout(300, 50);

    const UIRect& r = slider->rect();
    // Position representing value 0.8
    float x20 = r.x + 0.2f * r.w;
    float x80 = r.x + 0.8f * r.w;
    float y   = r.y + r.h * 0.5f;

    // Press at 0.2, drag to 0.8.
    ui.onMouseDown(x20, y, 0);
    ui.onMouseMove(x80, y);
    ui.onMouseUp(x80, y, 0);

    TEST("slider onChange fired during drag", changeCount >= 1);
    TEST("slider final value near 0.8", lastValue > 0.7f && lastValue < 0.9f);
    TEST("slider value within [0,1]",
         slider->value() >= slider->min() && slider->value() <= slider->max());
}

// ---------------------------------------------------------------------------
// Test 4: TextInput typing + backspace
// ---------------------------------------------------------------------------
static void testTextInput() {
    printf("\n=== Test 4: TextInput ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(300);
    root->setHeight(50);

    TextInput* input = new TextInput();
    int changeCount = 0;
    input->setOnChange([&](const char*) { changeCount++; });
    root->addChild(input);

    ui.setRoot(root);
    ui.layout(300, 50);

    // Focus the input so it receives keyboard events.
    ui.setFocus(input);

    // Type "hello".
    const char* hello = "hello";
    for (const char* p = hello; *p; ++p) ui.onChar((int)*p);

    TEST("text is 'hello' after typing", std::strcmp(input->text(), "hello") == 0);
    TEST("onChange fired 5 times", changeCount == 5);

    // Backspace twice -> "hel"
    ui.onKeyDown(UI::Key_Backspace);
    ui.onKeyDown(UI::Key_Backspace);
    TEST("text is 'hel' after 2 backspaces",
         std::strcmp(input->text(), "hel") == 0);

    // Type one more char to verify caret is at end.
    ui.onChar((int)'p');
    TEST("text is 'help' after typing p",
         std::strcmp(input->text(), "help") == 0);
}

// ---------------------------------------------------------------------------
// Test 5: ScrollView virtualization
// ---------------------------------------------------------------------------
static void testScrollView() {
    printf("\n=== Test 5: ScrollView ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(200);
    root->setHeight(200);

    ScrollView* sv = new ScrollView();
    sv->setHeight(200);
    sv->setWidth(200);
    // 100 children, each 30 px tall -> 3000 px content.
    for (int i = 0; i < 100; i++) {
        Label* l = new Label();
        char buf[32];
        std::snprintf(buf, sizeof(buf), "item %d", i);
        l->setText(buf);
        l->setHeight(30);
        sv->addChild(l);
    }
    root->addChild(sv);

    ui.setRoot(root);
    ui.layout(200, 200);

    // Count visible children at scrollTop=0.
    int visibleAt0 = 0;
    for (int i = 0; i < sv->childCount(); i++) {
        if (sv->isChildVisible(i)) visibleAt0++;
    }
    // Viewport is 200px tall, children 30px -> ~7 visible (6 full + 1 partial).
    TEST("only N children visible at scroll 0 (N < 100)", visibleAt0 < 100);
    TEST("at least one child visible at scroll 0", visibleAt0 > 0);

    // Child 0 visible at scroll 0; child 50 not.
    TEST("child 0 visible at scroll 0", sv->isChildVisible(0));
    TEST("child 50 not visible at scroll 0", !sv->isChildVisible(50));

    // Scroll down 300px (10 rows).
    float before = sv->scrollTop();
    sv->setScrollTop(300.0f);
    float after = sv->scrollTop();
    ui.layout(200, 200);  // re-layout after scroll change

    TEST("scrollTop changed", after > before);
    TEST("child 0 not visible after scroll 300", !sv->isChildVisible(0));
    TEST("some child visible after scroll 300", sv->isChildVisible(10));
}

// ---------------------------------------------------------------------------
// Test 6: ListView with 10000 items lays out in <1ms
// ---------------------------------------------------------------------------
static void testListViewPerf() {
    printf("\n=== Test 6: ListView 10k Items ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(200);
    root->setHeight(200);

    ListView* lv = new ListView();
    lv->setWidth(200);
    lv->setHeight(200);
    lv->setItemHeight(20);
    lv->setItemCount(10000);
    int buildCalls = 0;
    lv->setItemBuilder([&buildCalls](UIWidget* host, int index) {
        // Add a simple Label as the row content.
        Label* l = new Label();
        char buf[32];
        std::snprintf(buf, sizeof(buf), "row %d", index);
        l->setText(buf);
        host->addChild(l);
        buildCalls++;
    });
    root->addChild(lv);

    ui.setRoot(root);

    // Time the layout call.
    auto t0 = std::chrono::high_resolution_clock::now();
    ui.layout(200, 200);
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    printf("  ListView layout (10000 items): %.4f ms\n", ms);
    TEST("layout < 1ms", ms < 1.0);
    TEST("only ~10 visible items built (not 10000)",
         buildCalls > 0 && buildCalls < 50);

    int first = lv->firstVisibleIndex();
    int last  = lv->lastVisibleIndex();
    int visibleCount = last - first + 1;
    TEST("visible window is small (~10)", visibleCount >= 5 && visibleCount <= 20);

    // Scroll and verify visible window moves.
    lv->setScrollTop(1000.0f);
    ui.layout(200, 200);
    int first2 = lv->firstVisibleIndex();
    TEST("scrolling advances firstVisibleIndex", first2 > first);
}

// ---------------------------------------------------------------------------
// Test 7: Style inheritance (text color)
// ---------------------------------------------------------------------------
static void testStyleInheritance() {
    printf("\n=== Test 7: Style Inheritance ===\n");

    Container* parent = new Container();
    parent->setTextColor({1, 0, 0, 1});  // red, disables inheritance locally

    Label* child = new Label();
    child->setText("inherited");
    parent->addChild(child);

    UIColor c = child->resolvedTextColor();
    TEST("child inherits parent's red text color",
         c.r > 0.99f && c.g < 0.01f && c.b < 0.01f);

    // Grandchild should also inherit.
    Container* middle = new Container();
    parent->addChild(middle);
    Label* grandchild = new Label();
    grandchild->setText("grand");
    middle->addChild(grandchild);

    UIColor gc = grandchild->resolvedTextColor();
    TEST("grandchild inherits grandparent's red text color",
         gc.r > 0.99f && gc.g < 0.01f && gc.b < 0.01f);

    // Override at the middle level.
    middle->setTextColor({0, 1, 0, 1});  // green
    UIColor gc2 = grandchild->resolvedTextColor();
    TEST("grandchild inherits middle's green after override",
         gc2.g > 0.99f && gc2.r < 0.01f);

    delete parent;
}

// ---------------------------------------------------------------------------
// Test 8: Hit-test respects visible=false and pointerEvents=false
// ---------------------------------------------------------------------------
static void testHitTestFlags() {
    printf("\n=== Test 8: Hit-Test Flags ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(200);
    root->setHeight(200);

    Button* top = new Button();
    top->setText("top");
    top->setWidth(200);
    top->setHeight(100);
    int topClicks = 0;
    top->setOnClick([&]() { topClicks++; });
    root->addChild(top);

    Button* bottom = new Button();
    bottom->setText("bottom");
    bottom->setWidth(200);
    bottom->setHeight(100);
    int bottomClicks = 0;
    bottom->setOnClick([&]() { bottomClicks++; });
    root->addChild(bottom);

    ui.setRoot(root);
    ui.layout(200, 200);

    // Click in the bottom button (y=150).
    ui.onMouseDown(100, 150, 0);
    ui.onMouseUp(100, 150, 0);
    TEST("bottom button click fires (default)", bottomClicks == 1);
    TEST("top button click does not fire", topClicks == 0);

    // Make top button invisible; click in its area should pass through to bottom.
    // (Layout: top at y=0..100, bottom at y=100..200. If top is invisible,
    // hit-test skips it. But bottom isn't behind top — they're stacked.)
    top->setVisible(false);
    ui.layout(200, 200);
    // Click at y=50 (in top's original area). Top is invisible, so hit-test
    // returns the root container, not top. The click should not fire top.
    ui.onMouseDown(100, 50, 0);
    ui.onMouseUp(100, 50, 0);
    TEST("invisible button does not fire onClick", topClicks == 0);
}

// ---------------------------------------------------------------------------
// Test 9: Checkbox toggles
// ---------------------------------------------------------------------------
static void testCheckbox() {
    printf("\n=== Test 9: Checkbox ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(200);
    root->setHeight(50);

    Checkbox* cb = new Checkbox();
    cb->setLabel("Accept");
    bool lastState = false;
    int changeCount = 0;
    cb->setOnChange([&](bool v) { lastState = v; changeCount++; });
    root->addChild(cb);

    ui.setRoot(root);
    ui.layout(200, 50);

    const UIRect& r = cb->rect();
    float cx = r.x + 10;
    float cy = r.y + r.h * 0.5f;

    TEST("checkbox starts unchecked", !cb->checked());
    ui.onMouseDown(cx, cy, 0);
    ui.onMouseUp(cx, cy, 0);
    TEST("checkbox checked after click", cb->checked());
    TEST("onChange fired once", changeCount == 1);
    TEST("onChange received true", lastState);

    ui.onMouseDown(cx, cy, 0);
    ui.onMouseUp(cx, cy, 0);
    TEST("checkbox unchecked after second click", !cb->checked());
    TEST("onChange fired twice", changeCount == 2);
}

// ---------------------------------------------------------------------------
// Test 10: Tab focus cycling
// ---------------------------------------------------------------------------
static void testTabFocus() {
    printf("\n=== Test 10: Tab Focus Cycling ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(300);
    root->setHeight(200);

    TextInput* a = new TextInput(); a->setTabIndex(1);
    TextInput* b = new TextInput(); b->setTabIndex(2);
    TextInput* c = new TextInput(); c->setTabIndex(3);
    root->addChild(a);
    root->addChild(b);
    root->addChild(c);

    ui.setRoot(root);
    ui.layout(300, 200);

    TEST("no widget focused initially", ui.focused() == nullptr);

    ui.onKeyDown(UI::Key_Tab);
    TEST("first Tab focuses first tabbable (a)", ui.focused() == a);

    ui.onKeyDown(UI::Key_Tab);
    TEST("second Tab focuses b", ui.focused() == b);

    ui.onKeyDown(UI::Key_Tab);
    TEST("third Tab focuses c", ui.focused() == c);

    ui.onKeyDown(UI::Key_Tab);
    TEST("Tab wraps to a", ui.focused() == a);

    ui.onKeyDown(UI::Key_Tab, true /*shift*/);
    TEST("Shift-Tab wraps back to c", ui.focused() == c);
}

// ---------------------------------------------------------------------------
// Test 11: Drag detection (>4px threshold)
// ---------------------------------------------------------------------------
static void testDrag() {
    printf("\n=== Test 11: Drag Detection ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(300);
    root->setHeight(100);

    Slider* slider = new Slider();
    slider->setRange(0, 100);
    slider->setValue(10);
    slider->setWidth(300);
    slider->setHeight(40);
    int changeCount = 0;
    slider->setOnChange([&](float) { changeCount++; });
    root->addChild(slider);

    ui.setRoot(root);
    ui.layout(300, 100);

    const UIRect& r = slider->rect();
    float startX = r.x + r.w * 0.1f;  // value=10
    float endX   = r.x + r.w * 0.9f;  // value=90
    float y      = r.y + r.h * 0.5f;

    // Move <4px shouldn't be a drag.
    int before = changeCount;
    ui.onMouseDown(startX, y, 0);
    ui.onMouseMove(startX + 2, y);  // small move, no drag yet
    TEST("small move does not trigger drag (no onChange)",
         changeCount == before);
    ui.onMouseUp(startX + 2, y, 0);

    // Move >4px should be a drag.
    before = changeCount;
    ui.onMouseDown(startX, y, 0);
    ui.onMouseMove(startX + 5, y);   // crosses 4px threshold -> drag starts
    ui.onMouseMove(endX, y);          // drag to end
    ui.onMouseUp(endX, y, 0);
    TEST("drag beyond threshold fires onChange", changeCount > before);
    TEST("slider value increased after drag", slider->value() > 50.0f);
}

// ---------------------------------------------------------------------------
// Test 12: Dropdown selection
// ---------------------------------------------------------------------------
static void testDropdown() {
    printf("\n=== Test 12: Dropdown ===\n");

    UI ui;
    Container* root = new Container();
    root->setWidth(200);
    root->setHeight(50);

    Dropdown* dd = new Dropdown();
    std::vector<std::string> opts = {"apple", "banana", "cherry"};
    dd->setOptions(opts);
    dd->setHeight(30);
    int lastIdx = -99;
    int changeCount = 0;
    dd->setOnChange([&](int i) { lastIdx = i; changeCount++; });
    root->addChild(dd);

    ui.setRoot(root);
    ui.layout(200, 50);

    TEST("dropdown starts unselected", dd->selected() == -1);

    dd->setSelected(1);
    TEST("dropdown selected index 1", dd->selected() == 1);
    TEST("dropdown onChange fired once", changeCount == 1);
    TEST("dropdown selected text is banana",
         std::strcmp(dd->selectedText(), "banana") == 0);
}

// ---------------------------------------------------------------------------
// Test 13: Clipping (UIContext clip stack)
// ---------------------------------------------------------------------------
static void testClipping() {
    printf("\n=== Test 13: Clip Stack ===\n");

    UIContext ctx;
    UIRect screen = {0, 0, 800, 600};
    ctx.pushClip(screen);

    UIRect inside = {100, 100, 200, 200};
    UIRect outside = {1000, 1000, 100, 100};
    UIRect partial = {700, 100, 200, 200};

    UIRect vis;
    TEST("inside rect visible", ctx.clipRect(inside, &vis) &&
        vis.w > 0 && vis.h > 0);
    TEST("outside rect not visible", !ctx.clipRect(outside, &vis));
    TEST("partial rect clipped to screen",
         ctx.clipRect(partial, &vis) &&
         vis.x == 700 && vis.w == 100);  // clipped to screen edge

    ctx.pushClip({0, 0, 400, 400});
    UIRect vis2;
    TEST("nested clip narrows visible region",
         ctx.clipRect({300, 300, 200, 200}, &vis2) &&
         vis2.x == 300 && vis2.y == 300 &&
         vis2.w == 100 && vis2.h == 100);
    ctx.popClip();
    ctx.popClip();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    printf("TD Engine UI Tests (wave1-ui)\n");
    printf("=============================\n");

    testLayout();
    testClick();
    testSlider();
    testTextInput();
    testScrollView();
    testListViewPerf();
    testStyleInheritance();
    testHitTestFlags();
    testCheckbox();
    testTabFocus();
    testDrag();
    testDropdown();
    testClipping();

    printf("\n=============================\n");
    printf("Results: %d passed, %d failed\n", g_testsPassed, g_testsFailed);
    return g_testsFailed > 0 ? 1 : 0;
}
