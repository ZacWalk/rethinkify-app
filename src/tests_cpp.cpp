// tests_cpp.cpp — Unit tests for the C++ tooling: lexer, tool output, tool runner

#include "pch.h"
#include "cpp_lex.h"
#include "cpp_index.h"
#include "document.h"
#include "tool_output.h"
#include "tool_runner.h"
#include "test.h"

#include <cstdlib>

//
// C++ lexer
//

// One line per token stream, so a failure names the token that went wrong
static std::string lex_dump(const std::string_view src)
{
	std::string out;

	for (const auto& t : cpp::tokenize(src))
	{
		if (!out.empty())
			out += ' ';

		switch (t.type)
		{
		case cpp::token_type::identifier:
			out += t.kw == cpp::keyword::none ? "id:" : "kw:";
			break;
		case cpp::token_type::number:
			out += "num:";
			break;
		case cpp::token_type::string:
			out += "str:";
			break;
		case cpp::token_type::character:
			out += "chr:";
			break;
		case cpp::token_type::comment:
			out += "com:";
			break;
		default:
			out += "p:";
			break;
		}

		for (const auto c : t.text)
		{
			if (c == '\n')
				out += "\\n";
			else
				out += c;
		}
	}

	return out;
}

static void should_lex_basic_tokens()
{
	should::is_equal("kw:int id:x p:= num:1 p:+ num:2 p:; com:// done",
	                 lex_dump("int x = 1 + 2; // done"));
}

static void should_lex_identifier_bytes()
{
	// Bytes at or above 0x80 belong to the identifier, as everywhere else in the editor
	should::is_equal("id:_a id:$b id:caf\xC3\xA9 id:x9", lex_dump("_a $b caf\xC3\xA9 x9"));
}

static void should_lex_keywords()
{
	should::is_equal_true(cpp::keyword_of("class") == cpp::keyword::class_, "class");
	should::is_equal_true(cpp::keyword_of("constexpr") == cpp::keyword::constexpr_, "constexpr");
	should::is_equal_true(cpp::keyword_of("reinterpret_cast") == cpp::keyword::other, "an uninteresting keyword");
	should::is_equal(false, cpp::is_keyword("classy"), "not a prefix match");
	should::is_equal(false, cpp::is_keyword("clas"), "not a shortened match");
	should::is_equal(false, cpp::is_keyword("override"), "contextual words are identifiers");
}

static void should_lex_numbers()
{
	should::is_equal("num:1'000'000 num:0x1p+3 num:1.5e-7f num:.5 num:0b1010 num:12ull",
	                 lex_dump("1'000'000 0x1p+3 1.5e-7f .5 0b1010 12ull"));
}

static void should_lex_digit_separator_not_character()
{
	should::is_equal("id:x p:= chr:'a' p:; id:y p:= num:1'000 p:;",
	                 lex_dump("x = 'a'; y = 1'000;"));
}

static void should_lex_literal_prefixes()
{
	should::is_equal(R"lex(str:u8"a" chr:L'x' str:u"b" str:R"(q)" str:LR"(r)")lex",
	                 lex_dump(R"lex(u8"a" L'x' u"b" R"(q)" LR"(r)")lex"));
}

static void should_lex_raw_strings()
{
	// The closing sequence is the delimiter, so a bare quote or paren inside is content
	should::is_equal(R"lex(str:R"(has " quote)" p:;)lex", lex_dump(R"lex(R"(has " quote)";)lex"));
	should::is_equal(R"lex(str:R"x(a)b)x" p:;)lex", lex_dump(R"lex(R"x(a)b)x";)lex"));
}

static void should_lex_spliced_line_comment()
{
	const auto tokens = cpp::tokenize("// first \\\nstill comment\nint x;");

	should::is_equal(size_t{4}, tokens.size(), "the second line is swallowed by the comment");
	should::is_equal_true(tokens[0].type == cpp::token_type::comment, "one comment");
	should::is_equal_true(tokens[1].is(cpp::keyword::other), "reached the declaration");
	should::is_equal(2, static_cast<int>(tokens[1].line), "and counted both lines");
}

static void should_lex_block_comment_line_numbers()
{
	const auto tokens = cpp::tokenize("/* a\nb\nc */ int x;");

	should::is_equal(size_t{4}, tokens.size());
	should::is_equal(0, static_cast<int>(tokens[0].line), "the comment starts on the first line");
	should::is_equal(2, static_cast<int>(tokens[1].line), "and the code after it is on the third");
}

static void should_lex_directive_continuation()
{
	const auto tokens = cpp::tokenize("#define A(x) \\\n    x + 1\nint y;");

	auto in_directive = 0;
	for (const auto& t : tokens)
		if (t.in_directive)
			++in_directive;

	should::is_equal(9, in_directive, "the whole macro, across the continuation");
	should::is_equal_true(tokens.front().is_punct("#"), "starting at the hash");
	should::is_equal_true(tokens[in_directive].is(cpp::keyword::other), "int is outside the directive");
	should::is_equal(2, static_cast<int>(tokens[in_directive].line), "on the line after the continuation");
}

static void should_lex_hash_only_starts_a_directive_at_line_start()
{
	const auto tokens = cpp::tokenize("a # b");

	should::is_equal(size_t{3}, tokens.size());
	should::is_equal_true(tokens[1].is_punct("#"), "still punctuation");
	should::is_equal(false, tokens[1].in_directive, "but not a directive");
	should::is_equal(false, tokens[2].in_directive, "nor what follows it");
}

static void should_lex_unterminated_string()
{
	const auto tokens = cpp::tokenize("\"abc\nint x;");

	should::is_equal(size_t{4}, tokens.size(), "the newline ends the string");
	should::is_equal("\"abc", tokens[0].text);
	should::is_equal(1, static_cast<int>(tokens[1].line), "and the code after it is scanned normally");
}

static void should_lex_spliced_string()
{
	const auto tokens = cpp::tokenize("\"ab\\\ncd\";");

	should::is_equal(size_t{2}, tokens.size(), "one string across two lines");
	should::is_equal_true(tokens[0].type == cpp::token_type::string, "a string");
	should::is_equal(1, static_cast<int>(tokens[1].line), "the semicolon is on the second line");
}

