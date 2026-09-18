// document.cpp — Document model implementation: text manipulation, undo/redo, file I/O

#include "pch.h"
#include "platform.h"
#include "app.h"
#include "document.h"


static void find_utf8_line_boundaries(const pf::ui::text_bytes_ptr& buffer, const int header_len,
                                      std::vector<pf::ui::text_line>& lines)
{
	const auto* data = buffer->data();
	const auto size = static_cast<uint32_t>(buffer->size());
	uint32_t line_start = static_cast<uint32_t>(header_len);

	for (uint32_t pos = line_start; pos < size; pos++)
	{
		const auto c = data[pos];

		if (c == '\r' || c == '\n')
		{
			const auto line_len = pos - line_start;
			lines.emplace_back(buffer, line_start, line_len);

			if (c == '\r' && pos + 1 < size && data[pos + 1] == '\n')
				pos++; // skip \n after \r

			line_start = pos + 1;
		}
	}

	// Last line (no trailing newline)
	const auto line_len = size - line_start;
	lines.emplace_back(buffer, line_start, line_len);
}

// Decode UTF-16 file bytes into UTF-8, so the line model only ever holds UTF-8.
// Decoding is loading, which is this application's job; platform-ui's buffer is
// defined as UTF-8 and never consults an encoding.
static std::vector<uint8_t> transcode_utf16_to_utf8(const std::vector<uint8_t>& data,
                                                    const int header_len, const bool is_be)
{
	const auto size = data.size();
	const auto start = static_cast<size_t>(header_len);
	const auto chars = start < size ? (size - start) / 2 : 0;

	std::wstring wide(chars, L'\0');
	const auto* src = reinterpret_cast<const uint16_t*>(data.data() + start);

	for (size_t i = 0; i < chars; i++)
		wide[i] = static_cast<wchar_t>(is_be ? _byteswap_ushort(src[i]) : src[i]);

	std::string utf8;
	pf::utf16_to_utf8(wide, utf8);
	return {utf8.begin(), utf8.end()};
}

static void find_binary_line_boundaries(const pf::ui::text_bytes_ptr& buffer,
                                        std::vector<pf::ui::text_line>& lines)
{
	const auto size = static_cast<uint32_t>(buffer->size());
	constexpr uint32_t bytes_per_line = 16;

	for (uint32_t pos = 0; pos < size; pos += bytes_per_line)
	{
		const auto line_len = std::min(bytes_per_line, size - pos);
		lines.emplace_back(buffer, pos, line_len);
	}
}

// Length of the longest prefix of data that ends on a complete UTF-8 sequence.
static uint32_t utf8_complete_prefix(const uint8_t* data, const uint32_t len)
{
	for (uint32_t back = 0; back < 4 && back < len; back++)
	{
		const auto c = data[len - 1 - back];
		if (pf::is_utf8_continuation(static_cast<char>(c)))
			continue;

		uint32_t seq_len = 1;
		if ((c & 0xE0) == 0xC0) seq_len = 2;
		else if ((c & 0xF0) == 0xE0) seq_len = 3;
		else if ((c & 0xF8) == 0xF0) seq_len = 4;

		return back + 1 >= seq_len ? len : len - back - 1;
	}

	return len;
}

