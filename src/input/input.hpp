#pragma once

#include <string>

struct GLFWwindow;

namespace input {

class InputState {
public:
    void attach(GLFWwindow* window);
    void update_mouse_state(GLFWwindow* window);
    void consume_frame();

    void append_typed(unsigned int codepoint);
    void set_backspace();
    void add_scroll(double yoffset);

    const std::string& typed() const;
    bool backspace() const;
    double scroll_delta() const;
    double mouse_x() const;
    double mouse_y() const;
    bool mouse_down() const;
    bool mouse_pressed() const;

private:
    std::string typed_;
    bool backspace_ = false;
    double scroll_accum_ = 0.0;
    double mouse_x_ = 0.0;
    double mouse_y_ = 0.0;
    bool mouse_down_ = false;
    bool mouse_pressed_ = false;
    bool prev_mouse_down_ = false;
};

} // namespace input
