#pragma once

namespace td {
struct GuiContext;
struct AssetBrowser {
    char currentPath[512] = "assets";
    char filterText[128] = {};
    int filterType = 0; // 0=All,1=Tex,2=Model,3=Audio,4=Script,5=Shader
    bool visible = true;
    void render(GuiContext& gui);
};
} // namespace td
