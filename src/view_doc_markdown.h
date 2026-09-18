// view_doc_markdown.h — the shared markdown view, under this application's name.
//
// The 771-line fork that used to live here is now pf::ui::markdown_view: the same
// selectable source rendering, with the md:: model behind its layout and link hit
// testing. What a link means stays here, because that is application policy.

#pragma once

#include "view_doc_readonly.h"
#include "ui/view_markdown.h"

using markdown_doc_view = pf::ui::markdown_view;

// app_events is at once the view's host, its theme and its context, so every view
// in this application is built from one object.
inline std::shared_ptr<markdown_doc_view> make_markdown_doc_view(app_events& events)
{
	return std::make_shared<markdown_doc_view>(events, events.styles(), &events);
}
