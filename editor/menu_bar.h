#pragma once
#include "../src/ecs/world.h"

namespace td {
struct GuiContext;

struct MenuBarState {
    int openMenu = -1; // -1 = none, 0=File, 1=Edit, 2=View, 3=GameObject, 4=Component, 5=Build, 6=Help
    bool showAbout = false;
};

struct MenuBar {
    MenuBarState state;
    bool visible = true;

    enum class Action {
        None, NewScene, OpenScene, SaveScene, SaveSceneAs, Exit,
        Undo, Redo, Delete, SelectAll,
        ToggleScenePanel, ToggleInspector, ToggleAssetBrowser, ToggleConsole, ResetLayout,
        AddEmpty, AddSprite, AddCube, AddSphere, AddPlane, AddDirLight, AddPointLight, AddCamera,
        BuildWindows, BuildWeb, BuildInstaller, Run,
        About
    };

    Action render(GuiContext& gui);
};
} // namespace td