static void should_lex_windows_spliced_strings()
{
	const auto tokens = cpp::tokenize("\"ab\\\r\ncd\";\r\nint value;");

	should::is_equal(size_t{5}, tokens.size(), "CRLF does not expose string contents as code");
	should::is_equal("\"ab\\\r\ncd\"", tokens[0].text);
	should::is_equal(1, static_cast<int>(tokens[1].line));
	should::is_equal(3, static_cast<int>(tokens[1].column));
	should::is_equal(2, static_cast<int>(tokens[2].line));
}

static void should_lex_directives_after_comments()
{
	const auto tokens = cpp::tokenize("/* prefix */ #define VALUE 1\nint value;");
	should::is_equal(size_t{8}, tokens.size());
	should::is_equal_true(tokens[1].first_on_line, "comments are whitespace before a directive");
	should::is_equal_true(tokens[1].in_directive, "the hash starts a directive");
	should::is_equal(false, tokens[5].in_directive, "the following declaration is not a directive");

	const auto multiline = cpp::tokenize("#define VALUE /* end\n */ int value;");
	should::is_equal_true(multiline[4].in_directive, "the entire block comment is replaced by whitespace");
	should::is_equal(false, multiline[4].first_on_line, "the directive still precedes int");
	should::is_equal(1, static_cast<int>(multiline[4].line), "physical lines are still counted");
	should::is_equal(4, static_cast<int>(multiline[4].column));

	const auto continued = cpp::tokenize("#define VALUE /* same\\\r\n line */ 1\nint value;");
	should::is_equal_true(continued[4].in_directive, "a spliced newline does not end the directive");
	should::is_equal(false, continued[5].in_directive);

	const auto ordinary = cpp::tokenize("value /* prefix */ # other");
	should::is_equal(false, ordinary[2].in_directive, "a comment does not erase earlier code on the line");

	const auto after_multiline = cpp::tokenize("value /* end\n */ #define BAD 1");
	should::is_equal(false, after_multiline[2].in_directive, "comment-internal newlines do not start a logical line");
	should::is_equal(false, after_multiline[2].first_on_line);
	should::is_equal(1, static_cast<int>(after_multiline[2].line));
	should::is_equal(4, static_cast<int>(after_multiline[2].column));
}

static void should_lex_longest_punctuator()
{
	// '>>' is split so template arguments balance; '<<' is not, so a shift stays whole
	should::is_equal("id:a p:<=> id:b p:->* id:c p:>>= id:d p:> p:> id:e p:<< id:f p::: id:g p:...",
	                 lex_dump("a<=>b->*c>>=d>>e<<f::g..."));
}

static void should_lex_first_on_line()
{
	const auto tokens = cpp::tokenize("a b\nc");

	should::is_equal_true(tokens[0].first_on_line, "a");
	should::is_equal(false, tokens[1].first_on_line, "b");
	should::is_equal_true(tokens[2].first_on_line, "c");
}

static void should_lex_skip_byte_order_mark()
{
	should::is_equal("kw:int id:x p:;", lex_dump("\xEF\xBB\xBFint x;"));
}

static void should_lex_byte_columns()
{
	const auto tokens = cpp::tokenize("int x;\n  long yy;");

	should::is_equal(0, static_cast<int>(tokens[0].column), "int");
	should::is_equal(4, static_cast<int>(tokens[1].column), "x");
	should::is_equal(2, static_cast<int>(tokens[3].column), "long, on the next line");
	should::is_equal(7, static_cast<int>(tokens[4].column), "yy");

	// A token that spans lines has to leave the column right for what follows it
	const auto after = cpp::tokenize("/* a\nb */ int z;");
	should::is_equal(1, static_cast<int>(after[1].line));
	should::is_equal(5, static_cast<int>(after[1].column), "past the end of the comment");
}

static void should_highlight_cpp_source_and_header_suffixes()
{
	for (const auto suffix : {"c", "cpp", "cxx", "cc", "h", "hh", "hpp", "hxx",
		"inl", "ixx", "CXX", "HPP", "INL", "IXX"})
	{
		const auto highlight = select_highlighter(doc_type::text, pf::file_path(std::format("sample.{}", suffix)));
		text_block blocks[16]{};
		auto count = 0;
		highlight(0, "int value;", blocks, count);
		should::is_equal_true(std::ranges::any_of(std::span(blocks, count),
			[](const text_block& block) { return block._char_pos == 0 && block._color == style::code_keyword; }),
			suffix);
	}
}

//
// Tool output parsing
//

static tools::output_parser parse_output(const std::initializer_list<std::string_view> lines)
{
	tools::output_parser parser;

	for (const auto line : lines)
		parser.add_line(line);

	parser.finish();
	return parser;
}

static void should_parse_msvc_diagnostics()
{
	const auto parser = parse_output({
		R"(C:\src\app.cpp(120,5): error C2065: 'x': undeclared identifier)",
		R"(C:\src\app.cpp(9): warning C4996: 'strcpy' was declared deprecated)",
	});

	const auto found = parser.diagnostics();
	should::is_equal(size_t{2}, found.size());

	should::is_equal(R"(C:\src\app.cpp)", found[0].file);
	should::is_equal(120, found[0].line);
	should::is_equal(5, found[0].column);
	should::is_equal("C2065", found[0].code);
	should::is_equal("'x': undeclared identifier", found[0].text);
	should::is_equal_true(found[0].level == tools::severity::error, "an error");

	should::is_equal(9, found[1].line);
	should::is_equal(0, found[1].column, "no column was given");
	should::is_equal_true(found[1].level == tools::severity::warning, "a warning");
}

