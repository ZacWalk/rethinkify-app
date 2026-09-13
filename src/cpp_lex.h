// cpp_lex.h — C++ token scanner, the input to the symbol index.
//
// Deliberately not the per-line scanner in document_syntax.cpp: that one's single
// carry cookie is what keeps typing cheap, and it cannot represent a raw string, a
// spliced line or a continued directive. Two scanners, one token vocabulary.

#pragma once

namespace cpp
{
	enum class token_type : uint8_t
	{
		end,
		identifier,
		number,
		string,
		character,
		punct,
		comment,
	};

	// Only the keywords the declaration parser reacts to are named; every other
	// C++ keyword reports as 'other' so an identifier can still be told apart.
	enum class keyword : uint8_t
	{
		none,
		other,
		alignas_,
		alignof_,
		auto_,
		class_,
		concept_,
		const_,
		consteval_,
		constexpr_,
		constinit_,
		decltype_,
		enum_,
		explicit_,
		export_,
		extern_,
		friend_,
		inline_,
		mutable_,
		namespace_,
		noexcept_,
		operator_,
		private_,
		protected_,
		public_,
		requires_,
		return_,
		static_,
		static_assert_,
		struct_,
		template_,
		typedef_,
		typename_,
		union_,
		using_,
		virtual_,
	};

	struct token
	{
		std::string_view text;
		uint32_t offset = 0;
		uint32_t line = 0; // 0-based, of the token's first byte
		uint32_t column = 0; // byte offset within that line
		token_type type = token_type::end;
		keyword kw = keyword::none;

		// Physically first on its line, even when that line continues a directive
		bool first_on_line = false;

		// Inside a preprocessor directive, including its backslash continuations
		bool in_directive = false;

		[[nodiscard]] bool is(const token_type t) const { return type == t; }
		[[nodiscard]] bool is(const keyword k) const { return type == token_type::identifier && kw == k; }
		[[nodiscard]] bool is_punct(const std::string_view p) const { return type == token_type::punct && text == p; }
	};

	// Scans a whole buffer. Tokens are slices of it, so a token never spans a line
	// splice; an identifier broken by one is not recognised.
	class lexer
	{
	public:
		explicit lexer(std::string_view src);

		// Returns token_type::end at the end of the buffer, repeatedly
		token next();

		[[nodiscard]] std::string_view source() const { return _src; }

	private:
		[[nodiscard]] char peek(size_t ahead = 0) const;

		// Byte after a backslash-newline at 'pos', or 0 when there is no splice there
		[[nodiscard]] size_t splice_end(size_t pos) const;

		void skip_trivia();
		void scan_line_comment();
		void scan_block_comment();
		void scan_ident();
		void scan_number();
		void scan_quoted(char quote);
		void scan_raw_string();
		void scan_punct();

		std::string_view _src;
		size_t _pos = 0;
		size_t _line_start = 0;
		uint32_t _line = 0;
		bool _in_directive = false;
		bool _at_line_start = true;
	};

	[[nodiscard]] keyword keyword_of(std::string_view text);
	[[nodiscard]] bool is_keyword(std::string_view text);

	// One-shot pass, for tests and whole-file work. The end token is not included.
	[[nodiscard]] std::vector<token> tokenize(std::string_view src);
}