loaded_file_data load_lines(const pf::file_path& path)
{
	loaded_file_data data;
	const auto hFile = pf::open_for_read(path);

	if (hFile != nullptr)
	{
		const auto file_size = hFile->size();
		const auto is_bin_extension = is_binary_extension(path);
		const auto read_size = std::min(static_cast<uint32_t>(file_size), max_document_load_size);
		data.truncated = file_size > max_document_load_size;

		const auto buffer = std::make_shared<std::vector<uint8_t>>();
		buffer->resize(read_size);

		// Read the file into the buffer (may need multiple reads)
		uint32_t total_read = 0;
		while (total_read < read_size)
		{
			uint32_t bytes_read = 0;
			if (!hFile->read(buffer->data() + total_read, read_size - total_read, &bytes_read) ||
				bytes_read == 0)
				break;
			total_read += bytes_read;
		}
		buffer->resize(total_read);

		const auto is_binary = is_binary_data({buffer->data(), total_read});

		if (is_binary || is_bin_extension)
		{
			data.encoding = file_encoding::binary;
			data.endings = line_endings::binary;

			find_binary_line_boundaries(buffer, data.lines);
		}
		else
		{
			int header_len = 0;
			const auto encoding = detect_encoding(buffer->data(), total_read, header_len);
			data.encoding = encoding;
			data.has_bom = header_len > 0;
			data.endings = detect_line_endings(buffer->data(), total_read);

			// A truncated read can stop mid-character — drop the partial tail
			if (data.truncated &&
				(encoding == file_encoding::utf8 || encoding == file_encoding::ascii))
			{
				buffer->resize(utf8_complete_prefix(buffer->data(), total_read));
			}

			if (encoding == file_encoding::utf16 || encoding == file_encoding::utf16be)
			{
				// The line model is UTF-8 only, so decode once here rather than on
				// every render. data.encoding keeps the original, so a save writes
				// the file back in the encoding it arrived in.
				*buffer = transcode_utf16_to_utf8(*buffer, header_len,
				                                       encoding == file_encoding::utf16be);
				header_len = 0;
			}

			find_utf8_line_boundaries(buffer, header_len, data.lines);
		}

		data.buffer = buffer;
		data.disk_modified_time = pf::file_modified_time(path);
	}

	return data;
}

// Which documents get spell checked is an application judgement, so it stays here
// rather than in the shared buffer. Prose is checked; code and data are not.
static bool is_spell_check_extension(const std::string_view ext)
{
	static const std::set<std::string_view, pf::iless> extensions = {
		"md", "txt"
	};

	return extensions.contains(ext);
}

bool should_spell_check_path(const pf::file_path& path)
{
	auto ext = path.extension();
	if (!ext.empty() && ext.starts_with(L'.')) ext = ext.substr(1);
	return is_spell_check_extension(ext);
}

document::document(document_events& events, const std::string_view text,
                   const bool is_modified,
                   const line_endings nCrlfStyle) :
	text_buffer(events),
	_events(events)
{
	_line_ending = nCrlfStyle;
	set_text(text);
	set_modified(is_modified);
}

document::document(document_events& events, const pf::file_path& path, const uint64_t disk_modified_time,
                   const file_encoding encoding) : text_buffer(events), _events(events), _path(path),
                                                   _disk_modified_time(disk_modified_time), _encoding(encoding)
{
}

void document::path(const pf::file_path& path_in)
{
	_path = path_in;
	_events.invalidate(invalid::app_title);
}

file_encoding detect_encoding(const uint8_t* header, const size_t filesize, int& headerLen)
{
	if (filesize >= 4 && header[0] == 0xFF && header[1] == 0xFE && header[2] == 0x00 && header[3] == 0x00)
	{
		headerLen = 4;
		return file_encoding::utf32;
	}
	if (filesize >= 4 && header[0] == 0x00 && header[1] == 0x00 && header[2] == 0xFE && header[3] == 0xFF)
	{
		headerLen = 4;
		return file_encoding::utf32be;
	}
	if (filesize >= 3 && header[0] == 0xEF && header[1] == 0xBB && header[2] == 0xBF)
	{
		headerLen = 3;
		return file_encoding::utf8;
	}
	if (filesize >= 2 && header[0] == 0xFF && header[1] == 0xFE)
	{
		headerLen = 2;
		return file_encoding::utf16;
	}
	if (filesize >= 2 && header[0] == 0xFE && header[1] == 0xFF)
	{
		headerLen = 2;
		return file_encoding::utf16be;
	}

	headerLen = 0;

	if (filesize >= 4 && header[0] != 0 && header[1] == 0 && header[2] != 0 && header[3] == 0)
		return file_encoding::utf16; // little-endian: char,0,char,0
	if (filesize >= 4 && header[0] == 0 && header[1] != 0 && header[2] == 0 && header[3] != 0)
		return file_encoding::utf16be; // big-endian: 0,char,0,char

	return file_encoding::utf8;
}

