// document_syntax.cpp — choosing a highlighter for a document.
//
// The scanners themselves live in platform-ui (ui/syntax.h). What stays here is the
// policy: which highlighter a document gets, given its type and its path. That is an
// application decision, so the shared layer does not make it.

#include "pch.h"
#include "document.h"

#include "ui/syntax.h"

highlight_fn select_highlighter(const doc_type type, const pf::file_path& path)
{
	switch (type)
	{
	case doc_type::hex:
	case doc_type::csv:
		// Both render their own text and never consult the highlighter
		return pf::ui::syntax::for_language(pf::ui::syntax::language::plain);
	case doc_type::markdown:
		return pf::ui::syntax::for_language(pf::ui::syntax::language::markdown);
	default:
		break;
	}

	return pf::ui::syntax::for_language(
		pf::ui::syntax::language_from_extension(path.extension()));
}
