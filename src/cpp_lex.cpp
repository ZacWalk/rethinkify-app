// cpp_lex.cpp — C++ token scanner

#include "pch.h"
#include "cpp_lex.h"

namespace cpp
{
	namespace
	{
		struct keyword_entry
		{
			std::string_view name;
			keyword kw;
		};

		// Sorted; searched with lower_bound
		constexpr keyword_entry keyword_table[] = {
			{"alignas", keyword::alignas_},
			{"alignof", keyword::alignof_},
			{"and", keyword::other},
			{"and_eq", keyword::other},
			{"asm", keyword::other},
			{"auto", keyword::auto_},
			{"bitand", keyword::other},
			{"bitor", keyword::other},
			{"bool", keyword::other},
			{"break", keyword::other},
			{"case", keyword::other},
			{"catch", keyword::other},
			{"char", keyword::other},
			{"char16_t", keyword::other},
			{"char32_t", keyword::other},
			{"char8_t", keyword::other},
			{"class", keyword::class_},
			{"co_await", keyword::other},
			{"co_return", keyword::other},
			{"co_yield", keyword::other},
			{"compl", keyword::other},
			{"concept", keyword::concept_},
			{"const", keyword::const_},
			{"const_cast", keyword::other},
			{"consteval", keyword::consteval_},
			{"constexpr", keyword::constexpr_},
			{"constinit", keyword::constinit_},
			{"continue", keyword::other},
			{"decltype", keyword::decltype_},
			{"default", keyword::other},
			{"delete", keyword::other},
			{"do", keyword::other},
			{"double", keyword::other},
			{"dynamic_cast", keyword::other},
			{"else", keyword::other},
			{"enum", keyword::enum_},
			{"explicit", keyword::explicit_},
			{"export", keyword::export_},
			{"extern", keyword::extern_},
			{"false", keyword::other},
			{"float", keyword::other},
			{"for", keyword::other},
			{"friend", keyword::friend_},
			{"goto", keyword::other},
			{"if", keyword::other},
			{"inline", keyword::inline_},
			{"int", keyword::other},
			{"long", keyword::other},
			{"mutable", keyword::mutable_},
			{"namespace", keyword::namespace_},
			{"new", keyword::other},
			{"noexcept", keyword::noexcept_},
			{"not", keyword::other},
			{"not_eq", keyword::other},
			{"nullptr", keyword::other},
			{"operator", keyword::operator_},
			{"or", keyword::other},
			{"or_eq", keyword::other},
			{"private", keyword::private_},
			{"protected", keyword::protected_},
			{"public", keyword::public_},
			{"register", keyword::other},
			{"reinterpret_cast", keyword::other},
			{"requires", keyword::requires_},
			{"return", keyword::return_},
			{"short", keyword::other},
			{"signed", keyword::other},
			{"sizeof", keyword::other},
			{"static", keyword::static_},
			{"static_assert", keyword::static_assert_},
			{"static_cast", keyword::other},
			{"struct", keyword::struct_},
			{"switch", keyword::other},
			{"template", keyword::template_},
			{"this", keyword::other},
			{"thread_local", keyword::other},
			{"throw", keyword::other},
			{"true", keyword::other},
			{"try", keyword::other},
			{"typedef", keyword::typedef_},
			{"typeid", keyword::other},
			{"typename", keyword::typename_},
			{"union", keyword::union_},
			{"unsigned", keyword::other},
			{"using", keyword::using_},
			{"virtual", keyword::virtual_},
			{"void", keyword::other},
			{"volatile", keyword::other},
			{"wchar_t", keyword::other},
			{"while", keyword::other},
			{"xor", keyword::other},
			{"xor_eq", keyword::other},
		};

		static_assert([]
		{
			for (size_t i = 1; i < std::size(keyword_table); ++i)
				if (!(keyword_table[i - 1].name < keyword_table[i].name))
					return false;
			return true;
		}(), "keyword_table must be sorted for lower_bound");

		// '>>' is absent on purpose: emitting two '>' lets the declaration parser
		// balance angle brackets in 'vector<vector<int>>' without a special case.
		// '<<' stays whole, so a shift is never mistaken for two openers.
		constexpr std::string_view punct_table[] = {
			"<=>", "...", "<<=", ">>=", "->*",
			"::", "->", "++", "--", "<<", "<=", ">=", "==", "!=", "&&", "||",
			"+=", "-=", "*=", "/=", "%=", "^=", "&=", "|=", ".*", "##",
		};

