#include "ui/ui.hpp"

#include "platform/install.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cstdint>

namespace ui {

namespace {

ImFont* heading_font = nullptr;

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

const ImVec4 accent = rgb(232, 146, 52);
const ImVec4 accent_hover = rgb(245, 168, 82);
const ImVec4 muted = rgb(150, 150, 158);

ImTextureID id_of(const texture& value) {
    return static_cast<ImTextureID>(static_cast<std::intptr_t>(value.id));
}

void style() {
    ImGuiStyle& look = ImGui::GetStyle();
    ImGui::StyleColorsDark(&look);
    look.WindowPadding = ImVec2(16, 14);
    look.FramePadding = ImVec2(10, 6);
    look.ItemSpacing = ImVec2(10, 8);
    look.FrameRounding = 6;
    look.ChildRounding = 8;
    look.GrabRounding = 6;
    look.WindowBorderSize = 0;
    look.ChildBorderSize = 1;
    ImVec4* colors = look.Colors;
    colors[ImGuiCol_WindowBg] = rgb(24, 24, 27);
    colors[ImGuiCol_ChildBg] = rgb(30, 30, 34);
    colors[ImGuiCol_Border] = rgb(48, 48, 54);
    colors[ImGuiCol_FrameBg] = rgb(42, 42, 48);
    colors[ImGuiCol_FrameBgHovered] = rgb(54, 54, 62);
    colors[ImGuiCol_FrameBgActive] = rgb(62, 62, 70);
    colors[ImGuiCol_Button] = rgb(46, 46, 52);
    colors[ImGuiCol_ButtonHovered] = rgb(60, 60, 68);
    colors[ImGuiCol_ButtonActive] = rgb(72, 72, 80);
    colors[ImGuiCol_Header] = rgb(70, 52, 32);
    colors[ImGuiCol_HeaderHovered] = rgb(52, 52, 58);
    colors[ImGuiCol_HeaderActive] = rgb(84, 60, 34);
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_hover;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_Text] = rgb(232, 232, 236);
    colors[ImGuiCol_TextDisabled] = muted;
    colors[ImGuiCol_ScrollbarBg] = rgb(30, 30, 34, 0);
}

}

texture upload(const image::picture& picture) {
    texture result{0, picture.width, picture.height};
    if (picture.empty()) return result;
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, picture.width, picture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, picture.pixels.data());
    result.id = id;
    return result;
}

void release(texture& value) {
    if (value.id) {
        GLuint id = value.id;
        glDeleteTextures(1, &id);
    }
    value = {};
}

bool window::open(const std::string& title) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    handle = SDL_CreateWindow(title.c_str(), 1600, 1000,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!handle) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    context = SDL_GL_CreateContext(handle);
    if (!context) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(handle, static_cast<SDL_GLContext>(context));
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    const float density = std::max(1.0f, SDL_GetWindowPixelDensity(handle));
    const float scale = std::max(1.0f, SDL_GetWindowDisplayScale(handle));
    const float points = 17.0f * scale / density;
    ImFontConfig config;
    config.OversampleH = 2;
    if (auto file = platform::interface_font(false); !file.empty())
        io.Fonts->AddFontFromFileTTF(file.c_str(), points * density, &config);
    else
        io.Fonts->AddFontDefault();
    if (auto file = platform::interface_font(true); !file.empty())
        heading_font = io.Fonts->AddFontFromFileTTF(file.c_str(), points * 1.45f * density, &config);
    io.FontGlobalScale = 1.0f / density;

    style();
    ImGui::GetStyle().ScaleAllSizes(scale / density);
    ImGui_ImplSDL3_InitForOpenGL(handle, context);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void window::close() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (context) SDL_GL_DestroyContext(static_cast<SDL_GLContext>(context));
    if (handle) SDL_DestroyWindow(handle);
    SDL_Quit();
}

bool window::frame() {
    if (busy_frames <= 0) SDL_WaitEventTimeout(nullptr, 250);
    else --busy_frames;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        busy_frames = 2;
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) closing = true;
        if (event.type == SDL_EVENT_DROP_FILE && event.drop.data) {
            std::lock_guard lock(incoming_lock);
            incoming.emplace_back(event.drop.data);
        }
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return !closing;
}

void window::present() {
    ImGui::Render();
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(handle, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.094f, 0.094f, 0.106f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(handle);
}

void window::chosen(void* self, const char* const* files, int) {
    if (!files || !files[0]) return;
    auto* owner = static_cast<window*>(self);
    std::lock_guard lock(owner->incoming_lock);
    owner->incoming.emplace_back(files[0]);
}

void window::choose_picture() {
    static const SDL_DialogFileFilter filters[] = {{"Pictures", "png;jpg;jpeg;bmp;tga;gif;psd"}};
    const std::string start = platform::pictures_folder().string();
    SDL_ShowOpenFileDialog(&window::chosen, this, handle, filters, 1, start.c_str(), false);
}

std::vector<std::string> window::take_pictures() {
    std::lock_guard lock(incoming_lock);
    std::vector<std::string> files;
    files.swap(incoming);
    if (!files.empty()) busy_frames = 2;
    return files;
}

void begin_screen() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("##screen", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void end_screen() { ImGui::End(); }

void begin_area(const std::string& id, float width, float height, bool framed) {
    ImGui::BeginChild(id.c_str(), ImVec2(width, height),
                      framed ? ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding : ImGuiChildFlags_None);
}

void end_area() { ImGui::EndChild(); }

void heading(const std::string& value) {
    if (heading_font) ImGui::PushFont(heading_font);
    ImGui::TextUnformatted(value.c_str());
    if (heading_font) ImGui::PopFont();
}

void text(const std::string& value) { ImGui::TextUnformatted(value.c_str()); }

void quiet(const std::string& value) {
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopStyleColor();
}

void wrapped(const std::string& value) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

void quiet_wrapped(const std::string& value) {
    ImGui::PushStyleColor(ImGuiCol_Text, muted);
    wrapped(value);
    ImGui::PopStyleColor();
}

void bad(const std::string& value) {
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(240, 110, 100));
    wrapped(value);
    ImGui::PopStyleColor();
}

bool button(const std::string& label) { return ImGui::Button(label.c_str()); }

bool primary_button(const std::string& label) {
    ImGui::PushStyleColor(ImGuiCol_Button, accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(24, 20, 16));
    bool clicked = ImGui::Button(label.c_str());
    ImGui::PopStyleColor(4);
    return clicked;
}

bool tab(const std::string& label, bool selected) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, rgb(84, 60, 34));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(96, 68, 38));
    }
    bool clicked = ImGui::Button(label.c_str());
    if (selected) ImGui::PopStyleColor(2);
    return clicked;
}

