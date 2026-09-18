// view_doc_csv.h — the shared CSV table view, under this application's name.

#pragma once

#include "view_doc_readonly.h"
#include "ui/view_csv.h"

using csv_doc_view = pf::ui::csv_view;

// app_events is at once the view's host, its theme and its context, so every view
// in this application is built from one object.
inline std::shared_ptr<csv_doc_view> make_csv_doc_view(app_events& events)
{
	return std::make_shared<csv_doc_view>(events, events.styles(), &events);
}