static void should_parse_msvc_path_containing_parentheses()
{
	const auto parser = parse_output({
		R"(C:\Program Files (x86)\sdk\a.h(9): warning C4100: unreferenced parameter)",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size());
	should::is_equal(R"(C:\Program Files (x86)\sdk\a.h)", parser.diagnostics()[0].file);
	should::is_equal(9, parser.diagnostics()[0].line);
}

static void should_parse_linker_errors_without_a_line()
{
	const auto parser = parse_output({
		"LINK : fatal error LNK1104: cannot open file 'x.lib'",
		R"lnk(main.obj : error LNK2019: unresolved external symbol "void __cdecl f(void)" (?f@@YAXXZ))lnk",
	});

	const auto found = parser.diagnostics();
	should::is_equal(size_t{2}, found.size());
	should::is_equal("LINK", found[0].file);
	should::is_equal("LNK1104", found[0].code);
	should::is_equal(0, found[0].line, "the linker names no line");
	should::is_equal("main.obj", found[1].file);
	should::is_equal("LNK2019", found[1].code);
}

static void should_attach_msvc_notes_to_the_diagnostic_above()
{
	const auto parser = parse_output({
		"a.cpp(1,1): error C2065: 'x': undeclared identifier",
		"a.h(3,1): note: see declaration of 'x'",
		"a.cpp(1,1): message : while compiling class template member function",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size(), "one diagnostic owns both notes");
	should::is_equal(size_t{2}, parser.diagnostics()[0].notes.size());
	should::is_equal("a.h(3): see declaration of 'x'", parser.diagnostics()[0].notes[0]);
}

// Verbatim from cl.exe, where the template context is indented under the note
static void should_attach_an_indented_template_context()
{
	const auto parser = parse_output({
		"bad.cpp",
		R"(tmp\bad.cpp(2): error C2665: 'std::vector<int>::push_back': no overloaded function)",
		R"(C:\vs\include\vector(939): note: could be 'void std::vector<int>::push_back(_Ty &&)')",
		"        with",
		"        [",
		"            _Ty=int",
		"        ]",
		"Microsoft (R) Incremental Linker",
		"  Creating library x.lib and object x.exp",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size(), "the source name echo is not a diagnostic");

	const auto& notes = parser.diagnostics()[0].notes;
	should::is_equal(size_t{5}, notes.size(), "the note and its four indented lines");
	should::is_equal("_Ty=int", notes[3]);
	should::is_equal_true(notes.back() == "]", "and nothing after the block is swallowed");
}

static void should_parse_clang_diagnostics()
{
	const auto parser = parse_output({
		"src/a.cpp:12:5: error: expected ';' after expression",
		R"(C:\x\b.h:3: warning: unused variable 'y')",
	});

	const auto found = parser.diagnostics();
	should::is_equal(size_t{2}, found.size());
	should::is_equal("src/a.cpp", found[0].file);
	should::is_equal(12, found[0].line);
	should::is_equal(5, found[0].column);
	should::is_equal("expected ';' after expression", found[0].text);
	should::is_equal(R"(C:\x\b.h)", found[1].file, "a drive letter is not a line number");
	should::is_equal(3, found[1].line);
	should::is_equal(0, found[1].column);
}

static void should_attach_the_include_chain()
{
	const auto parser = parse_output({
		"In file included from a.cpp:1:",
		"b.h:2:3: error: bad",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size());
	should::is_equal(size_t{1}, parser.diagnostics()[0].notes.size());
	should::is_equal("In file included from a.cpp:1:", parser.diagnostics()[0].notes[0]);
}

static void should_parse_a_cmake_error_block()
{
	const auto parser = parse_output({
		"CMake Error at CMakeLists.txt:12 (add_executable):",
		"  Cannot find source file:",
		"",
		"    missing.cpp",
		"",
		"Call Stack (most recent call first):",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size(), "the call stack line is not a diagnostic");

	const auto& d = parser.diagnostics()[0];
	should::is_equal("CMakeLists.txt", d.file);
	should::is_equal(12, d.line);
	should::is_equal("CMake", d.code);
	should::is_equal("Cannot find source file:", d.text);
	should::is_equal(size_t{1}, d.notes.size());
	should::is_equal("missing.cpp", d.notes[0]);
}

static void should_parse_a_cmake_error_without_a_location()
{
	const auto parser = parse_output({"CMake Error: Could not find CMAKE_ROOT"});

	should::is_equal(size_t{1}, parser.diagnostics().size());
	should::is_equal("Could not find CMAKE_ROOT", parser.diagnostics()[0].text);
	should::is_equal_true(parser.diagnostics()[0].file.empty(), "no file");
}

static void should_track_ninja_progress_and_failures()
{
	const auto parser = parse_output({
		"[12/345] Building CXX object CMakeFiles/x.dir/a.cpp.obj",
		"FAILED: CMakeFiles/x.dir/a.cpp.obj",
		"a.cpp(1,1): error C2065: 'x': undeclared identifier",
	});

	should::is_equal("12/345", parser.progress());
	should::is_equal(size_t{1}, parser.failed_targets().size());
	should::is_equal("CMakeFiles/x.dir/a.cpp.obj", parser.failed_targets()[0]);
	should::is_equal(size_t{1}, parser.diagnostics().size(), "the failed target is not itself a diagnostic");
}

static void should_parse_ctest_failures_only()
{
	const auto parser = parse_output({
		"1/3 Test #1: alpha ...........................   Passed    0.01 sec",
		"2/3 Test #2: beta ............................***Failed    0.02 sec",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size(), "a passing test is not a diagnostic");
	should::is_equal("CTest", parser.diagnostics()[0].code);
	should::is_equal("beta: Failed", parser.diagnostics()[0].text);
}

static void should_parse_a_powershell_error_record()
{
	const auto parser = parse_output({
		"Write-Error: boom",
		"Line |",
		"   3 |  Write-Error \"boom\"",
		"     |  ~~~~~~~~~~~~~~~~~~",
		"     | boom",
		"[1/2] Building",
	});

	should::is_equal(size_t{1}, parser.diagnostics().size());
	should::is_equal("PowerShell", parser.diagnostics()[0].code);
	should::is_equal("boom", parser.diagnostics()[0].text);
	should::is_equal(size_t{4}, parser.diagnostics()[0].notes.size(), "the record block follows it");
	should::is_equal("1/2", parser.progress(), "and the block ends at ordinary output");
}

static void should_strip_ansi_sequences()
{
	should::is_equal("error", tools::strip_ansi("\x1b[31merror\x1b[0m"));
	should::is_equal("x", tools::strip_ansi("\x1b]0;title\x07x"));
	should::is_equal("plain", tools::strip_ansi("plain"));

	// A coloured path still has to match
	const auto parser = parse_output({"\x1b[31ma.cpp(1,1): error C2065: bad\x1b[0m"});
	should::is_equal(size_t{1}, parser.diagnostics().size());
	should::is_equal("a.cpp", parser.diagnostics()[0].file);
}

static void should_ignore_ordinary_output()
{
	const auto parser = parse_output({
		"-- Configuring done (0.4s)",
		"-- Generating done",
		"Microsoft (R) C/C++ Optimizing Compiler Version 19.44",
		"  Creating library x.lib and object x.exp",
		"Time Elapsed 00:00:03.45",
		"2 Warning(s)",
		"note: this is only prose",
		"Call Stack (most recent call first):",
		"",
	});

	should::is_equal(size_t{0}, parser.diagnostics().size(), "none of this is a diagnostic");
}

static void should_count_by_severity()
{
	const auto parser = parse_output({
		"a.cpp(1,1): error C2065: one",
		"a.cpp(2,1): warning C4100: two",
		"a.cpp(3,1): error C2065: three",
	});

	should::is_equal(2, parser.count(tools::severity::error));
	should::is_equal(1, parser.count(tools::severity::warning));
}

//
// Tool runner
//

// Stands in for a spawned process, so no test ever launches one
class fake_process final : public pf::child_process
{
public:
	pf::child_process_callbacks callbacks;
	bool running = true;
	bool terminated = false;

	bool write_line(std::string_view) override { return true; }

	void close_input() override
	{
	}

	void terminate() override
	{
		terminated = true;
		exit_with(1);
	}

	[[nodiscard]] bool is_running() const override { return running; }

	void say(const std::string_view line, const bool from_stderr = false) const
	{
		if (from_stderr)
			callbacks.on_stderr_line(line);
		else
			callbacks.on_stdout_line(line);
	}

	void exit_with(const int code)
	{
		if (!running)
			return;

		running = false;
		callbacks.on_exit(code);
	}
};

// Hands out the fake processes a test drives by hand
struct fake_spawner
{
	std::vector<fake_process*> live;
	std::vector<std::string> launched;
	bool refuse = false;

	tools::runner::spawn_function function()
	{
		return [this](const pf::file_path& exe, const std::span<const std::string> args, const pf::file_path&,
		              pf::child_process_callbacks callbacks) -> pf::child_process_ptr
		{
			auto line = std::string(exe.view());

			for (const auto& arg : args)
				line += " " + arg;

			launched.push_back(line);

			if (refuse)
				return nullptr;

			auto process = std::make_unique<fake_process>();
			process->callbacks = std::move(callbacks);
			live.push_back(process.get());
			return process;
		};
	}
};

static tools::command make_command(const std::string_view name)
{
	tools::command cmd;
	cmd.name = std::string(name);
	cmd.exe = pf::file_path{R"(C:\tools\pwsh.exe)"};
	cmd.args = {"-NoProfile", std::string(name)};
	cmd.working_dir = pf::file_path{R"(C:\root)"};
	return cmd;
}

static void should_run_a_tool_and_report_its_output()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	tools::result finished;
	auto finish_count = 0;
	runner.on_finished = [&](const tools::result& r)
	{
		finished = r;
		++finish_count;
	};

	const auto id = runner.queue(make_command("build"));
	should::is_equal_true(id != 0, "it was queued");
	should::is_equal_true(runner.busy(), "and started at once");

	spawner.live[0]->say("[1/2] Building");
	spawner.live[0]->say("a.cpp(1,1): error C2065: 'x': undeclared identifier");
	spawner.live[0]->say("boom", true);
	spawner.live[0]->exit_with(2);

	should::is_equal(1, finish_count);
	should::is_equal(false, runner.busy(), "and is idle again");
	should::is_equal("build", finished.name);
	should::is_equal(2, finished.exit_code);
	should::is_equal_true(finished.started, "it launched");
	should::is_equal(size_t{3}, finished.output.size(), "every line is kept");
	should::is_equal_true(finished.output[2].from_stderr, "stderr is identified");
	should::is_equal(size_t{1}, finished.diagnostics.size(), "and the error was parsed");
	should::is_equal("C2065", finished.diagnostics[0].code);
}

static void should_report_a_tool_that_could_not_start()
{
	fake_spawner spawner;
	spawner.refuse = true;

	tools::runner runner;
	runner.spawn = spawner.function();

	tools::result finished;
	runner.on_finished = [&](const tools::result& r) { finished = r; };
	runner.queue(make_command("build"));

	should::is_equal(false, finished.started, "it never launched");
	should::is_equal(false, runner.busy(), "and the runner is free for the next one");
}

static void should_queue_a_second_run_rather_than_refusing_it()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	std::vector<std::string> finished;
	runner.on_finished = [&](const tools::result& r) { finished.push_back(r.name); };

	runner.queue(make_command("build"));
	const auto second = runner.queue(make_command("test"), tools::requester::client);

	should::is_equal_true(second != 0, "the agent's run was accepted");
	should::is_equal(size_t{1}, runner.queued(), "and waits its turn");
	should::is_equal(size_t{1}, spawner.live.size(), "only one process at a time");

	spawner.live[0]->exit_with(0);

	should::is_equal(size_t{2}, spawner.live.size(), "the queued run starts next");
	should::is_equal_true(runner.current_requester() == tools::requester::client, "and remembers who asked");

	spawner.live[1]->exit_with(0);

	should::is_equal(size_t{2}, finished.size());
	should::is_equal("build", finished[0]);
	should::is_equal("test", finished[1]);
}

static void should_refuse_a_full_queue()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	// One is running, so the bound is on how many are waiting behind it
	for (size_t i = 0; i <= tools::runner::max_queued; ++i)
		should::is_equal_true(runner.queue(make_command("build")) != 0, "accepted while there is room");

	should::is_equal(tools::runner::max_queued, runner.queued());
	should::is_equal(0, runner.queue(make_command("build")), "and refused once full");
}

static void should_cancel_a_queued_run()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	std::vector<std::string> finished;
	runner.on_finished = [&](const tools::result& r) { finished.push_back(r.name); };

	runner.queue(make_command("build"));
	const auto queued = runner.queue(make_command("clean"));

	should::is_equal_true(runner.cancel(queued), "the waiting run is dropped");
	should::is_equal(size_t{0}, runner.queued());

	spawner.live[0]->exit_with(0);

	should::is_equal(size_t{1}, finished.size(), "and never runs");
	should::is_equal(size_t{1}, spawner.live.size());
}

static void should_stop_the_running_tool()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	tools::result finished;
	runner.on_finished = [&](const tools::result& r) { finished = r; };
	runner.queue(make_command("build"));
	runner.stop_current();

	should::is_equal_true(spawner.live[0]->terminated, "the process was terminated");
	should::is_equal_true(finished.cancelled, "and the result says so");
	should::is_equal(false, runner.busy());
}

static void should_bound_the_output_it_keeps()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	tools::result finished;
	runner.on_finished = [&](const tools::result& r) { finished = r; };
	runner.queue(make_command("build"));

	const auto total = tools::runner::head_lines + tools::runner::tail_lines + 500;

	for (size_t i = 0; i < total; ++i)
		spawner.live[0]->say(std::format("line {}", i));

	spawner.live[0]->exit_with(0);

	should::is_equal(size_t{500}, finished.dropped_lines, "the middle is what goes");
	should::is_equal(tools::runner::head_lines + tools::runner::tail_lines + 1, finished.output.size(),
	                 "the beginning, a marker and the end");
	should::is_equal("line 0", finished.output[0].text, "the beginning is kept");
	should::is_equal(std::format("line {}", total - 1), finished.output.back().text, "and so is the end");
}

