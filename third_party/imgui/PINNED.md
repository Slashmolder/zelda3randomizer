# Vendored Dear ImGui — pin record

- **IMGUI_VERSION**: 1.91.6
- **Tag**: v1.91.6
- **Commit**: 993fa347495860ed44b83574254ef2a317d0c14f
- **Source**: https://github.com/ocornut/imgui (non-docking branch)
- **Vendored**: 2026-05-28 for add-rando-native-settings-window

## Files vendored
Core (production only — `imgui_demo.cpp` deliberately excluded):
imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp,
imgui.h, imgui_internal.h, imconfig.h, imstb_rectpack.h, imstb_textedit.h, imstb_truetype.h

Backends (third_party/imgui/backends/): imgui_impl_sdl2.{cpp,h}, imgui_impl_opengl3.{cpp,h}, imgui_impl_opengl3_loader.h

## Build notes
- Compiled C++17, -fno-exceptions -fno-rtti (Makefile) / stdcpp17 (MSBuild). NOT under -Werror / /WX.
- OpenGL3 backend uses its OWN embedded loader (imgui_impl_opengl3_loader.h). MUST NOT include
  third_party/gl_core in any ImGui TU; the game's gl_core loader lives only in src/opengl.c. TU isolation
  keeps the two GL loaders from colliding (embedded loader uses static fn pointers; gl_core exports ptrc_gl*).
