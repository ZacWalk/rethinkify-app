// cpp_index.cpp — Declaration parser and symbol table

#include "pch.h"
#include "cpp_index.h"

namespace cpp
{
	std::string_view to_string(const symbol_kind kind)
	{
		switch (kind)
		{
		case symbol_kind::name_space: return "namespace";
		case symbol_kind::class_type: return "class";
		case symbol_kind::struct_type: return "struct";
		case symbol_kind::union_type: return "union";
		case symbol_kind::enum_type: return "enum";
		case symbol_kind::enumerator: return "enumerator";
		case symbol_kind::function: return "function";
		case symbol_kind::variable: return "variable";
		case symbol_kind::type_alias: return "alias";
		case symbol_kind::concept_type: return "concept";
		case symbol_kind::macro: return "macro";
		default: return "unknown";
		}
	}

	name_table::name_table()
	{
		const auto [it, added] = _ids.emplace(std::string{}, 0u);
		_texts.push_back(&it->first);
	}

	uint32_t name_table::id(const std::string_view text)
	{
		if (text.empty())
			return 0;

		const auto next = static_cast<uint32_t>(_texts.size());
		const auto [it, added] = _ids.emplace(text, next);

		if (added)
			_texts.push_back(&it->first);

		return it->second;
	}

	uint32_t name_table::find(const std::string_view text) const
	{
		const auto it = _ids.find(std::string(text));
		return it == _ids.end() ? 0 : it->second;
	}

	std::string_view name_table::text(const uint32_t id) const
	{
		return id < _texts.size() ? std::string_view(*_texts[id]) : std::string_view{};
	}

	namespace
	{
		// A parenthesis after one of these is an attribute or an operator, never a
		// parameter list, so it must not name a function
		bool is_transparent_call(const std::string_view name)
		{
			static constexpr std::string_view words[] = {
				"alignas", "alignof", "decltype", "noexcept", "sizeof", "static_assert",
				"explicit", "throw", "requires", "if", "while", "switch", "for", "return",
			};

			return name.starts_with("__") || std::ranges::find(words, name) != std::end(words);
		}

		struct scope_entry
		{
			std::string name; // empty for anonymous or transparent scopes
			symbol_kind kind = symbol_kind::unknown;
			int depth = 0; // brace depth the body opened at
		};

		class declaration_parser
		{
		public:
			declaration_parser(const std::string_view source, const uint32_t file, name_table& names)
				: _lex(source), _names(names), _file(file)
			{
			}

			std::vector<symbol> run()
			{
				for (auto t = _lex.next(); t.type != token_type::end; t = _lex.next())
				{
					if (t.type == token_type::comment)
						continue;

					if (t.in_directive)
						directive_token(t);
					else
						code_token(t);
				}

				return std::move(_symbols);
			}

		private:
			void emit(const std::string_view name, const std::string_view extra_scope, const symbol_kind kind,
			          const uint32_t offset, const uint32_t line, const uint32_t column, const uint8_t flags)
			{
				if (name.empty())
					return;

				symbol s;
				s.name = _names.id(name);
				s.scope = _names.id(scope_text(extra_scope));
				s.file = _file;
				s.offset = offset;
				s.line = line;
				s.column = column;
				s.kind = kind;
				s.flags = flags | (in_alternative_branch() ? symbol_flag::alternative_branch : symbol_flag::none);
				_symbols.push_back(s);
			}

			[[nodiscard]] bool in_alternative_branch() const
			{
				return std::ranges::any_of(_conditionals, [](const bool alternative) { return alternative; });
			}

			[[nodiscard]] std::string scope_text(const std::string_view extra) const
			{
				std::string out;

				for (const auto& scope : _scopes)
				{
					if (scope.name.empty())
						continue;

					if (!out.empty())
						out += "::";

					out += scope.name;
				}

				if (!extra.empty())
				{
					if (!out.empty())
						out += "::";

					out += extra;
				}

				return out;
			}

			[[nodiscard]] symbol_kind enclosing_kind() const
			{
				return _scopes.empty() ? symbol_kind::unknown : _scopes.back().kind;
			}