static void should_render_a_result_as_markdown()
{
	fake_spawner spawner;
	tools::runner runner;
	runner.spawn = spawner.function();

	tools::result finished;
	runner.on_finished = [&](const tools::result& r) { finished = r; };
	runner.queue(make_command("build"));

	spawner.live[0]->say("a.cpp(1,1): error C2065: 'x': undeclared identifier");
	spawner.live[0]->say("FAILED: a.obj");
	spawner.live[0]->exit_with(1);

	const auto text = tools::to_markdown(finished);
	should::is_equal_true(text.starts_with("# build\n"), "the command names the document");
	should::is_equal_true(text.find("**C2065**") != std::string::npos, "the code is shown");
	should::is_equal_true(text.find("`a.cpp(1)`") != std::string::npos, "with where it happened");
	should::is_equal_true(text.find("## Failed targets") != std::string::npos, "and what failed to build");
	should::is_equal_true(text.find("**Exit code 1**") != std::string::npos, "and how it ended");
}

//
// Helper script discovery
//

static void should_parse_a_script_interface()
{
	const auto found = tools::parse_script_interface(R"json(
		{"parameters":[
			{"name":"Command","position":0,"values":["run","build","test","clean"]},
			{"name":"Config","position":1,"values":["Debug","Release"]},
			{"name":"Verbose","position":2,"values":[]}
		]})json");

	should::is_equal(size_t{4}, found.commands.size());
	should::is_equal("run", found.commands[0]);
	should::is_equal("clean", found.commands[3]);
	should::is_equal(size_t{1}, found.options.size(), "a parameter with no set is not an option");
	should::is_equal("Config", found.options[0].name);
	should::is_equal(size_t{2}, found.options[0].values.size());
}

