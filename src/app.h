// app.h — Application types: event interfaces, search results, index items, app_events

#pragma once

#include "platform.h"
#include "ui/text_types.h"
#include "ui/theme.h"
#include "ui/view_host.h"

class document;
using document_ptr = std::shared_ptr<document>;
enum class command_id : int;

using text_location = pf::ui::text_location;

enum class view_content : int
{
	none,
	edit_text,
	markdown,
	hex,
	csv,
};

struct index_item;
using index_item_ptr = std::shared_ptr<index_item>;

struct search_result
{
	std::string line_text;
	int line_number = -1;
	int line_match_pos = -1;
	int text_match_start = -1;
	int text_match_length = 0;
};

constexpr int max_search_results = 5000;

struct create_path_result
{
	bool created = false;
	pf::file_path path;
	std::string name;

	[[nodiscard]] explicit operator bool() const { return created; }
};

struct index_item
{
	pf::file_path path;
	std::string name;
	bool is_folder = false;
	bool is_deleted = false;
	view_content saved_view_content = view_content::none;
	document_ptr doc;
	std::vector<index_item_ptr> children;
	std::vector<search_result> search_results;

	// Bumped by each async load so a stale read can be discarded when it completes
	uint32_t load_generation = 0;

	// Ordering for document eviction; higher is more recently opened
	uint64_t last_used = 0;

	index_item() = default;

	index_item(pf::file_path p, std::string n, const bool folder,
	           document_ptr d = nullptr)
		: path(std::move(p)), name(std::move(n)), is_folder(folder), doc(std::move(d))
	{
	}
};

namespace invalid
{
	constexpr auto app_title = 1 << 0;
	constexpr auto doc_layout = 1 << 2;
	constexpr auto doc_caret = 1 << 3;
	constexpr auto doc_scrollbar = 1 << 4;
	constexpr auto windows = 1 << 6;
	constexpr auto doc = doc_scrollbar | doc_layout | doc_caret;

	constexpr auto files_layout = 1 << 7;
	constexpr auto search_layout = 1 << 8;
	constexpr auto files_populate = 1 << 9;
	constexpr auto search_populate = 1 << 11;
	constexpr auto index = 1 << 15;
	constexpr auto agent_layout = 1 << 16;
}

enum class view_mode
{
	edit_text_files,
	edit_text_search,
	markdown_files,
	markdown_search,
	hex_files,
	hex_search,
	csv_files,
	csv_search,
};

inline view_content view_content_of(const view_mode m)
{
	switch (m)
	{
	case view_mode::edit_text_files:
	case view_mode::edit_text_search:
		return view_content::edit_text;
	case view_mode::markdown_files:
	case view_mode::markdown_search:
		return view_content::markdown;
	case view_mode::hex_files:
	case view_mode::hex_search:
		return view_content::hex;
	case view_mode::csv_files:
	case view_mode::csv_search:
		return view_content::csv;
	}

	return view_content::edit_text;
}

inline bool is_search(const view_mode m)
{
	return m == view_mode::edit_text_search || m == view_mode::markdown_search || m == view_mode::hex_search || m ==
		view_mode::csv_search;
}

inline view_mode make_view_mode(const view_content content, const bool search)
{
	switch (content)
	{
	case view_content::edit_text:
		return search ? view_mode::edit_text_search : view_mode::edit_text_files;
	case view_content::markdown:
		return search ? view_mode::markdown_search : view_mode::markdown_files;
	case view_content::hex:
		return search ? view_mode::hex_search : view_mode::hex_files;
	case view_content::csv:
		return search ? view_mode::csv_search : view_mode::csv_files;
	}

	return search ? view_mode::edit_text_search : view_mode::edit_text_files;
}

inline view_mode with_search(const view_mode mode, const bool search)
{
	return make_view_mode(view_content_of(mode), search);
}

inline view_mode with_view_content(const view_mode mode, const view_content content)
{
	return make_view_mode(content, is_search(mode));
}

inline bool is_markdown(const view_mode m) { return view_content_of(m) == view_content::markdown; }
inline bool is_hex(const view_mode m) { return view_content_of(m) == view_content::hex; }
inline bool is_csv(const view_mode m) { return view_content_of(m) == view_content::csv; }

inline bool is_edit_text(const view_mode m)
{
	return view_content_of(m) == view_content::edit_text;
}

enum class view_focus
{
	text,
	list,
	agent,
};

enum class spell_check_mode
{
	auto_detect,
	disabled,
	enabled,
};

// document_events — Narrow interface for document-to-host notifications.
// The document model depends only on this, not the full app_events.
//
// The line-level half is pf::ui::view_host, so a platform-ui view can be hosted
// here without knowing what an `invalid::` bit is.
class document_events : public pf::ui::view_host
{
public:
	virtual void invalidate(uint32_t i) = 0;

	// view_host's neutral spelling of "redraw everything about the document".
	void invalidate_view() override { invalidate(invalid::doc); }
};

// view_styles — the app's theme. The fonts, metrics and palette come from
// pf::ui::theme; this adds only what the agent pane needs on top.
struct view_styles : pf::ui::theme
{
	int agent_font_height = 20;
	pf::font agent_font = {20, pf::font_name::consolas};
};

enum class zoom_target { text, list, agent };

// app_events — Full application event interface used by views and panels.
class app_events : public document_events
{
public:
	virtual std::string_view message_bar_text() const = 0;
	virtual std::string_view agent_status_text() const = 0;
	virtual index_item_ptr root_item() const = 0;
	virtual index_item_ptr active_item() const = 0;
	virtual const view_styles& styles() const = 0;

	virtual void set_focus(view_focus v) = 0;
	virtual void set_mode(view_mode m) = 0;

	virtual void path_selected(const index_item_ptr& item) = 0;
	virtual bool is_path_modified(const index_item_ptr& item) const = 0;
	virtual void on_escape() = 0;
	virtual void open_path_and_select(const index_item_ptr& item, int line, int col, int length) = 0;

	virtual void on_search(const std::string& text) = 0;
	virtual void on_zoom(int delta, zoom_target target) = 0;
	virtual pf::menu_command command_menu_item(command_id id,
	                                           std::function<void()> action_override = nullptr,
	                                           std::function<bool()> is_enabled_override = nullptr,
	                                           std::function<bool()> is_checked_override = nullptr,
	                                           std::string text_override = {}) const = 0;

	virtual pf::file_path save_folder() const = 0;
	virtual void copy_files_to_folder(const std::vector<pf::file_path>& sources, const pf::file_path& dest_folder) = 0;
	virtual void delete_item(const index_item_ptr& item) = 0;
	virtual void rename_item(const index_item_ptr& item, const std::string& new_name) = 0;
	virtual create_path_result create_new_file(const pf::file_path& folder, std::string content) = 0;
	virtual create_path_result create_new_folder(const pf::file_path& folder) = 0;
};

// async_scheduler — Abstraction for background/UI task scheduling.
// Allows app_state to be tested with synchronous execution.
class async_scheduler
{
public:
	virtual ~async_scheduler() = default;
	virtual void run_async(std::function<void()> task) = 0;
	virtual void run_ui(std::function<void()> task) = 0;
};

using async_scheduler_ptr = std::shared_ptr<async_scheduler>;