			//
			// Preprocessor
			//

			void directive_token(const token& t)
			{
				if (t.is_punct("#") && t.first_on_line)
				{
					_directive.clear();
					_directive_index = 0;
					return;
				}

				++_directive_index;

				if (_directive_index == 1)
				{
					_directive = t.text;

					if (_directive == "if" || _directive == "ifdef" || _directive == "ifndef")
						_conditionals.push_back(false);
					else if (_directive == "else" || _directive == "elif")
					{
						if (!_conditionals.empty())
							_conditionals.back() = true;
					}
					else if (_directive == "endif")
					{
						if (!_conditionals.empty())
							_conditionals.pop_back();
					}

					return;
				}

				// The name is the only part of a macro worth indexing; its body is opaque
				if (_directive_index == 2 && _directive == "define" && t.type == token_type::identifier)
					emit(t.text, {}, symbol_kind::macro, t.offset, t.line, t.column, symbol_flag::definition);
			}

			//
			// Declarations
			//

			void reset_declaration()
			{
				_pending.clear();
				_qualifier.clear();
				_tag_name.clear();
				_alias_name.clear();
				_tag_kind = symbol_kind::unknown;
				_expect_tag_name = false;
				_in_base_clause = false;
				_is_function = false;
				_seen_assign = false;
				_using = false;
				_typedef = false;
				_concept = false;
				_extern_linkage = false;
				_pending_tilde = false;
				_operator_pending = false;
				_operator_pair.clear();
				_access_specifier = false;
				_in_ctor_initializers = false;
				_expect_initializer = false;
				_token_count = 0;
			}

			void set_pending(const std::string_view text, const uint32_t offset, const uint32_t line,
			                 const uint32_t column)
			{
				_pending = text;
				_pending_offset = offset;
				_pending_line = line;
				_pending_column = column;
			}

			void code_token(const token& t)
			{
				if (_paren == 0 && _bracket == 0)
					++_token_count;

				// Only the name directly after '::' inherits the qualifier
				const auto after_scope = _after_scope_operator;
				_after_scope_operator = false;

				if (t.type == token_type::identifier)
					identifier_token(t, after_scope);
				else if (t.type == token_type::punct)
					punct_token(t);
				else if (t.type == token_type::string && _pending == "extern")
					_extern_linkage = true;
			}

			void identifier_token(const token& t, const bool after_scope)
			{
				if (_paren > 0 || _bracket > 0 || _angle > 0 || _seen_assign)
					return;

				if (_operator_pending)
				{
					_pending += ' ';
					_pending += t.text;
					_operator_pending = false;
					return;
				}

				if (t.kw != keyword::none)
				{
					if (!_is_function)
						keyword_token(t);

					return;
				}

				if (_in_base_clause)
					return;

				if (_expect_tag_name)
				{
					if (_tag_name.empty())
					{
						_tag_offset = t.offset;
						_tag_line = t.line;
						_tag_column = t.column;
					}

					_tag_name += t.text;
					_expect_tag_name = false;
					set_pending(t.text, t.offset, t.line, t.column);
					return;
				}

				if (_tag_kind != symbol_kind::unknown && _tag_kind != symbol_kind::name_space)
				{
					if (t.text == "final" && (_tag_kind == symbol_kind::class_type || _tag_kind == symbol_kind::struct_type))
					{
						auto lookahead = _lex;
						auto next = lookahead.next();
						while (next.type == token_type::comment)
							next = lookahead.next();

						if (next.is_punct("{") || next.is_punct(":"))
							return;
					}

					// A second name declares an object or function using the tag as its type.
					_tag_kind = symbol_kind::unknown;
				}

				if (_using && _alias_name.empty())
					_alias_name = t.text;

				if (_concept && _alias_name.empty())
					_alias_name = t.text;

				if (_pending_tilde)
				{
					set_pending("~" + std::string(t.text), _pending_offset, _pending_line, _pending_column);
					_pending_tilde = false;
					return;
				}

				// 'std::vector<T> f()' declares f, not std::f
				if (!after_scope)
					_qualifier.clear();

				set_pending(t.text, t.offset, t.line, t.column);

				// An enumerator is simply the first name after '{' or ','
				if (enclosing_kind() == symbol_kind::enum_type && _after_enum_separator)
				{
					emit(t.text, {}, symbol_kind::enumerator, t.offset, t.line, t.column, symbol_flag::definition);
					_after_enum_separator = false;
				}
			}

