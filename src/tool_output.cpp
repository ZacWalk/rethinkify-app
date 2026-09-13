// tool_output.cpp — Compiler, CMake, Ninja, CTest and PowerShell output parsing

#include "pch.h"
#include "tool_output.h"

namespace tools
{
	namespace
	{
		constexpr std::string_view digit_chars = "0123456789";
		constexpr std::string_view upper_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

		// A template instantiation can print pages of context; the rest stays in the output
		constexpr size_t max_notes = 32;

		std::string_view trim(std::string_view s)
		{
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
				s.remove_prefix(1);

			while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
				s.remove_suffix(1);

			return s;
		}

		bool all_digits(const std::string_view s)
		{
			return !s.empty() && s.find_first_not_of(digit_chars) == std::string_view::npos;
		}

		int to_int(const std::string_view s)
		{
			auto value = 0;
			std::from_chars(s.data(), s.data() + s.size(), value);
			return value;
		}

		// 'C2065', 'LNK1104' — uppercase letters then digits, and nothing else
		bool is_code(const std::string_view id)
		{
			const auto split = id.find_first_of(digit_chars);
			return split != std::string_view::npos && split != 0
				&& id.substr(0, split).find_first_not_of(upper_chars) == std::string_view::npos
				&& all_digits(id.substr(split));
		}

		struct location
		{
			std::string_view file;
			int line = 0;
			int column = 0;
			size_t rest = 0;
		};

		// 'dir\file.cpp(12,5):'. Only a parenthesis holding nothing but numbers and
		// followed by a colon counts, so 'Program Files (x86)' cannot be mistaken for one.
		bool msvc_location(const std::string_view s, location& out)
		{
			for (size_t i = 0; i < s.size(); ++i)
			{
				if (s[i] != '(')
					continue;

				const auto close = s.find(')', i);

				if (close == std::string_view::npos)
					break;

				const auto inside = s.substr(i + 1, close - i - 1);

				if (inside.empty() || inside.find_first_not_of("0123456789,") != std::string_view::npos)
					continue;

				auto j = close + 1;

				while (j < s.size() && s[j] == ' ')
					++j;

				if (j >= s.size() || s[j] != ':')
					continue;

				const auto comma = inside.find(',');
				out.file = s.substr(0, i);
				out.line = to_int(inside.substr(0, comma));
				out.column = comma == std::string_view::npos ? 0 : to_int(inside.substr(comma + 1));
				out.rest = j + 1;
				return true;
			}

			return false;
		}

		bool msvc_tail(std::string_view rest, severity& level, std::string& code, std::string& text)
		{
			rest = trim(rest);

			if (rest.starts_with("fatal error "))
			{
				level = severity::error;
				rest.remove_prefix(12);
			}
			else if (rest.starts_with("error "))
			{
				level = severity::error;
				rest.remove_prefix(6);
			}
			else if (rest.starts_with("warning "))
			{
				level = severity::warning;
				rest.remove_prefix(8);
			}
			else if (rest.starts_with("note: ") || rest.starts_with("message : "))
			{
				level = severity::note;
				code.clear();
				text = std::string(trim(rest.substr(rest.find(':') + 1)));
				return true;
			}
			else
			{
				return false;
			}

			const auto colon = rest.find(':');

			if (colon == std::string_view::npos)
				return false;

			const auto id = trim(rest.substr(0, colon));

			if (!id.empty() && !is_code(id))
				return false;

			code = std::string(id);
			text = std::string(trim(rest.substr(colon + 1)));
			return true;
		}

		bool msvc_diagnostic(const std::string_view s, diagnostic& d)
		{
			if (location loc; msvc_location(s, loc))
			{
				if (!msvc_tail(s.substr(loc.rest), d.level, d.code, d.text))
					return false;

				d.file = std::string(trim(loc.file));
				d.line = loc.line;
				d.column = loc.column;
				return true;
			}

			// 'LINK : fatal error LNK1104:' names no line, so the code has to carry it
			const auto sep = s.find(" : ");

			if (sep == std::string_view::npos || !msvc_tail(s.substr(sep + 3), d.level, d.code, d.text)
				|| d.code.empty())
				return false;

			d.file = std::string(trim(s.substr(0, sep)));
			return true;
		}

