// ui.h — the shared platform-ui widgets, under the names this application uses.
//
// The implementations moved to platform-h/src/ui. This header re-exports them so
// existing call sites keep reading the same; it should shrink to nothing as the
// views are migrated and start naming pf::ui directly.

#pragma once

#include "ui/ui.h"
#include "ui/view_list.h"

namespace ui = pf::ui::colors;
namespace table_layout = pf::ui::table_layout;

using pf::ui::caret_blinker;
using pf::ui::custom_scrollbar;
using pf::ui::edit_box;
using pf::ui::edit_box_widget;
using pf::ui::splitter;

// A panel row. The panels hang their own object off its `data`.
using list_view_item = pf::ui::list_item;
using list_view_item_ptr = pf::ui::list_item_ptr;