			void keyword_token(const token& t)
			{
				switch (t.kw)
				{
				case keyword::namespace_:
					_tag_kind = symbol_kind::name_space;
					_expect_tag_name = true;
					break;
				case keyword::class_:
					if (_tag_kind != symbol_kind::enum_type) // 'enum class' keeps the enum kind
					{
						_tag_kind = symbol_kind::class_type;
						_expect_tag_name = true;
					}
					break;
				case keyword::struct_:
					if (_tag_kind != symbol_kind::enum_type)
					{
						_tag_kind = symbol_kind::struct_type;
						_expect_tag_name = true;
					}
					break;
				case keyword::union_:
					_tag_kind = symbol_kind::union_type;
					_expect_tag_name = true;
					break;
				case keyword::enum_:
					_tag_kind = symbol_kind::enum_type;
					_expect_tag_name = true;
					break;
				case keyword::template_:
					_skip_template_params = true;
					break;
				case keyword::operator_:
					set_pending("operator", t.offset, t.line, t.column);
					_operator_pending = true;
					break;
				case keyword::using_:
					_using = true;
					break;
				case keyword::typedef_:
					_typedef = true;
					break;
				case keyword::concept_:
					_concept = true;
					break;
				case keyword::public_:
				case keyword::private_:
				case keyword::protected_:
					_access_specifier = true;
					break;
				case keyword::extern_:
					set_pending("extern", t.offset, t.line, t.column);
					break;
				default:
					break;
				}
			}

			void punct_token(const token& t)
			{
				const auto p = t.text;

				// 'operator' takes the punctuation that follows it as part of its name
				if (_operator_pending)
				{
					if (!_operator_pair.empty())
					{
						if (p == _operator_pair)
						{
							_operator_pair.clear();
							_operator_pending = false;
						}

						return;
					}

					if (p == "(" || p == "[")
					{
						_pending += p == "(" ? "()" : "[]";
						_operator_pair = p == "(" ? ")" : "]";
						return;
					}

					_pending += p;
					_operator_pending = false;
					return;
				}

				if (p == "(")
				{
					if (_paren == 0 && _bracket == 0 && _angle == 0)
						open_paren();

					++_paren;
					return;
				}

				if (p == ")")
				{
					if (_paren > 0)
						--_paren;

					if (_paren == 0 && _in_ctor_initializers)
						_expect_initializer = false;

					return;
				}

				if (p == "[")
				{
					++_bracket;
					return;
				}

				if (p == "]")
				{
					if (_bracket > 0)
						--_bracket;

					return;
				}

				if (_paren > 0 || _bracket > 0)
					return;

				if (_skip_template_params)
				{
					if (p == "<")
					{
						++_angle;
						return;
					}

					if (p == ">")
					{
						if (_angle > 0 && --_angle == 0)
							_skip_template_params = false;

						return;
					}

					if (_angle > 0)
						return;
				}

				if (p == "<" && !_seen_assign && !_is_function && !_pending.empty())
				{
					++_angle;
					return;
				}

				if (_angle > 0)
				{
					if (p == ">")
						--_angle;

					return;
				}

				if (p == "{")
				{
					open_brace();
					return;
				}

				if (p == "}")
				{
					close_brace();
					return;
				}

				if (p == ";")
				{
					end_declaration();
					return;
				}

				if (p == ",")
				{
					if (enclosing_kind() == symbol_kind::enum_type)
					{
						_after_enum_separator = true;
						_seen_assign = false;
					}
					else if (_in_ctor_initializers)
						_expect_initializer = true;

					return;
				}

				// The initializer cannot rename its declaration or introduce a scope qualifier.
				if (_seen_assign)
					return;

				if (p == "::")
				{
					if (_expect_tag_name || (_tag_kind == symbol_kind::name_space && !_tag_name.empty()))
					{
						_tag_name += "::";
						_expect_tag_name = true;
					}
					else if (!_pending.empty())
					{
						if (!_qualifier.empty())
							_qualifier += "::";

						_qualifier += _pending;
						_pending.clear();
						_after_scope_operator = true;
					}

					return;
				}

				if (p == "=")
				{
					_seen_assign = true;
					return;
				}

				if (p == ":")
				{
					if (_access_specifier)
						reset_declaration();
					else if (_is_function)
					{
						_in_ctor_initializers = true;
						_expect_initializer = true;
					}
					else if (_tag_kind != symbol_kind::unknown)
						_in_base_clause = true;

					_access_specifier = false;
					return;
				}

				if (p == "~")
				{
					_pending_tilde = true;
					_pending_offset = t.offset;
					_pending_line = t.line;
					_pending_column = t.column;
					return;
				}
			}