		struct clang_marker
		{
			std::string_view text;
			severity level;
		};

		constexpr clang_marker clang_markers[] = {
			{": fatal error: ", severity::error},
			{": error: ", severity::error},
			{": warning: ", severity::warning},
			{": note: ", severity::note},
			{": remark: ", severity::note},
		};

		bool clang_diagnostic(const std::string_view s, diagnostic& d)
		{
			for (const auto& marker : clang_markers)
			{
				const auto at = s.find(marker.text);

				if (at == std::string_view::npos)
					continue;

				auto head = s.substr(0, at);
				d.level = marker.level;
				d.text = std::string(trim(s.substr(at + marker.text.size())));

				// Split the trailing ':line:col' from the right, so a drive letter survives
				for (auto taken = 0; taken < 2; ++taken)
				{
					const auto colon = head.rfind(':');

					if (colon == std::string_view::npos)
						break;

					const auto number = head.substr(colon + 1);

					if (!all_digits(number))
						break;

					d.column = d.line;
					d.line = to_int(number);
					head = head.substr(0, colon);
				}

				d.file = std::string(trim(head));
				return true;
			}

			return false;
		}

		bool cmake_diagnostic(const std::string_view s, diagnostic& d)
		{
			if (s.starts_with("CMake Error"))
				d.level = severity::error;
			else if (s.starts_with("CMake Warning") || s.starts_with("CMake Deprecation Warning"))
				d.level = severity::warning;
			else
				return false;

			d.code = "CMake";

			const auto at = s.find(" at ");

			if (at == std::string_view::npos)
			{
				if (const auto colon = s.find(':'); colon != std::string_view::npos)
					d.text = std::string(trim(s.substr(colon + 1)));

				return true;
			}

			auto where = s.substr(at + 4);
			const auto paren = where.find(" (");
			where = trim(where.substr(0, paren == std::string_view::npos ? where.size() : paren));

			while (!where.empty() && where.back() == ':')
				where.remove_suffix(1);

			if (const auto colon = where.rfind(':');
				colon != std::string_view::npos && all_digits(where.substr(colon + 1)))
			{
				d.line = to_int(where.substr(colon + 1));
				where = where.substr(0, colon);
			}

			d.file = std::string(where);
			return true;
		}

		bool ninja_progress(const std::string_view s, std::string& out)
		{
			if (s.empty() || s.front() != '[')
				return false;

			const auto close = s.find(']');

			if (close == std::string_view::npos)
				return false;

			const auto inside = s.substr(1, close - 1);
			const auto slash = inside.find('/');

			if (slash == std::string_view::npos || !all_digits(inside.substr(0, slash))
				|| !all_digits(inside.substr(slash + 1)))
				return false;

			out = std::string(inside);
			return true;
		}

		bool ctest_failure(const std::string_view s, diagnostic& d)
		{
			const auto tag = s.find("Test #");

			if (tag == std::string_view::npos)
				return false;

			const auto colon = s.find(':', tag);

			if (colon == std::string_view::npos)
				return false;

			const auto stars = s.find("***", colon);

			if (stars == std::string_view::npos)
				return false;

			auto name = trim(s.substr(colon + 1, stars - colon - 1));

			while (!name.empty() && (name.back() == '.' || name.back() == ' '))
				name.remove_suffix(1);

			auto reason = trim(s.substr(stars + 3));

			if (const auto gap = reason.find("  "); gap != std::string_view::npos)
				reason = reason.substr(0, gap);

			d.level = severity::error;
			d.code = "CTest";
			d.text = std::format("{}: {}", name, reason);
			return true;
		}

		bool powershell_error(const std::string_view s, diagnostic& d)
		{
			const auto colon = s.find(": ");

			if (colon == 0 || colon == std::string_view::npos)
				return false;

			const auto name = s.substr(0, colon);

			if (name.size() > 64 || name.find(' ') != std::string_view::npos
				|| !(name.ends_with("Error") || name.ends_with("Exception")))
				return false;

			d.level = severity::error;
			d.code = "PowerShell";
			d.text = std::string(trim(s.substr(colon + 2)));
			return true;
		}

