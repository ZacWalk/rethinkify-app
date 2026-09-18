// view_list.h — the shared list panel, under this application's name.
//
// The rows, selection, hover, keyboard handling and scrollbar live in
// pf::ui::list_view. What arrives here is what the application decides: where the
// zoom goes, and what Enter means once a row is chosen.
//
// A row's `data` is whatever the panel put there — an index_item for the folder
// browser, a search hit for the search panel. The shared list never looks inside it.

#pragma once

#include "ui.h"
#include "view_base.h"
#include "ui/view_list.h"

class list_view : public pf::ui::list_view
{
protected:
	app_events& _events;

public:
	explicit list_view(app_events& events) : pf::ui::list_view(events.styles(), &events), _events(events)
	{
	}

	~list_view() override = default;

protected:
	void zoom(const pf::window_frame_ptr& window, const int delta) override
	{
		_events.on_zoom(delta, zoom_target::list);
	}

	// Choosing a row here means "I have found it" — the editor takes focus next.
	void on_item_activated(const pf::window_frame_ptr& window) override
	{
		_events.set_focus(view_focus::text);
	}
};
