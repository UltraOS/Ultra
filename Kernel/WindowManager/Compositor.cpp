#include "Compositor.h"
#include "Drivers/Video/VideoDevice.h"
#include "Screen.h"
#include "Window.h"
#include "WindowManager.h"

#include "Core/CPU.h"

#include "Time/Time.h"

namespace kernel {

Compositor* Compositor::s_instance;

void Compositor::initialize()
{
    ASSERT(s_instance == nullptr);

    s_instance = new Compositor;
}

Compositor::Compositor()
    : m_painter(new Painter(&Screen::the().surface()))
    , m_last_cursor_location(Screen::the().cursor().location())
{
    prepare_desktop();
    auto& cursor = Screen::the().cursor();
    m_painter->blit(m_last_cursor_location, cursor.bitmap(), cursor.rect());
}

void Compositor::compose()
{
    update_clock_widget();
    update_cursor_position();

    auto& windows = WindowManager::the().windows();

    for (auto& window : windows) {
        window.submit_dirty_rects();
    }

    for (auto& rect : m_dirty_rects) {
        m_painter->blit(rect.top_left(), WindowManager::the().desktop()->surface(), rect);

        for (auto itr = --windows.end(); itr != windows.end(); --itr) {
            auto& window = *itr;

            if (window.is_invalidated())
                continue;

            auto intersected_rect = window.full_translated_rect().intersected(rect);
            if (intersected_rect.empty())
                continue;

            m_painter->set_clip_rect(m_wallpaper_rect);
            m_painter->blit(intersected_rect.top_left(), window.surface(), intersected_rect.translated(-window.location()));
            m_painter->reset_clip_rect();
        }

        if (!m_cursor_invalidated)
            m_cursor_invalidated = rect.contains(m_last_cursor_location);
    }

    m_dirty_rects.clear();

    for (auto& window : windows) {
        if (window.is_invalidated()) {
            m_painter->set_clip_rect(m_wallpaper_rect);
            m_painter->blit(window.location(), window.surface(), window.full_rect());
            m_painter->reset_clip_rect();
        }

        window.set_invalidated(false);

        if (!m_cursor_invalidated)
            m_cursor_invalidated = window.full_translated_rect().contains(m_last_cursor_location);
    }

    if (m_cursor_invalidated) {
        m_cursor_invalidated = false;

        auto& cursor = Screen::the().cursor();

        m_painter->blit(m_last_cursor_location, cursor.bitmap(), cursor.rect());
    }
}

void Compositor::update_cursor_position()
{
    auto& cursor = Screen::the().cursor();
    auto location = cursor.location();

    if (location != m_last_cursor_location) {
        m_dirty_rects.emplace(cursor.rect().translated(m_last_cursor_location));
        m_cursor_invalidated = true;
        m_last_cursor_location = location;
    }
}

void Compositor::prepare_desktop()
{
    auto desktop_height = Screen::the().height() * 97 / 100;
    auto taskbar_height = Screen::the().height() - desktop_height;

    Rect desktop_rect(0, 0, Screen::the().width(), desktop_height);
    Rect taskbar_rect(0, desktop_height, Screen::the().width(), taskbar_height);

    m_wallpaper_rect = desktop_rect;
    m_wallpaper_rect.set_height(desktop_height);

    static constexpr size_t clock_widget_width = 100;
    static constexpr size_t clock_width_height = 16;

    Point clock_top_left(Screen::the().width() - clock_widget_width, taskbar_rect.center().y() - 8);
    m_clock_rect = Rect(clock_top_left, clock_widget_width, clock_width_height);

    auto& desktop_theme = WindowManager::the().desktop()->theme();

    Painter painter(&WindowManager::the().desktop()->surface());

    painter.fill_rect(desktop_rect, desktop_theme.desktop_background_color());
    painter.fill_rect(taskbar_rect, desktop_theme.taskbar_color());

    update_clock_widget();

    m_painter->draw_bitmap(WindowManager::the().desktop()->surface(), { 0, 0 });
}

void Compositor::update_clock_widget()
{
    static constexpr Color digit_color = Color::white();

    static Time::time_t last_drawn_second = 0;

    if (Time::now() == last_drawn_second)
        return;

    Painter painter(&WindowManager::the().desktop()->surface());

    last_drawn_second = Time::now();

    auto time = Time::now_readable();
    bool is_pm = false;

    if (time.hour >= 12) {
        is_pm = true;

        if (time.hour != 12)
            time.hour -= 12;
    } else if (time.hour == 0)
        time.hour = 12;

    auto current_x_offset = m_clock_rect.left();

    auto taskbar_color = WindowManager::the().active_theme()->taskbar_color();

    auto draw_digits = [&](char* digits, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            painter.draw_char({ current_x_offset, m_clock_rect.top() }, digits[i], digit_color, taskbar_color);
            current_x_offset += Painter::font_width;
        }
    };

    char digits[8];

    to_string(time.hour, digits, 8);
    if (time.hour < 10) {
        digits[1] = digits[0];
        digits[0] = '0';
    }

    digits[2] = ':';

    draw_digits(digits, 3);

    to_string(time.minute, digits, 8);
    if (time.minute < 10) {
        digits[1] = digits[0];
        digits[0] = '0';
    }
    digits[2] = ':';

    draw_digits(digits, 3);

    to_string(time.second, digits, 8);
    if (time.second < 10) {
        digits[1] = digits[0];
        digits[0] = '0';
    }
    digits[2] = ' ';

    draw_digits(digits, 3);

    digits[0] = is_pm ? 'P' : 'A';
    digits[1] = 'M';

    draw_digits(digits, 2);

    m_dirty_rects.emplace(m_clock_rect);
}
}