line_endings detect_line_endings(const uint8_t* buffer, const int len)
{
	for (int i = 0; i < len; i++)
	{
		if (buffer[i] == 0x0d)
		{
			// \r\n = DOS, \r alone = Mac
			if (i + 1 < len && buffer[i + 1] == 0x0a)
				return line_endings::crlf_style_dos;
			return line_endings::crlf_style_mac;
		}
		if (buffer[i] == 0x0a)
		{
			// \n alone = Unix
			return line_endings::crlf_style_unix;
		}
	}

	return line_endings::crlf_style_dos; // guess
}

file_lines_info iterate_file_lines(const pf::file_handle_ptr& handle,
                                   const std::function<void(const std::string&, int)>& on_line)
{
	file_lines_info info;

	constexpr uint32_t buf_size = 64 * 1024;
	uint8_t buffer[buf_size];
	uint32_t read_len = 0;

	if (!handle->read(buffer, buf_size, &read_len) || read_len == 0)
		return info;

	// Sniff the first block rather than making the caller reopen the file
	if (is_binary_data({buffer, read_len}))
	{
		info.enc = file_encoding::binary;
		return info;
	}

	int header_len = 0;
	info.enc = detect_encoding(buffer, read_len, header_len);
	info.endings = detect_line_endings(buffer, read_len);

	int line_number = 0;

	if (info.enc == file_encoding::utf8)
	{
		std::string utf8_line;
		auto pos = static_cast<uint32_t>(header_len);
		bool skip_next_lf = false;

		for (;;)
		{
			for (; pos < read_len; pos++)
			{
				const auto c = buffer[pos];

				if (skip_next_lf && c == '\n')
				{
					skip_next_lf = false;
					continue;
				}
				skip_next_lf = false;

				if (c == '\r' || c == '\n')
				{
					on_line(utf8_line, line_number);
					utf8_line.clear();
					line_number++;

					if (c == '\r')
						skip_next_lf = true;
				}
				else
				{
					utf8_line += static_cast<char>(c);
				}
			}

			if (!handle->read(buffer, buf_size, &read_len) || read_len == 0)
				break;
			pos = 0;
		}

		on_line(utf8_line, line_number);
	}
	else if (info.enc == file_encoding::utf16 || info.enc == file_encoding::utf16be)
	{
		const bool is_be = (info.enc == file_encoding::utf16be);
		const auto* buffer16 = reinterpret_cast<const uint16_t*>(buffer);
		read_len /= 2;
		auto pos = static_cast<uint32_t>(header_len / 2);
		bool skip_next_lf = false;
		uint16_t pending_lead = 0;

		std::string line;

		for (;;)
		{
			for (; pos < read_len; pos++)
			{
				auto c = buffer16[pos];
				if (is_be) c = _byteswap_ushort(c);

				if (pending_lead != 0)
				{
					if (pf::is_trail_surrogate(c))
					{
						const uint32_t cp = 0x10000 + ((static_cast<uint32_t>(pending_lead) - 0xD800) << 10) + (c -
							0xDC00);
						pf::char32_to_utf8(std::back_inserter(line), cp);
						pending_lead = 0;
						continue;
					}
					pending_lead = 0;
				}

				if (skip_next_lf && c == '\n')
				{
					skip_next_lf = false;
					continue;
				}
				skip_next_lf = false;

				if (c == '\r' || c == '\n')
				{
					on_line(line, line_number);
					line.clear();
					line_number++;

					if (c == '\r')
						skip_next_lf = true;
				}
				else if (pf::is_lead_surrogate(c))
				{
					pending_lead = c;
				}
				else
				{
					pf::char32_to_utf8(std::back_inserter(line), c);
				}
			}

			if (!handle->read(buffer, buf_size, &read_len) || read_len == 0)
				break;
			buffer16 = reinterpret_cast<const uint16_t*>(buffer);
			read_len /= 2;
			pos = 0;
		}

		on_line(line, line_number);
	}

	return info;
}

static std::string temp_file_path()
{
	return pf::platform_temp_file_path("rethinkify.");
}

static std::string last_error_message()
{
	return pf::platform_last_error_message();
}

