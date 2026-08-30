#include "input/input.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace input {
namespace {
void append_utf8(std::string& out, unsigned int codepoint) {
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

void char_callback(GLFWwindow* window, unsigned int codepoint) {
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state) {
        state->append_typed(codepoint);
    }
}

void key_callback(GLFWwindow* window, int key, int, int action, int) {
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state && key == GLFW_KEY_BACKSPACE && action == GLFW_PRESS) {
        state->set_backspace();
    }
}

void scroll_callback(GLFWwindow* window, double, double yoffset) {
    auto* state = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (state) {
        state->add_scroll(yoffset);
    }
}
} // namespace

void InputState::attach(GLFWwindow* window) {
    glfwSetWindowUserPointer(window, this);
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback(window, scroll_callback);
}

void InputState::update_mouse_state(GLFWwindow* window) {
    glfwGetCursorPos(window, &mouse_x_, &mouse_y_);
    const bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    mouse_pressed_ = down && !prev_mouse_down_;
    prev_mouse_down_ = down;
    mouse_down_ = down;
}

void InputState::append_typed(unsigned int codepoint) {
    append_utf8(typed_, codepoint);
}

void InputState::set_backspace() {
    backspace_ = true;
}

void InputState::add_scroll(double yoffset) {
    scroll_accum_ += yoffset;
}

void InputState::consume_frame() {
    typed_.clear();
    backspace_ = false;
    scroll_accum_ = 0.0;
}

const std::string& InputState::typed() const { return typed_; }
bool InputState::backspace() const { return backspace_; }
double InputState::scroll_delta() const { return scroll_accum_; }
double InputState::mouse_x() const { return mouse_x_; }
double InputState::mouse_y() const { return mouse_y_; }
bool InputState::mouse_down() const { return mouse_down_; }
bool InputState::mouse_pressed() const { return mouse_pressed_; }

} // namespace input
