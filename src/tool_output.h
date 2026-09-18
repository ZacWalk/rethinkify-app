// tool_output.h — Turns a child process's output stream into diagnostics
//
// Pure text to structure: no paths are resolved and no lines are consumed. The
// runner keeps the whole stream; this only says what parts of it mean.

#pragma once

namespace tools
{
	enum class severity : uint8_t
	{
		note,
		warning,
		error,
	};

	struct diagnostic
	{
		std::string file; // exactly as the tool printed it, relative or absolute
		int line = 0; // 1-based; 0 when the tool named no line
		int column = 0; // 1-based; 0 when the tool named no column
		severity level = severity::error;
		std::string code; // 'C2065', 'LNK2019', 'CMake', 'CTest'; empty for clang
		std::string text;
		std::vector<std::string> notes; // continuation lines belonging to this one
	};

	// Stateful, because a diagnostic owns the lines that follow it. Lines are fed
	// in arrival order, stdout and stderr merged as the user sees them.
	class output_parser
	{
	public:
		void add_line(std::string_view raw);
		void finish();

		[[nodiscard]] std::span<const diagnostic> diagnostics() const { return _diagnostics; }
		[[nodiscard]] std::span<const std::string> failed_targets() const { return _failed_targets; }

		// Newest build progress seen, such as "12/345", for the message bar
		[[nodiscard]] std::string_view progress() const { return _progress; }

		[[nodiscard]] int count(severity level) const;

	private:
		enum class block
		{
			none,
			cmake,
			powershell,

			// Indented lines under a compiler diagnostic, such as MSVC's 'with [ _Ty=int ]'
			indent,
		};

		diagnostic* last();
		void add(diagnostic d);

		std::vector<diagnostic> _diagnostics;
		std::vector<std::string> _failed_targets;
		std::vector<std::string> _pending_includes; // gcc's 'In file included from' chain
		std::string _progress;
		block _block = block::none;
	};

	// Removes CSI, OSC and other escape sequences. Tools colour their output even
	// when it is redirected, and an escape byte left in a path breaks every match.
	[[nodiscard]] std::string strip_ansi(std::string_view text);
}