			void open_paren()
			{
				if (_is_function || _seen_assign || _pending.empty() || _tag_kind != symbol_kind::unknown)
					return;

				if (is_transparent_call(_pending))
				{
					_pending.clear();
					return;
				}

				_is_function = true;
				_func_name = _pending;
				_func_qualifier = _qualifier;
				_func_offset = _pending_offset;
				_func_line = _pending_line;
				_func_column = _pending_column;
			}

			void open_brace()
			{
				++_depth;

				if (const auto tag = _tag_kind; tag != symbol_kind::unknown)
				{
					if (!_tag_name.empty())
						emit(_tag_name, {}, tag, _tag_offset, _tag_line, _tag_column, symbol_flag::definition);

					auto name = _tag_name;
					reset_declaration();
					push_scope(name, tag);
					_after_enum_separator = tag == symbol_kind::enum_type;
					return;
				}

				// 'extern "C" { ... }' declares into the enclosing scope, so stay inside it
				if (_extern_linkage && _pending == "extern")
				{
					reset_declaration();
					push_scope({}, symbol_kind::unknown);
					return;
				}

				if (_is_function)
				{
					if (_in_ctor_initializers && _expect_initializer)
					{
						skip_block();
						_expect_initializer = false;
						return;
					}

					emit(_func_name, _func_qualifier, symbol_kind::function, _func_offset, _func_line, _func_column,
					     symbol_flag::definition);
					skip_block();
					reset_declaration();
					return;
				}

				// Braced initializers are opaque, but their declaration still ends at ';'.
				skip_block();
				_seen_assign = true;
			}

			void push_scope(const std::string_view name, const symbol_kind kind)
			{
				_scopes.push_back({std::string(name), kind, _depth});
			}

			void close_brace()
			{
				if (_depth > 0)
					--_depth;

				while (!_scopes.empty() && _scopes.back().depth > _depth)
					_scopes.pop_back();

				_after_enum_separator = enclosing_kind() == symbol_kind::enum_type;
				reset_declaration();
			}

			void end_declaration()
			{
				if (_is_function && !_func_name.empty())
				{
					emit(_func_name, _func_qualifier, symbol_kind::function, _func_offset, _func_line, _func_column,
					     symbol_flag::none);
				}
				else if (_concept && !_alias_name.empty())
				{
					emit(_alias_name, {}, symbol_kind::concept_type, _pending_offset, _pending_line, _pending_column,
					     symbol_flag::definition);
				}
				else if (_using && _seen_assign && !_alias_name.empty())
				{
					emit(_alias_name, {}, symbol_kind::type_alias, _pending_offset, _pending_line, _pending_column,
					     symbol_flag::definition);
				}
				else if (_typedef && !_pending.empty())
				{
					emit(_pending, {}, symbol_kind::type_alias, _pending_offset, _pending_line, _pending_column,
					     symbol_flag::definition);
				}
				else if (!_using && !_typedef && _tag_kind == symbol_kind::unknown && !_pending.empty()
					&& _token_count >= 2 && enclosing_kind() != symbol_kind::enum_type)
				{
					emit(_pending, _qualifier, symbol_kind::variable, _pending_offset, _pending_line, _pending_column,
					     symbol_flag::none);
				}

				reset_declaration();
			}