bool document::save_to_file(const pf::file_path& path, const line_endings nCrlfStyle /*= CRLF_STYLE_AUTOMATIC*/,
                            const bool bClearModifiedFlag /*= true*/) const
{
	auto success = false;

	// A truncated document holds only the first part of the file, so writing it back would
	// destroy the rest
	if (_read_only || _is_truncated)
		return false;

	const auto tempPath = temp_file_path();
	const auto wpath = pf::utf8_to_utf16(tempPath);
	std::ofstream stream(wpath, std::ios::binary);

	if (stream)
	{
		auto effective_style = nCrlfStyle;
		if (effective_style == line_endings::crlf_style_automatic)
			effective_style = _line_ending == line_endings::crlf_style_automatic
				                  ? line_endings::crlf_style_dos
				                  : _line_ending;

		const char* eol;
		switch (effective_style)
		{
		case line_endings::crlf_style_unix: eol = "\n";
			break;
		case line_endings::crlf_style_mac: eol = "\r";
			break;
		default: eol = "\r\n";
			break;
		}

		auto first = true;

		std::string line_text;

		// A file that arrived as UTF-16 is written back as UTF-16 rather than silently re-encoded
		const bool utf16 = _encoding == file_encoding::utf16 || _encoding == file_encoding::utf16be;
		const bool big_endian = _encoding == file_encoding::utf16be;

		const auto write_text = [&](const std::string_view s)
		{
			if (!utf16)
			{
				stream.write(s.data(), s.size());
				return;
			}

			auto wide = pf::utf8_to_utf16(s);
			if (big_endian)
				for (auto& ch : wide)
					ch = static_cast<wchar_t>(ch >> 8 | ch << 8);
			stream.write(reinterpret_cast<const char*>(wide.data()),
			             static_cast<std::streamsize>(wide.size() * sizeof(wchar_t)));
		};

		if (_has_bom)
		{
			if (_encoding == file_encoding::utf16)
			{
				static constexpr uint8_t bom[2] = {0xFF, 0xFE};
				stream.write(reinterpret_cast<const char*>(bom), 2);
			}
			else if (_encoding == file_encoding::utf16be)
			{
				static constexpr uint8_t bom[2] = {0xFE, 0xFF};
				stream.write(reinterpret_cast<const char*>(bom), 2);
			}
			else
			{
				static constexpr uint8_t utf8_bom[3] = {0xEF, 0xBB, 0xBF};
				stream.write(reinterpret_cast<const char*>(utf8_bom), 3);
			}
		}

		for (const auto& line : _lines)
		{
			if (!first) write_text(eol);
			line.render(line_text);
			write_text(line_text);
			first = false;
		}

		stream.close();

		if (stream.fail())
			return false;

		success = pf::platform_move_file_replace(tempPath.c_str(), path.c_str());

		if (success && bClearModifiedFlag)
		{
			_modified = false;
			_saved_undo_pos = _undo_pos;
			_disk_modified_time = pf::file_modified_time(path);
		}

		if (!success)
		{
			pf::platform_show_error(last_error_message(), g_app_name);
		}
	}

	return success;
}


bool document::is_json() const
{
	std::string line_text;

	for (const auto& line : _lines)
	{
		line.render(line_text);

		for (const auto c : line_text)
		{
			if (c == u8'{') return true;
			if (c != u8' ' && c != u8'\n' && c != u8'\t' && c != u8'\r') return false;
		}
	}
	return false;
}

