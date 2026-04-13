#include "window-theming.h"

#include "config.h"
#include "feed.h"
#include "os.h"

namespace Theme
{
    void apply_border_color(OS::OSWindow window, Feed::MessageFeed* feed)
    {
        PROF_SCOPE();

        OS::Error err = OS::apply_window_border_color(window, Config::diff_colors().background);
        if (err != OS::Error::None)
        {
            feed->queue_warning(OS::error_text(err));
        }

        err = OS::apply_title_font_color(window, Config::widget_colors().window_title_font_color);
        if (err != OS::Error::None)
        {
            feed->queue_warning(OS::error_text(err));
        }
    }
} // namespace Theme