			void skip_block()
			{
				auto level = 1;

				for (auto t = _lex.next(); t.type != token_type::end; t = _lex.next())
				{
					if (t.type != token_type::punct || t.in_directive)
						continue;

					if (t.text == "{")
					{
						++level;
					}
					else if (t.text == "}" && --level == 0)
					{
						break;
					}
				}

				if (_depth > 0)
					--_depth;
			}

			lexer _lex;
			name_table& _names;
			uint32_t _file = 0;
			std::vector<symbol> _symbols;
			std::vector<scope_entry> _scopes;
			std::vector<bool> _conditionals;

			int _depth = 0;
			int _paren = 0;
			int _bracket = 0;
			int _angle = 0;
			int _token_count = 0;

			std::string _directive;
			int _directive_index = 0;

			std::string _pending;
			uint32_t _pending_offset = 0;
			uint32_t _pending_line = 0;
			uint32_t _pending_column = 0;

			std::string _qualifier;
			std::string _tag_name;
			uint32_t _tag_offset = 0;
			uint32_t _tag_line = 0;
			uint32_t _tag_column = 0;
			symbol_kind _tag_kind = symbol_kind::unknown;

			std::string _func_name;
			std::string _func_qualifier;
			uint32_t _func_offset = 0;
			uint32_t _func_line = 0;
			uint32_t _func_column = 0;

			std::string _alias_name;
			std::string _operator_pair;

			bool _expect_tag_name = false;
			bool _in_base_clause = false;
			bool _is_function = false;
			bool _seen_assign = false;
			bool _using = false;
			bool _typedef = false;
			bool _concept = false;
			bool _extern_linkage = false;
			bool _pending_tilde = false;
			bool _operator_pending = false;
			bool _access_specifier = false;
			bool _in_ctor_initializers = false;
			bool _expect_initializer = false;
			bool _skip_template_params = false;
			bool _after_enum_separator = false;
			bool _after_scope_operator = false;
		};
	}

	std::vector<symbol> parse_declarations(const std::string_view source, const uint32_t file, name_table& names)
	{
		declaration_parser parser(source, file, names);
		return parser.run();
	}

	uint32_t index::add_file(const std::string_view path)
	{
		return _files.id(path);
	}

	void index::update_file(const uint32_t file, const std::string_view source)
	{
		remove_file(file);

		auto parsed = parse_declarations(source, file, _names);
		_count += parsed.size();

		for (const auto& s : parsed)
			_by_name[s.name].push_back(s);

		_by_file[file] = std::move(parsed);
	}

	void index::remove_file(const uint32_t file)
	{
		const auto found = _by_file.find(file);

		if (found == _by_file.end())
			return;

		// One pass per distinct name, since a name's entries are removed together
		std::unordered_set<uint32_t> names;

		for (const auto& s : found->second)
			names.insert(s.name);

		for (const auto name : names)
		{
			if (const auto entries = _by_name.find(name); entries != _by_name.end())
			{
				std::erase_if(entries->second, [file](const symbol& s) { return s.file == file; });

				if (entries->second.empty())
					_by_name.erase(entries);
			}
		}

		_count -= found->second.size();
		_by_file.erase(found);
	}

	std::vector<symbol> index::find(const std::string_view name) const
	{
		const auto id = _names.find(name);

		if (id == 0)
			return {};

		const auto found = _by_name.find(id);
		return found == _by_name.end() ? std::vector<symbol>{} : found->second;
	}

	std::vector<symbol> index::in_file(const uint32_t file) const
	{
		const auto found = _by_file.find(file);
		return found == _by_file.end() ? std::vector<symbol>{} : found->second;
	}

	std::string index::qualified_name(const symbol& s) const
	{
		const auto scope = scope_of(s);
		const auto name = name_of(s);
		return scope.empty() ? std::string(name) : std::format("{}::{}", scope, name);
	}
}