		constexpr std::string_view string_prefixes[] = {"u8", "u", "U", "L", "R", "u8R", "uR", "UR", "LR"};
		constexpr std::string_view char_prefixes[] = {"u8", "u", "U", "L"};

		constexpr size_t max_raw_delimiter = 16;

		bool is_digit(const char c)
		{
			return c >= '0' && c <= '9';
		}

		// Bytes at or above 0x80 are identifier bytes, matching how the rest of the
		// editor treats UTF-8 words
		bool is_ident_start(const char c)
		{
			const auto u = static_cast<unsigned char>(c);
			return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u == '$' || u >= 0x80;
		}

		bool is_ident_char(const char c)
		{
			return is_ident_start(c) || is_digit(c);
		}

		bool contains(const std::span<const std::string_view> set, const std::string_view text)
		{
			return std::ranges::find(set, text) != set.end();
		}

		uint32_t count_lines(const std::string_view text)
		{
			return static_cast<uint32_t>(std::ranges::count(text, '\n'));
		}
	}

	keyword keyword_of(const std::string_view text)
	{
		const auto it = std::ranges::lower_bound(keyword_table, text, {}, &keyword_entry::name);
		return it != std::end(keyword_table) && it->name == text ? it->kw : keyword::none;
	}

	bool is_keyword(const std::string_view text)
	{
		return keyword_of(text) != keyword::none;
	}

	lexer::lexer(const std::string_view src) : _src(src)
	{
		if (_src.starts_with("\xEF\xBB\xBF"))
			_pos = 3;
	}

	char lexer::peek(const size_t ahead) const
	{
		const auto at = _pos + ahead;
		return at < _src.size() ? _src[at] : '\0';
	}

	size_t lexer::splice_end(size_t pos) const
	{
		if (pos >= _src.size() || _src[pos] != '\\')
			return 0;

		++pos;

		// Trailing blanks after the backslash are tolerated, as the compilers do
		while (pos < _src.size() && (_src[pos] == ' ' || _src[pos] == '\t' || _src[pos] == '\r'))
			++pos;

		return pos < _src.size() && _src[pos] == '\n' ? pos + 1 : 0;
	}

	void lexer::skip_trivia()
	{
		while (_pos < _src.size())
		{
			const auto c = _src[_pos];

			if (c == '\n')
			{
				++_pos;
				++_line;
				_line_start = _pos;
				_at_line_start = true;
				_in_directive = false; // only an unspliced newline ends a directive
				continue;
			}

			if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f')
			{
				++_pos;
				continue;
			}

			if (const auto after = splice_end(_pos))
			{
				_pos = after;
				++_line;
				_line_start = _pos;
				_at_line_start = true;
				continue;
			}

			break;
		}
	}

	void lexer::scan_line_comment()
	{
		_pos += 2;

		while (_pos < _src.size() && _src[_pos] != '\n')
		{
			if (const auto after = splice_end(_pos))
				_pos = after; // a spliced line comment swallows the next line too
			else
				++_pos;
		}
	}

	void lexer::scan_block_comment()
	{
		_pos += 2;

		while (_pos < _src.size())
		{
			if (_src[_pos] == '*' && peek(1) == '/')
			{
				_pos += 2;
				return;
			}

			++_pos;
		}
	}

	void lexer::scan_ident()
	{
		while (_pos < _src.size() && is_ident_char(_src[_pos]))
			++_pos;
	}

	void lexer::scan_number()
	{
		if (_src[_pos] == '.')
			++_pos;

		while (_pos < _src.size())
		{
			const auto c = _src[_pos];

			if ((c == 'e' || c == 'E' || c == 'p' || c == 'P') && (peek(1) == '+' || peek(1) == '-'))
			{
				_pos += 2;
				continue;
			}

			if (is_ident_char(c) || c == '.')
			{
				++_pos;
				continue;
			}

			// A quote between digits is a separator, not the start of a character literal
			if (c == '\'' && is_ident_char(peek(1)))
			{
				_pos += 2;
				continue;
			}

			break;
		}
	}