static void should_parse_a_collapsed_script_interface()
{
	// ConvertTo-Json collapses a one-element array into the value itself
	const auto found = tools::parse_script_interface(
		R"json({"parameters":{"name":"Command","position":0,"values":"build"}})json");

	should::is_equal(size_t{1}, found.commands.size());
	should::is_equal("build", found.commands[0]);
}

static void should_survive_a_script_interface_that_is_not_json()
{
	should::is_equal_true(tools::parse_script_interface("not json at all").empty(), "nothing is discovered");
	should::is_equal_true(tools::parse_script_interface("").empty(), "and an empty reply is not a crash");
}

static void should_quote_a_powershell_literal()
{
	should::is_equal("'C:\\a\\dd.ps1'", tools::powershell_literal("C:\\a\\dd.ps1"));
	should::is_equal("'it''s here'", tools::powershell_literal("it's here"),
	                 "a quote is doubled, so a path cannot end the literal");
}

static void should_build_a_script_command()
{
	const tools::script_argument arguments[] = {{"Config", "Debug"}};

	const auto cmd = tools::script_command(pf::file_path{R"(C:\pwsh.exe)"}, pf::file_path{R"(C:\root\dd.ps1)"},
	                                       "build", arguments, pf::file_path{R"(C:\root)"});

	should::is_equal("build", cmd.name);
	should::is_equal(size_t{7}, cmd.args.size());
	should::is_equal("-NoProfile", cmd.args[0]);
	should::is_equal("-NonInteractive", cmd.args[1]);
	should::is_equal("-File", cmd.args[2]);
	should::is_equal(R"(C:\root\dd.ps1)", cmd.args[3]);
	should::is_equal("build", cmd.args[4]);
	should::is_equal("-Config", cmd.args[5]);
	should::is_equal("Debug", cmd.args[6]);

	for (const auto& arg : cmd.args)
		should::is_equal(false, arg == "-ExecutionPolicy", "the user's execution policy is respected");
}

static void should_build_a_probe_that_only_reads_the_script()
{
	const auto cmd = tools::probe_script_command(pf::file_path{R"(C:\pwsh.exe)"},
	                                             pf::file_path{R"(C:\root\dd.ps1)"});

	should::is_equal("-Command", cmd.args[2]);
	should::is_equal_true(cmd.args[3].find("ParseFile") != std::string::npos, "it parses rather than runs");
	should::is_equal_true(cmd.args[3].find("'C:\\root\\dd.ps1'") != std::string::npos, "with the script quoted");

	for (const auto& arg : cmd.args)
		should::is_equal(false, arg == "-File", "the script itself is never executed");
}

//
// Declaration parser and symbol table
//

static std::string index_dump(const std::string_view source)
{
	cpp::index idx;
	const auto file = idx.add_file("test.cpp");
	idx.update_file(file, source);

	std::vector<cpp::symbol> all = idx.in_file(file);
	std::string out;

	for (const auto& s : all)
	{
		if (!out.empty())
			out += ' ';

		out += std::format("{}:{}", cpp::to_string(s.kind), idx.qualified_name(s));
	}

	return out;
}

