// view_agent_input.h — the agent prompt: the shared composer, under this
// application's name.
//
// The editing, the growing, the history, the filtering and what Enter means all
// live in pf::ui::composer now. What stays here is what this application decides:
// the font the agent pane uses, where zoom goes, and what Escape means — which is
// "go back to the editor", not the global Escape that closes a pane.

#pragma once

#include "view_doc_edit.h"
#include "ui/view_composer.h"

class agent_input_view final : public pf::ui::composer
{
	app_events& _events;

public:
	explicit agent_input_view(app_events& events)
		: composer(events, events.styles(), &events), _events(events)
	{
		set_placeholder("Message the agent, or /help");
	}

	~agent_input_view() override = default;

	[[nodiscard]] pf::font body_font() const override { return _events.styles().agent_font; }

	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::agent);
	}

	// Escape here only moves focus, so it is safe to press while the agent is
	// working — unlike the application's Escape, which closes whatever pane is open.
	void on_escape() override
	{
		_events.set_focus(view_focus::text);
	}
};

using agent_input_view_ptr = std::shared_ptr<agent_input_view>;
