#pragma once

#include "image/image.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;

namespace ui {

struct texture {
    unsigned id = 0;
    int width = 0;
    int height = 0;

    explicit operator bool() const { return id != 0; }
};

texture upload(const image::picture& picture);
void release(texture& value);

class window {
public:
    bool open(const std::string& title);
    void close();
    bool frame();
    void present();

    void choose_picture();
    std::vector<std::string> take_pictures();
    void keep_drawing() { busy_frames = 2; }
    void request_close() { closing = true; }

private:
    static void chosen(void* self, const char* const* files, int filter);

    SDL_Window* handle = nullptr;
    void* context = nullptr;
    bool closing = false;
    int busy_frames = 2;
    std::mutex incoming_lock;
    std::vector<std::string> incoming;
};

enum class key { quit, back };

void begin_screen();
void end_screen();
void begin_area(const std::string& id, float width, float height, bool framed);
void end_area();

void heading(const std::string& value);
void text(const std::string& value);
void quiet(const std::string& value);
void wrapped(const std::string& value);
void quiet_wrapped(const std::string& value);
void bad(const std::string& value);

bool button(const std::string& label);
bool primary_button(const std::string& label);
bool tab(const std::string& label, bool selected);
bool checkbox(const std::string& label, bool& value);
bool slider(const std::string& label, float& value, float minimum, float maximum, const char* format);
bool search(const std::string& hint, std::string& value, float width);

void same_line(float spacing = -1);
void push_right(float width);
void space(float height);
void separator();
float available_width();
float available_height();
float line_height();
float button_width(const std::string& label);
bool disabled_begin(bool disabled);
void disabled_end(bool disabled);

void picture(const texture& value, float width, float height);
bool picture_drag(const std::string& id, const texture& value, float width, float height,
                  float& moved_x, float& moved_y, float& wheel);
bool drop_zone(const std::string& id, const std::string& label, float width, float height);
bool list_row(const std::string& id, const texture* thumbnail, const std::string& title,
              const std::string& detail, bool selected, float height);
void each_visible(int count, float row_height, const std::function<void(int)>& draw);

bool pressed(key which);
bool typing();
bool pointer_down();

}