void document::reformat_json()
{
	if (!is_json())
		return;

	std::string result;
	std::string line_text;

	int tabs = 0, tokens = -1;
	bool in_string = false;
	bool escaped = false;

	for (const auto& line : _lines)
	{
		line.render(line_text);

		for (const auto ch : line_text)
		{
			if (in_string)
			{
				result += ch;

				if (escaped)
					escaped = false;
				else if (ch == u8'\\')
					escaped = true;
				else if (ch == u8'"')
					in_string = false;

				continue;
			}

			if (ch == u8'"')
			{
				in_string = true;
				result += ch;
			}
			else if (ch == u8'{')
			{
				tokens++;
				tabs = tokens;
				if (tokens > 0) result += u8'\n';
				while (tabs)
				{
					result += u8'\t';
					tabs--;
				}
				result += ch;
				result += u8'\n';
				tabs = tokens + 1;
				while (tabs)
				{
					result += u8'\t';
					tabs--;
				}
			}
			else if (ch == u8':')
			{
				result += " : ";
			}
			else if (ch == u8',')
			{
				result += ",\n";
				tabs = tokens + 1;
				while (tabs)
				{
					result += u8'\t';
					tabs--;
				}
			}
			else if (ch == u8'}')
			{
				tabs = tokens;
				result += u8'\n';
				while (tabs)
				{
					result += u8'\t';
					tabs--;
				}
				result += ch;
				result += u8'\n';
				tokens--;
				tabs = tokens + 1;
				while (tabs)
				{
					result += u8'\t';
					tabs--;
				}
			}
			else
			{
				if (ch == u8'\n' || ch == u8'\t') continue;
				result += ch;
			}
		}
	}

	undo_group ug(*this);
	select(replace_text(ug, all(), result));
}

void document::sort_remove_duplicates()
{
	std::vector<std::string> lines;
	lines.reserve(_lines.size());

	std::string line_text;
	for (const auto& line : _lines)
	{
		line.render(line_text);
		lines.emplace_back(line_text);
	}

	std::ranges::sort(lines);
	const auto [first, last] = std::ranges::unique(lines);
	lines.erase(first, last);

	undo_group ug(*this);
	select(replace_text(ug, all(), combine(lines, "\n")));
}

void document::apply_loaded_data(const pf::file_path& path, loaded_file_data data)
{
	if (data.disk_modified_time != 0)
	{
		std::swap(_lines, data.lines);
		_bytes = std::move(data.buffer);

		if (_lines.empty())
			append_line("");

		_is_truncated = data.truncated;
		_read_only = data.encoding == file_encoding::binary || data.truncated;
		_spell_check = should_spell_check_path(path);
		_encoding = data.encoding;
		_has_bom = data.has_bom;
		_line_ending = data.endings;
		_modified = false;
		_undo_pos = 0;
		_saved_undo_pos = 0;
		_path = path;
		_disk_modified_time = data.disk_modified_time;

		reset();
		_events.invalidate(invalid::doc | invalid::app_title);
	}
}

bool is_binary_extension(const pf::file_path& path)
{
	// is binary 
	static const std::unordered_set<std::string_view, pf::ihash, pf::ieq> binary_extensions = {
		".exe", ".dll", ".obj", ".lib", ".pdb", ".ilk", ".pch",
		".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico",
		".zip", ".7z", ".rar", ".tar", ".gz",
		".pdf", ".doc", ".docx", ".xls", ".xlsx",
		".mp3", ".mp4", ".avi", ".mov", ".wav",
		".ttf", ".otf", ".woff", ".woff2",
		".bin", ".dat", ".db", ".sqlite",
		".res", ".recipe",
	};
	return binary_extensions.contains(path.extension());
}

bool is_binary_data(const std::span<const uint8_t> buf)
{
	const auto readLen = buf.size();

	if (readLen == 0)
		return false;

	// Check for BOM signatures indicating a text encoding
	if (readLen >= 2)
	{
		if (buf[0] == 0xFF && buf[1] == 0xFE) return false; // UTF-16 LE (or UTF-32 LE)
		if (buf[0] == 0xFE && buf[1] == 0xFF) return false; // UTF-16 BE
	}
	if (readLen >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
		return false; // UTF-8 BOM

	// Check for UTF-16 without BOM (alternating null byte pattern)
	if (readLen >= 4)
	{
		if (buf[0] != 0 && buf[1] == 0 && buf[2] != 0 && buf[3] == 0) return false; // UTF-16 LE
		if (buf[0] == 0 && buf[1] != 0 && buf[2] == 0 && buf[3] != 0) return false; // UTF-16 BE
	}

	for (uint32_t i = 0; i < readLen; i++)
	{
		const auto c = buf[i];
		if (c == 0)
			return true;
		if (c < 8 && c != 7)
			return true;
	}

	return false;
}