	void lexer::scan_quoted(const char quote)
	{
		++_pos;

		while (_pos < _src.size())
		{
			const auto c = _src[_pos];

			if (const auto after = splice_end(_pos))
			{
				_pos = after;
				continue;
			}

			if (c == '\\' && _pos + 1 < _src.size())
			{
				_pos += 2;
				continue;
			}

			if (c == quote)
			{
				++_pos;
				return;
			}

			if (c == '\n')
				return; // unterminated; leave the newline for skip_trivia

			++_pos;
		}
	}

	void lexer::scan_raw_string()
	{
		const auto quote = _pos;
		++_pos;

		const auto delim_start = _pos;

		while (_pos < _src.size() && _pos - delim_start <= max_raw_delimiter)
		{
			const auto c = _src[_pos];

			if (c == '(')
				break;

			if (c == ')' || c == '\\' || c == '"' || c == ' ' || c == '\t' || c == '\n' || c == '\r')
			{
				_pos = quote; // malformed delimiter, so read it as an ordinary string
				scan_quoted('"');
				return;
			}

			++_pos;
		}

		if (_pos >= _src.size() || _src[_pos] != '(')
		{
			_pos = quote;
			scan_quoted('"');
			return;
		}

		const std::string_view delim = _src.substr(delim_start, _pos - delim_start);
		++_pos;

		while (_pos < _src.size())
		{
			if (_src[_pos] == ')' && _pos + 1 + delim.size() < _src.size()
				&& _src[_pos + 1 + delim.size()] == '"'
				&& _src.compare(_pos + 1, delim.size(), delim) == 0)
			{
				_pos += delim.size() + 2;
				return;
			}

			++_pos;
		}
	}

	void lexer::scan_punct()
	{
		const auto rest = _src.substr(_pos);

		for (const auto op : punct_table)
		{
			if (rest.starts_with(op))
			{
				_pos += op.size();
				return;
			}
		}

		++_pos;
	}

	token lexer::next()
	{
		skip_trivia();

		token t;
		t.offset = static_cast<uint32_t>(_pos);
		t.line = _line;
		t.column = static_cast<uint32_t>(_pos - _line_start);
		t.first_on_line = _at_line_start;
		t.in_directive = _in_directive;

		if (_pos >= _src.size())
			return t;

		const auto start = _pos;
		const auto c = _src[_pos];
		_at_line_start = false;

		if (c == '/' && peek(1) == '/')
		{
			scan_line_comment();
			t.type = token_type::comment;
		}
		else if (c == '/' && peek(1) == '*')
		{
			scan_block_comment();
			t.type = token_type::comment;
		}
		else if (is_ident_start(c))
		{
			scan_ident();
			const auto id = _src.substr(start, _pos - start);
			const auto after = peek();

			if (after == '"' && contains(string_prefixes, id))
			{
				if (id.back() == 'R')
					scan_raw_string();
				else
					scan_quoted('"');

				t.type = token_type::string;
			}
			else if (after == '\'' && contains(char_prefixes, id))
			{
				scan_quoted('\'');
				t.type = token_type::character;
			}
			else
			{
				t.type = token_type::identifier;
				t.kw = keyword_of(id);
			}
		}
		else if (is_digit(c) || (c == '.' && is_digit(peek(1))))
		{
			scan_number();
			t.type = token_type::number;
		}
		else if (c == '"')
		{
			scan_quoted('"');
			t.type = token_type::string;
		}
		else if (c == '\'')
		{
			scan_quoted('\'');
			t.type = token_type::character;
		}
		else if (c == '#' && t.first_on_line && !_in_directive)
		{
			++_pos;
			_in_directive = true;
			t.in_directive = true;
			t.type = token_type::punct;
		}
		else
		{
			scan_punct();
			t.type = token_type::punct;
		}

		t.text = _src.substr(start, _pos - start);

		if (t.type == token_type::comment)
			_at_line_start = t.first_on_line;

		if (t.type == token_type::comment || t.type == token_type::string || t.type == token_type::character)
		{
			if (const auto newlines = count_lines(t.text); newlines > 0)
			{
				_line += newlines;
				_line_start = start + t.text.rfind('\n') + 1;
			}
		}

		return t;
	}

	std::vector<token> tokenize(const std::string_view src)
	{
		std::vector<token> result;
		lexer lex(src);

		for (auto t = lex.next(); t.type != token_type::end; t = lex.next())
			result.push_back(t);

		return result;
	}
}