static void should_index_namespaces_and_types()
{
	should::is_equal("namespace:a class:a::b struct:a::b::c variable:a::b::c::x",
	                 index_dump("namespace a { class b { struct c { int x; }; }; }"));
	should::is_equal("struct:item function:item::run variable:final",
	                 index_dump("struct item final : base { void run(); }; struct item final;"));
}

static void should_index_a_nested_namespace_name()
{
	should::is_equal("namespace:a::b function:a::b::f",
	                 index_dump("namespace a::b { void f() {} }"));
}

static void should_index_functions_and_methods()
{
	should::is_equal("class:widget function:widget::draw function:widget::~widget function:widget::hidden",
	                 index_dump("class widget { public: void draw(int n); ~widget(); private: int hidden(); };"));
}

static void should_index_an_out_of_line_definition()
{
	should::is_equal("function:app_state::open_path",
	                 index_dump("void app_state::open_path(const path& p, int line) { body(); }"));
}

static void should_index_a_constructor_with_an_initialiser_list()
{
	// The initialiser list's parentheses must not be mistaken for the parameter list
	should::is_equal("function:widget::widget",
	                 index_dump("widget::widget(int n) : _a(n), _b(0) { setup(); }"));
}

static void should_skip_constructor_braced_initializers_and_body()
{
	should::is_equal("class:widget function:widget::widget function:widget::after",
	                 index_dump("class widget { widget() : first{make()}, second(0), third{{1, 2}} "
		                 "{ hidden(); } void after(); };"));
	should::is_equal("function:widget::widget function:after",
	                 index_dump("widget::widget() : widget{make()} { hidden(); } void after();"));
}

static void should_index_declarations_not_initializer_references()
{
	should::is_equal("variable:count variable:copied variable:folded variable:text variable:callback variable:values",
	                 index_dump("int count = source;\nauto copied = other::make();\n"
		                 "auto folded = first < last ? yes : no;\nconst char* text = \"hello\";\n"
		                 "auto callback = [] { hidden(); };\nstd::vector<int> values{make(), 2};\n"));
}

static void should_skip_single_extern_linkage_function_bodies()
{
	should::is_equal("function:entry function:second function:third",
	                 index_dump("extern \"C\" int entry() { hidden(); }\n"
		                 "extern \"C\" { void second() { also_hidden(); } void third(); }\n"));
}

static void should_index_functions_and_variables_with_c_tag_types()
{
	should::is_equal("function:create variable:shared function:evaluate function:select function:trailing",
	                 index_dump("struct item* create(void) { hidden(); }\n"
		                 "struct item* shared = acquire();\n"
		                 "enum status evaluate(void) { also_hidden(); }\n"
		                 "union value* select(void);\n"
		                 "auto trailing() -> struct item { return {}; }\n"));
}

static void should_keep_template_arguments_out_of_declaration_names()
{
	should::is_equal("variable:table function:queue::take",
	                 index_dump("std::map<key, std::vector<value>> table;\n"
		                 "template<class T> std::vector<T> queue<T>::take() { hidden(); }\n"));
}

static void should_index_operators()
{
	should::is_equal("class:v function:v::operator== function:v::operator() function:v::operator[]",
	                 index_dump("class v { bool operator==(const v& o) const; int operator()(int i); "
		                 "int operator[](int i); };"));
}

static void should_skip_function_bodies()
{
	// Locals, lambdas and nested blocks inside a body declare nothing the index wants
	should::is_equal("function:f",
	                 index_dump("void f() { int local = 1; struct inner { int y; }; auto g = [](int a) "
		                 "{ return a; }; if (local) { int deeper = 2; } }"));
}

static void should_index_enumerators()
{
	should::is_equal("enum:colour enumerator:colour::red enumerator:colour::green enumerator:colour::blue",
	                 index_dump("enum class colour : int { red, green = 4, blue };"));
	should::is_equal("enum:colour enumerator:colour::red enumerator:colour::blue",
	                 index_dump("enum struct colour { red = other::red, blue = other::blue };"));
}

static void should_index_aliases_and_concepts()
{
	should::is_equal("alias:byte_span alias:ulong concept:small",
	                 index_dump("using byte_span = std::span<const char>;\n"
		                 "typedef unsigned long ulong;\n"
		                 "using namespace std;\n"
		                 "using std::swap;\n"
		                 "template <typename T> concept small = sizeof(T) < 8;\n"));
}

static void should_index_a_template_without_its_parameters()
{
	should::is_equal("struct:pair function:pair::get",
	                 index_dump("template <typename T, int N> struct pair { T get() const { return {}; } };"));
}

static void should_index_macros()
{
	should::is_equal("macro:MAX_SIZE macro:CHECK function:f",
	                 index_dump("#define MAX_SIZE 10\n#define CHECK(x) \\\n  ((x) != 0)\nvoid f() {}\n"));
	should::is_equal("macro:CHECK function:f",
	                 index_dump("/* prefix */ #define CHECK(x) hidden(x)\nvoid f() {}\n"));
	should::is_equal("macro:VALUE function:f",
	                 index_dump("#define VALUE /* end\n */ int value;\nvoid f() {}\n"));
}

static void should_not_index_a_forward_declaration()
{
	should::is_equal("class:real", index_dump("class fwd;\nstruct also_fwd;\nclass real { };"));
}

static void should_mark_an_alternative_branch()
{
	cpp::index idx;
	const auto file = idx.add_file("a.h");
	idx.update_file(file, "#ifdef WIN32\nvoid f() {}\n#else\nvoid g() {}\n#endif\nvoid h() {}\n");

	const auto found = idx.in_file(file);
	should::is_equal(size_t{3}, found.size());
	should::is_equal(0, found[0].flags & cpp::symbol_flag::alternative_branch, "the first branch is plain");
	should::is_equal_true((found[1].flags & cpp::symbol_flag::alternative_branch) != 0, "the #else branch is marked");
	should::is_equal(0, found[2].flags & cpp::symbol_flag::alternative_branch, "and #endif clears it");
}

static void should_report_where_a_symbol_was_declared()
{
	cpp::index idx;
	const auto file = idx.add_file("a.cpp");
	idx.update_file(file, "namespace pf\n{\n\tvoid run_ui(int t) {}\n}\n");

	const auto found = idx.find("run_ui");
	should::is_equal(size_t{1}, found.size());
	should::is_equal("pf", idx.scope_of(found[0]));
	should::is_equal(2, static_cast<int>(found[0].line), "on the third line");
	should::is_equal_true(idx.find("missing").empty(), "and an unknown name finds nothing");
}

