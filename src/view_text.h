// view_text.h — the shared text-view base, bound to this application's events.
//
// pf::ui::text_view knows fonts, scrolling, selection and the clipboard. What it
// deliberately does not know is what Escape means here, what the message bar says,
// or how zoom is applied — this subclass supplies those from app_events.

#pragma once

#include "ui.h"
#include "view_base.h"
#include "document.h"
#include "ui/view_text.h"

pf::color_t style_to_color(style style_index);

class text_view : public pf::ui::text_view
{
protected:
	app_events& _events;

public:
	text_view(app_events& events)
		: pf::ui::text_view(events, events.styles()), _events(events)
	{
	}

	// The agent pane reports the agent instead; see view_agent.h.
	[[nodiscard]] std::string_view status_text() const override { return _events.message_bar_text(); }

	void on_escape() override { _events.on_escape(); }

	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::text);
	}
};