bool checkbox(const std::string& label, bool& value) { return ImGui::Checkbox(label.c_str(), &value); }

bool slider(const std::string& label, float& value, float minimum, float maximum, const char* format) {
    return ImGui::SliderFloat(label.c_str(), &value, minimum, maximum, format);
}

bool search(const std::string& hint, std::string& value, float width) {
    char buffer[256];
    std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    if (!ImGui::InputTextWithHint("##search", hint.c_str(), buffer, sizeof buffer)) return false;
    value = buffer;
    return true;
}

void same_line(float spacing) { ImGui::SameLine(0, spacing); }

void push_right(float width) {
    float target = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    if (target > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(target);
}

void space(float height) { ImGui::Dummy(ImVec2(0, height)); }
void separator() { ImGui::Separator(); }
float available_width() { return ImGui::GetContentRegionAvail().x; }
float available_height() { return ImGui::GetContentRegionAvail().y; }
float line_height() { return ImGui::GetFrameHeightWithSpacing(); }

float button_width(const std::string& label) {
    return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
}

bool disabled_begin(bool disabled) {
    if (disabled) ImGui::BeginDisabled();
    return disabled;
}

void disabled_end(bool disabled) {
    if (disabled) ImGui::EndDisabled();
}

void picture(const texture& value, float width, float height) {
    if (!value) {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }
    ImGui::Image(id_of(value), ImVec2(width, height));
}

bool picture_drag(const std::string& id, const texture& value, float width, float height,
                  float& moved_x, float& moved_y, float& wheel) {
    ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
    ImGui::GetWindowDrawList()->AddImage(id_of(value), corner, ImVec2(corner.x + width, corner.y + height));
    moved_x = moved_y = wheel = 0;
    bool changed = false;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        moved_x = ImGui::GetIO().MouseDelta.x;
        moved_y = ImGui::GetIO().MouseDelta.y;
        changed = moved_x != 0 || moved_y != 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        wheel = ImGui::GetIO().MouseWheel;
        changed = changed || wheel != 0;
    }
    return changed;
}

bool drop_zone(const std::string& id, const std::string& label, float width, float height) {
    ImVec2 corner = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImU32 line = ImGui::GetColorU32(ImGui::IsItemHovered() ? accent : rgb(80, 80, 88));
    draw->AddRect(corner, ImVec2(corner.x + width, corner.y + height), line, 10, 0, 2);
    ImVec2 size = ImGui::CalcTextSize(label.c_str(), nullptr, false, width - 40);
    ImVec2 at(corner.x + (width - size.x) / 2, corner.y + (height - size.y) / 2);
    draw->AddText(nullptr, 0, at, ImGui::GetColorU32(muted), label.c_str(), nullptr, width - 40);
    return clicked;
}

bool list_row(const std::string& id, const texture* thumbnail, const std::string& title,
              const std::string& detail, bool selected, float height) {
    ImVec2 corner = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::Selectable(("##" + id).c_str(), selected, ImGuiSelectableFlags_None, ImVec2(0, height));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float side = height - 8;
    ImVec2 box(corner.x + 4, corner.y + 4);
    draw->AddRectFilled(box, ImVec2(box.x + side, box.y + side), ImGui::GetColorU32(rgb(18, 18, 20)), 4);
    if (thumbnail && *thumbnail) {
        float aspect = float(thumbnail->width) / float(std::max(1, thumbnail->height));
        float w = aspect >= 1 ? side : side * aspect, h = aspect >= 1 ? side / aspect : side;
        ImVec2 at(box.x + (side - w) / 2, box.y + (side - h) / 2);
        draw->AddImage(id_of(*thumbnail), at, ImVec2(at.x + w, at.y + h));
    }
    const float text_x = box.x + side + 12;
    const float line = ImGui::GetTextLineHeight();
    draw->AddText(ImVec2(text_x, corner.y + height / 2 - line - 1), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    draw->AddText(ImVec2(text_x, corner.y + height / 2 + 1), ImGui::GetColorU32(muted), detail.c_str());
    return clicked;
}

void each_visible(int count, float row_height, const std::function<void(int)>& draw) {
    ImGuiListClipper clipper;
    clipper.Begin(count, row_height + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step())
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) draw(i);
}

bool pressed(key which) {
    switch (which) {
    case key::quit: return ImGui::IsKeyPressed(ImGuiKey_Q, false);
    case key::back: return ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    }
    return false;
}

bool typing() { return ImGui::GetIO().WantTextInput; }
bool pointer_down() { return ImGui::IsMouseDown(ImGuiMouseButton_Left); }

}
