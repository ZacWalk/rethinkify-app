// document.h — the application's document: a platform-ui text buffer that knows
// where it came from.
//
// The editing model itself lives in platform-ui (ui/text_buffer.h). What is added
// here is everything that makes a document a *file*: a path, an encoding, line
// endings, loading and saving, and the application commands that transform text.

#pragma once

#include "platform.h"
#include "ui/text_buffer.h"
#include "ui/text_types.h"

class doc_view;
class view_base;
class document_events;

// The text vocabulary lives in platform-ui, which owns the views that consume it.
// These aliases keep this header the place the document model names them from.
using text_location = pf::ui::text_location;
using text_selection = pf::ui::text_selection;
using style = pf::ui::text_style;
using text_block = pf::ui::text_block;
using highlight_fn = pf::ui::highlight_fn;
using document_line = pf::ui::text_line;
using undo_action = pf::ui::undo_action;
using undo_step = pf::ui::undo_step;
using undo_item = pf::ui::undo_item;
using undo_group = pf::ui::undo_group;

using pf::ui::invalid_length;
using pf::ui::invalid_cookie;

bool is_binary_extension(const pf::file_path& path);
bool is_binary_data(std::span<const uint8_t> data);

inline bool is_markdown_path(const pf::file_path& path)
{
	const auto ext = path.extension();
	if (ext.empty()) return false;
	const auto e = ext.starts_with('.') ? ext.substr(1) : ext;
	return pf::icmp(e, "md") == 0 || pf::icmp(e, "markdown") == 0;
}

inline bool is_csv_path(const pf::file_path& path)
{
	const auto ext = path.extension();
	if (ext.empty()) return false;
	const auto e = ext.starts_with('.') ? ext.substr(1) : ext;
	return pf::icmp(e, "csv") == 0;
}

bool should_spell_check_path(const pf::file_path& path);

enum class doc_type
{
	text,
	markdown,
	hex,
	csv,
};


enum class line_endings
{
	crlf_style_automatic = -1,
	crlf_style_dos = 0,
	crlf_style_unix = 1,
	crlf_style_mac = 2,
	binary,
};

enum class file_encoding
{
	utf32,
	utf32be,
	utf8,
	utf16be,
	utf16,
	ascii,
	binary,
};

constexpr uint32_t max_document_load_size = 2 * 1024 * 1024;

file_encoding detect_encoding(const uint8_t* header, size_t filesize, int& headerLen);
line_endings detect_line_endings(const uint8_t* buffer, int len);

struct file_lines_info
{
	file_encoding enc = file_encoding::utf8;
	line_endings endings = line_endings::crlf_style_dos;
};

file_lines_info iterate_file_lines(const pf::file_handle_ptr& handle,
                                   const std::function<void(const std::string&, int)>& on_line);

struct loaded_file_data
{
	std::vector<pf::ui::text_line> lines;
	pf::ui::text_bytes_ptr buffer;
	line_endings endings = line_endings::crlf_style_dos;
	file_encoding encoding = file_encoding::utf8;
	uint64_t disk_modified_time = 0;
	bool truncated = false;
	// Whether the file began with a byte-order mark, so a save can write it back.
	// This is a property of the file, not of the line storage.
	bool has_bom = false;
};

loaded_file_data load_lines(const pf::file_path& path);

class document : public pf::ui::text_buffer
{
	// The richer application interface. The base holds the same object as a
	// pf::ui::view_host; this keeps the application-only signals reachable.
	document_events& _events;

	pf::file_path _path;
	mutable uint64_t _disk_modified_time = 0;

	bool _has_bom = false;
	bool _is_truncated = false;
	line_endings _line_ending = line_endings::crlf_style_automatic;
	file_encoding _encoding = file_encoding::utf8;

	// Keeps the loaded bytes alive for as long as any line still points into them.
	pf::ui::text_bytes_ptr _bytes;

public:
	document(document_events& events, std::string_view text = {},
	         bool is_modified = false,
	         line_endings nCrlfStyle = line_endings::crlf_style_dos);
	document(document_events& events, const pf::file_path& path, uint64_t disk_modified_time,
	         file_encoding encoding);
	~document() override = default;

	void apply_loaded_data(const pf::file_path& path, loaded_file_data data);
	bool save_to_file(const pf::file_path& path, line_endings nCrlfStyle = line_endings::crlf_style_automatic,
	                  bool bClearModifiedFlag = true) const;

	[[nodiscard]] file_encoding encoding() const { return _encoding; }
	[[nodiscard]] bool is_truncated() const { return _is_truncated; }
	[[nodiscard]] uint64_t disk_modified_time() const { return _disk_modified_time; }
	[[nodiscard]] pf::file_path path() const { return _path; }
	void path(const pf::file_path& path_in);

	// Application commands that transform the whole document.
	[[nodiscard]] bool is_json() const;
	void reformat_json();
	void sort_remove_duplicates();

	// Which view a document opens in is decided from its path and encoding, so it
	// belongs here rather than in the shared buffer.
	[[nodiscard]] doc_type get_doc_type() const
	{
		if (_encoding == file_encoding::binary)
			return doc_type::hex;
		if (is_csv_path(_path))
			return doc_type::csv;
		if (is_markdown_path(_path))
			return doc_type::markdown;
		return doc_type::text;
	}
};

using document_ptr = std::shared_ptr<document>;

// Select the appropriate syntax highlighter based on file path/extension.
highlight_fn select_highlighter(doc_type type, const pf::file_path& path);

// Spell checking moved to platform-ui; these keep the existing call sites reading
// the same. Whether a document should be checked at all stays application policy.
inline bool is_spell_word_byte(const char ch) { return pf::ui::spell::is_word_byte(ch); }
inline bool spell_check_word(const std::string_view word) { return pf::ui::spell::check_word(word); }
inline std::vector<std::string> spell_suggest(const std::string_view word) { return pf::ui::spell::suggest(word); }
inline void spell_add_word(const std::string_view word) { pf::ui::spell::add_word(word); }
inline void reset_spell_checker() { pf::ui::spell::reset(); }