		// '   3 |  throw' and '     | boom' under a PowerShell error record
		bool powershell_continuation(const std::string_view body)
		{
			if (body == "Line |" || body.starts_with('|'))
				return true;

			const auto bar = body.find('|');
			return bar != std::string_view::npos && all_digits(trim(body.substr(0, bar)));
		}
	}

	std::string strip_ansi(const std::string_view text)
	{
		if (text.find('\x1b') == std::string_view::npos)
			return std::string(text);

		std::string out;
		out.reserve(text.size());

		for (size_t i = 0; i < text.size();)
		{
			if (text[i] != '\x1b')
			{
				out += text[i++];
				continue;
			}

			if (++i >= text.size())
				break;

			if (text[i] == '[')
			{
				++i;

				while (i < text.size() && static_cast<unsigned char>(text[i]) >= 0x20
					&& static_cast<unsigned char>(text[i]) <= 0x3f)
					++i;

				if (i < text.size())
					++i; // the final byte
			}
			else if (text[i] == ']')
			{
				++i;

				while (i < text.size())
				{
					if (text[i] == '\x07')
					{
						++i;
						break;
					}

					if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '\\')
					{
						i += 2;
						break;
					}

					++i;
				}
			}
			else
			{
				++i;
			}
		}

		return out;
	}

	diagnostic* output_parser::last()
	{
		return _diagnostics.empty() ? nullptr : &_diagnostics.back();
	}

	void output_parser::add(diagnostic d)
	{
		if (d.level == severity::note)
		{
			if (auto* owner = last())
			{
				owner->notes.push_back(d.file.empty() || d.line == 0
					                       ? d.text
					                       : std::format("{}({}): {}", d.file, d.line, d.text));
				return;
			}
		}

		if (!_pending_includes.empty())
		{
			d.notes.insert(d.notes.begin(), _pending_includes.begin(), _pending_includes.end());
			_pending_includes.clear();
		}

		_diagnostics.push_back(std::move(d));
	}

	void output_parser::add_line(const std::string_view raw)
	{
		const auto stripped = strip_ansi(raw);
		std::string_view s(stripped);

		while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
			s.remove_suffix(1);

		const auto body = trim(s);

		if (_block == block::cmake)
		{
			if (body.empty())
				return;

			if (s.front() == ' ' || s.front() == '\t')
			{
				if (auto* owner = last())
				{
					if (owner->text.empty())
						owner->text = std::string(body);
					else
						owner->notes.emplace_back(body);
				}

				return;
			}

			_block = block::none;
		}
		else if (_block == block::powershell)
		{
			if (powershell_continuation(body))
			{
				if (auto* owner = last(); owner && !body.empty())
					owner->notes.emplace_back(body);

				return;
			}

			_block = block::none;
		}
		else if (_block == block::indent)
		{
			if (!body.empty() && (s.front() == ' ' || s.front() == '\t'))
			{
				if (auto* owner = last(); owner && owner->notes.size() < max_notes)
					owner->notes.emplace_back(body);

				return;
			}

			_block = block::none;
		}

		if (body.empty())
			return;

		if (ninja_progress(body, _progress))
			return;

		if (body.starts_with("FAILED: "))
		{
			_failed_targets.emplace_back(trim(body.substr(8)));
			return;
		}

		if (body.starts_with("In file included from "))
		{
			_pending_includes.emplace_back(body);
			return;
		}

		// Each attempt gets a fresh value, so a partial match cannot leak into the next
		if (diagnostic d; cmake_diagnostic(body, d))
		{
			add(std::move(d));
			_block = block::cmake;
			return;
		}

		if (diagnostic d; msvc_diagnostic(body, d))
		{
			add(std::move(d));
			_block = block::indent;
			return;
		}

		if (diagnostic d; clang_diagnostic(body, d))
		{
			add(std::move(d));
			_block = block::indent;
			return;
		}

		if (diagnostic d; ctest_failure(body, d))
		{
			add(std::move(d));
			return;
		}

		if (diagnostic d; powershell_error(body, d))
		{
			add(std::move(d));
			_block = block::powershell;
		}
	}

	void output_parser::finish()
	{
		_block = block::none;
		_pending_includes.clear();
	}

	int output_parser::count(const severity level) const
	{
		return static_cast<int>(std::ranges::count(_diagnostics, level, &diagnostic::level));
	}
}
