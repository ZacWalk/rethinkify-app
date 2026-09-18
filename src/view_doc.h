// view_doc.h — the shared document view, under the name this application uses.

#pragma once

#include "view_text.h"
#include "commands.h"
#include "ui/view_doc.h"

using doc_view = pf::ui::doc_view;

// Choosing a highlighter depends on where a document came from, which the shared
// view does not know. Every call site that shows a document pairs them here.
inline pf::ui::highlight_fn highlight_for(const document_ptr& d)
{
	return select_highlighter(d ? d->get_doc_type() : doc_type::text,
	                          d ? d->path() : pf::file_path{});
}
