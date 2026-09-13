// cpp_index.h — Declaration-level parser and symbol table
//
// Names resolve lexically, with scope heuristics. This is not a compiler front end:
// see docs/cpp.md for what that costs and what it buys.

#pragma once

#include "cpp_lex.h"

namespace cpp
{
	enum class symbol_kind : uint8_t
	{
		unknown,
		name_space,
		class_type,
		struct_type,
		union_type,
		enum_type,
		enumerator,
		function, // free functions and members alike; the scope tells them apart
		variable,
		type_alias,
		concept_type,
		macro,
	};

	[[nodiscard]] std::string_view to_string(symbol_kind kind);

	namespace symbol_flag
	{
		constexpr uint8_t none = 0;
		constexpr uint8_t definition = 1 << 0; // has a body, rather than only a declaration

		// Inside an #else or #elif, so a sibling branch declares something else
		constexpr uint8_t alternative_branch = 1 << 1;
	}

	struct symbol
	{
		uint32_t name = 0; // interned
		uint32_t scope = 0; // interned, fully qualified, empty at file scope
		uint32_t file = 0;
		uint32_t offset = 0; // byte offset of the name within the file
		uint32_t line = 0; // 0-based
		uint32_t column = 0; // byte offset of the name within its line
		symbol_kind kind = symbol_kind::unknown;
		uint8_t flags = 0;
	};

	static_assert(sizeof(symbol) <= 32, "the index is sized in bytes per symbol");

	// Interns strings; ids are stable across insertion and 0 is the empty string
	class name_table
	{
	public:
		name_table();

		uint32_t id(std::string_view text);
		[[nodiscard]] uint32_t find(std::string_view text) const;
		[[nodiscard]] std::string_view text(uint32_t id) const;
		[[nodiscard]] size_t size() const { return _texts.size(); }

	private:
		// Node-based, so an interned string's address survives every later insert
		std::unordered_map<std::string, uint32_t> _ids;
		std::vector<const std::string*> _texts;
	};

	// Everything one file declares, in source order
	[[nodiscard]] std::vector<symbol> parse_declarations(std::string_view source, uint32_t file, name_table& names);

	class index
	{
	public:
		// Interned, so re-adding the same path returns the same id
		uint32_t add_file(std::string_view path);
		[[nodiscard]] uint32_t find_file(std::string_view path) const { return _files.find(path); }
		[[nodiscard]] std::string_view file_path(uint32_t file) const { return _files.text(file); }

		// Replaces everything previously parsed from this file
		void update_file(uint32_t file, std::string_view source);
		void remove_file(uint32_t file);

		[[nodiscard]] std::vector<symbol> find(std::string_view name) const;
		[[nodiscard]] std::vector<symbol> in_file(uint32_t file) const;
		[[nodiscard]] size_t symbol_count() const { return _count; }
		[[nodiscard]] size_t file_count() const { return _by_file.size(); }

		[[nodiscard]] std::string_view name_of(const symbol& s) const { return _names.text(s.name); }
		[[nodiscard]] std::string_view scope_of(const symbol& s) const { return _names.text(s.scope); }
		[[nodiscard]] std::string qualified_name(const symbol& s) const;

	private:
		name_table _names;
		name_table _files;
		std::unordered_map<uint32_t, std::vector<symbol>> _by_file;

		// Name id to where it was declared, so a lookup does not scan the index
		std::unordered_map<uint32_t, std::vector<symbol>> _by_name;
		size_t _count = 0;
	};
}
