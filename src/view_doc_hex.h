// view_doc_hex.h — the shared hex view, under this application's name.

#pragma once

#include "view_doc_readonly.h"
#include "ui/view_hex.h"

using hex_doc_view = pf::ui::hex_view;

// app_events is at once the view's host, its theme and its context, so every view
// in this application is built from one object.
inline std::shared_ptr<hex_doc_view> make_hex_doc_view(app_events& events)
{
	return std::make_shared<hex_doc_view>(events, events.styles(), &events);
}
