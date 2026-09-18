// app_views.cpp — the application policy the shared views ask for.
//
// pf::ui::view_context is what every platform-ui view consults for the things that
// are the same across views but differ between applications: what the status bar
// says, what Escape does, how zoom applies, and what a right-click offers. The
// first three are one-liners on app_state; the popup menu is this file.

#include "pch.h"

#include "ui.h"
#include "app.h"
#include "document.h"
#include "commands.h"
#include "view_doc.h"
#include "app_state.h"

std::vector<pf::menu_command> app_state::popup_menu_items(pf::ui::doc_view& view, const pf::ipoint& client_pt)
{
	std::vector<pf::menu_command> items;
	const auto doc = view.buffer();

	// A read-only pane can copy and select, and that is all it can do.
	if (!view.is_editable() || !doc)
	{
		items.push_back(command_menu_item(command_id::edit_copy));
		items.emplace_back(); // separator
		items.push_back(command_menu_item(command_id::edit_select_all));
		return items;
	}

	const auto active = active_item();
	auto definition = command_menu_item(command_id::nav_go_to_definition);

	if (active && active->doc == doc && definition.is_enabled && definition.is_enabled())
	{
		// Copy keeps the selection; navigation uses the name actually clicked within it.
		const auto target = view.text_at(client_pt);
		definition.action = [doc, target, action = std::move(definition.action)]
		{
			doc->select(target);
			action();
		};
		items.push_back(std::move(definition));
		items.push_back(command_menu_item(command_id::nav_switch_header_source));
		items.emplace_back();
	}

	// Spelling suggestions for the word under the cursor
	if (doc->spell_check())
	{
		const auto text_pos = view.text_at(client_pt);
		const auto word_start = doc->word_to_left(text_pos);
		const auto word_end = doc->word_to_right(text_pos);

		if (word_start != word_end && word_start.y == word_end.y)
		{
			std::string line_text;
			(*doc)[word_start.y].render(line_text);
			const auto word = line_text.substr(word_start.x, word_end.x - word_start.x);

			if (!word.empty() && !spell_check_word(word))
			{
				const text_selection word_sel(word_start, word_end);
				const auto suggestions = spell_suggest(word);

				for (const auto& suggestion : suggestions)
				{
					items.emplace_back(suggestion, 0,
					                   [doc, word_sel, s = suggestion]
					                   {
						                   doc->select(word_sel);
						                   undo_group ug(doc);
						                   doc->replace_text(ug, word_sel, s);
					                   });
				}

				if (!suggestions.empty())
					items.emplace_back(); // separator

				items.emplace_back("Add to Dictionary", 0,
				                   [this, w = word]
				                   {
					                   spell_add_word(w);
					                   invalidate(invalid::doc);
				                   });
				items.emplace_back(); // separator
			}
		}
	}

	// Edit commands
	items.push_back(command_menu_item(command_id::edit_undo));
	items.push_back(command_menu_item(command_id::edit_redo));
	items.emplace_back(); // separator
	items.push_back(command_menu_item(command_id::edit_cut));
	items.push_back(command_menu_item(command_id::edit_copy));
	items.push_back(command_menu_item(command_id::edit_paste));
	items.push_back(command_menu_item(command_id::edit_delete));
	items.emplace_back(); // separator
	items.push_back(command_menu_item(command_id::edit_select_all));

	return items;
}