static void should_record_where_a_symbol_starts()
{
	cpp::index idx;
	const auto file = idx.add_file("a.h");
	idx.update_file(file, "namespace pf\n{\n\tvoid run_ui(int t);\n}\n");

	const auto found = idx.find("run_ui");
	should::is_equal(size_t{1}, found.size());
	should::is_equal(2, static_cast<int>(found[0].line));
	should::is_equal(6, static_cast<int>(found[0].column), "columns are byte offsets, so a tab counts once");
}

static void should_record_alias_concept_and_namespace_name_positions()
{
	cpp::index idx;
	const auto file = idx.add_file("a.h");
	const std::string_view source = "using value = other::type;\n"
		"template<class T> concept valid = requirement<T>;\n"
		"namespace nested::inner { }\n";
	idx.update_file(file, source);

	for (const auto name : {"value", "valid", "nested::inner"})
	{
		const auto found = idx.find(name);
		should::is_equal(size_t{1}, found.size(), name);
		const auto& s = found.at(0);
		const auto start = source.find(name);
		const auto newline = source.rfind('\n', start);
		const auto line_start = newline == std::string_view::npos ? 0 : newline + 1;
		should::is_equal(start, static_cast<size_t>(s.offset), name);
		should::is_equal(start - line_start, static_cast<size_t>(s.column), name);
		should::is_equal(name, source.substr(s.offset, std::string_view(name).size()), "the target selects its name");
	}
}

static void should_replace_a_file_when_it_is_reparsed()
{
	cpp::index idx;
	const auto file = idx.add_file("a.cpp");

	idx.update_file(file, "void first() {}");
	should::is_equal(size_t{1}, idx.find("first").size());

	idx.update_file(file, "void second() {}");
	should::is_equal_true(idx.find("first").empty(), "the old symbols are gone");
	should::is_equal(size_t{1}, idx.find("second").size());
	should::is_equal(size_t{1}, idx.symbol_count(), "and nothing is counted twice");

	idx.remove_file(file);
	should::is_equal(size_t{0}, idx.symbol_count());
	should::is_equal(size_t{0}, idx.file_count());
}

static void should_keep_symbols_from_two_files_apart()
{
	cpp::index idx;
	const auto a = idx.add_file("a.cpp");
	const auto b = idx.add_file("b.cpp");

	idx.update_file(a, "void shared() {}");
	idx.update_file(b, "void shared() {}");

	const auto found = idx.find("shared");
	should::is_equal(size_t{2}, found.size(), "an overload set, not one symbol");
	should::is_equal_true(found[0].file != found[1].file, "from different files");

	idx.remove_file(a);
	should::is_equal(size_t{1}, idx.find("shared").size(), "and removing one leaves the other");
}

// The acceptance test: read the real sources and check known symbols resolve. Skipped
// rather than failed when the sources are not beside the executable, so an installed
// copy still passes its own suite.
static void should_index_this_repository()
{
	auto root = pf::file_path::module_folder().folder();

	if (!root.combine("src").combine("cpp_lex.h").exists())
		return;

	cpp::index idx;
	std::vector<std::pair<pf::file_path, std::string>> sources;
	auto bytes = size_t{0};

	for (const auto& entry : pf::iterate_file_items(root.combine("src"), false).files)
	{
		const auto extension = entry.path.extension();

		if (pf::icmp(extension, ".h") != 0 && pf::icmp(extension, ".cpp") != 0)
			continue;

		std::ifstream stream(entry.path.c_str(), std::ios::binary);
		std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

		if (!source.empty())
		{
			bytes += source.size();
			sources.emplace_back(entry.path, std::move(source));
		}
	}

	// Read everything first, so the rate below measures the index and not the disk
	const auto started = std::chrono::steady_clock::now();

	for (const auto& [path, source] : sources)
		idx.update_file(idx.add_file(path.view()), source);

	const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - started).count();
	const auto mb_per_second = elapsed_us > 0 ? static_cast<double>(bytes) / static_cast<double>(elapsed_us) : 0.0;

	should::is_equal_true(sources.size() > 20, std::format("indexed {} files", sources.size()));
	should::is_equal_true(idx.symbol_count() > 1000, std::format("found {} symbols", idx.symbol_count()));
	should::is_equal_true(bytes > 100 * 1024, std::format("over {} bytes of real source", bytes));

	// A floor low enough for a loaded machine, high enough to catch a collapse.
	// Shared CI runners are slower and noisy enough to sit on the boundary, so
	// the throughput check is a local signal only; correctness is asserted below
	// regardless of where the suite runs.
	if (std::getenv("CI") == nullptr)
	{
		should::is_equal_true(mb_per_second > 5.0,
		                      std::format("indexed {} bytes at {:.0f} MB/s", bytes, mb_per_second));
	}

	// Each of these is a shape the parser has to get right, not just a name
	const std::pair<std::string_view, std::string_view> expected[] = {
		{"app_state", ""}, // a class
		{"open_path_and_select", "app_state"}, // an out-of-line member definition
		{"tokenize", "cpp"}, // a free function in a namespace
		{"keyword_of", "cpp"},
		{"strip_ansi", "tools"},
		{"output_parser", "tools"}, // a class inside a namespace
		{"add_line", "tools::output_parser"}, // a method declared in a class body
		{"symbol", "cpp"}, // a struct
		{"parse_declarations", "cpp"},
		{"runner", "tools"},
	};

	for (const auto& [name, scope] : expected)
	{
		const auto found = idx.find(name);
		should::is_equal_true(!found.empty(), std::format("'{}' is in the index", name));

		const auto matched = std::ranges::any_of(found, [&](const cpp::symbol& s)
		{
			return idx.scope_of(s) == scope;
		});

		should::is_equal_true(matched, std::format("'{}' is declared in '{}'", name, scope));
	}
}

