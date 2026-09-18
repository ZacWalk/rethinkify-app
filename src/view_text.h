// view_text.h — the shared text-view base, under the name this application uses.
//
// The application policy a view needs — the message bar text, what Escape does,
// how zoom is applied, what a right-click offers — arrives through
// pf::ui::view_context, which app_events implements. Views are therefore the
// shared ones, not subclasses of them.

#pragma once

#include "ui.h"
#include "view_base.h"
#include "document.h"
#include "ui/view_text.h"

using text_view = pf::ui::text_view;