void register_cpp_tests(tests& suite)
{
	// C++ lexer
	suite.register_test("should lex basic tokens", should_lex_basic_tokens);
	suite.register_test("should lex identifier bytes", should_lex_identifier_bytes);
	suite.register_test("should lex keywords", should_lex_keywords);
	suite.register_test("should lex numbers", should_lex_numbers);
	suite.register_test("should lex digit separator not character", should_lex_digit_separator_not_character);
	suite.register_test("should lex literal prefixes", should_lex_literal_prefixes);
	suite.register_test("should lex raw strings", should_lex_raw_strings);
	suite.register_test("should lex spliced line comment", should_lex_spliced_line_comment);
	suite.register_test("should lex block comment line numbers", should_lex_block_comment_line_numbers);
	suite.register_test("should lex directive continuation", should_lex_directive_continuation);
	suite.register_test("should lex hash only starts a directive at line start",
	                    should_lex_hash_only_starts_a_directive_at_line_start);
	suite.register_test("should lex unterminated string", should_lex_unterminated_string);
	suite.register_test("should lex spliced string", should_lex_spliced_string);
	suite.register_test("should lex windows spliced strings", should_lex_windows_spliced_strings);
	suite.register_test("should lex directives after comments", should_lex_directives_after_comments);
	suite.register_test("should lex longest punctuator", should_lex_longest_punctuator);
	suite.register_test("should lex first on line", should_lex_first_on_line);
	suite.register_test("should lex skip byte order mark", should_lex_skip_byte_order_mark);
	suite.register_test("should lex byte columns", should_lex_byte_columns);
	suite.register_test("should highlight cpp source and header suffixes",
	                    should_highlight_cpp_source_and_header_suffixes);

	// Tool output
	suite.register_test("should parse msvc diagnostics", should_parse_msvc_diagnostics);
	suite.register_test("should parse msvc path containing parentheses",
	                    should_parse_msvc_path_containing_parentheses);
	suite.register_test("should parse linker errors without a line", should_parse_linker_errors_without_a_line);
	suite.register_test("should attach msvc notes to the diagnostic above",
	                    should_attach_msvc_notes_to_the_diagnostic_above);
	suite.register_test("should attach an indented template context",
	                    should_attach_an_indented_template_context);
	suite.register_test("should parse clang diagnostics", should_parse_clang_diagnostics);
	suite.register_test("should attach the include chain", should_attach_the_include_chain);
	suite.register_test("should parse a cmake error block", should_parse_a_cmake_error_block);
	suite.register_test("should parse a cmake error without a location",
	                    should_parse_a_cmake_error_without_a_location);
	suite.register_test("should track ninja progress and failures", should_track_ninja_progress_and_failures);
	suite.register_test("should parse ctest failures only", should_parse_ctest_failures_only);
	suite.register_test("should parse a powershell error record", should_parse_a_powershell_error_record);
	suite.register_test("should strip ansi sequences", should_strip_ansi_sequences);
	suite.register_test("should ignore ordinary output", should_ignore_ordinary_output);
	suite.register_test("should count by severity", should_count_by_severity);

	// Tool runner
	suite.register_test("should run a tool and report its output", should_run_a_tool_and_report_its_output);
	suite.register_test("should report a tool that could not start", should_report_a_tool_that_could_not_start);
	suite.register_test("should queue a second run rather than refusing it",
	                    should_queue_a_second_run_rather_than_refusing_it);
	suite.register_test("should refuse a full queue", should_refuse_a_full_queue);
	suite.register_test("should cancel a queued run", should_cancel_a_queued_run);
	suite.register_test("should stop the running tool", should_stop_the_running_tool);
	suite.register_test("should bound the output it keeps", should_bound_the_output_it_keeps);
	suite.register_test("should render a result as markdown", should_render_a_result_as_markdown);

	// Helper script discovery
	suite.register_test("should parse a script interface", should_parse_a_script_interface);
	suite.register_test("should parse a collapsed script interface", should_parse_a_collapsed_script_interface);
	suite.register_test("should survive a script interface that is not json",
	                    should_survive_a_script_interface_that_is_not_json);
	suite.register_test("should quote a powershell literal", should_quote_a_powershell_literal);
	suite.register_test("should build a script command", should_build_a_script_command);
	suite.register_test("should build a probe that only reads the script",
	                    should_build_a_probe_that_only_reads_the_script);

	// Declaration parser and symbol table
	suite.register_test("should index namespaces and types", should_index_namespaces_and_types);
	suite.register_test("should index a nested namespace name", should_index_a_nested_namespace_name);
	suite.register_test("should index functions and methods", should_index_functions_and_methods);
	suite.register_test("should index an out of line definition", should_index_an_out_of_line_definition);
	suite.register_test("should index a constructor with an initialiser list",
	                    should_index_a_constructor_with_an_initialiser_list);
	suite.register_test("should skip constructor braced initializers and body",
	                    should_skip_constructor_braced_initializers_and_body);
	suite.register_test("should index declarations not initializer references",
	                    should_index_declarations_not_initializer_references);
	suite.register_test("should skip single extern linkage function bodies",
	                    should_skip_single_extern_linkage_function_bodies);
	suite.register_test("should index functions and variables with c tag types",
	                    should_index_functions_and_variables_with_c_tag_types);
	suite.register_test("should keep template arguments out of declaration names",
	                    should_keep_template_arguments_out_of_declaration_names);
	suite.register_test("should index operators", should_index_operators);
	suite.register_test("should skip function bodies", should_skip_function_bodies);
	suite.register_test("should index enumerators", should_index_enumerators);
	suite.register_test("should index aliases and concepts", should_index_aliases_and_concepts);
	suite.register_test("should index a template without its parameters",
	                    should_index_a_template_without_its_parameters);
	suite.register_test("should index macros", should_index_macros);
	suite.register_test("should not index a forward declaration", should_not_index_a_forward_declaration);
	suite.register_test("should mark an alternative branch", should_mark_an_alternative_branch);
	suite.register_test("should report where a symbol was declared", should_report_where_a_symbol_was_declared);
	suite.register_test("should record where a symbol starts", should_record_where_a_symbol_starts);
	suite.register_test("should record alias concept and namespace name positions",
	                    should_record_alias_concept_and_namespace_name_positions);
	suite.register_test("should replace a file when it is reparsed", should_replace_a_file_when_it_is_reparsed);
	suite.register_test("should keep symbols from two files apart", should_keep_symbols_from_two_files_apart);
	suite.register_test("should index this repository", should_index_this_repository);
}
