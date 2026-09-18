// tool_runner.h — Runs project tools as child processes and turns what they print
// into a value. The menu, the command line and MCP are all callers of the same thing.

#pragma once

#include "tool_output.h"

namespace tools
{
	struct command
	{
		std::string name; // shown in the menu and the message bar
		std::string tag; // lets a caller recognise its own run in the result
		pf::file_path exe;
		std::vector<std::string> args;
		pf::file_path working_dir;
		bool destructive = false; // confirmed every time, never run implicitly
	};

	enum class requester : uint8_t
	{
		user,
		client, // an MCP client, so its run waits its turn rather than being refused
	};

	struct output_line
	{
		std::string text;
		bool from_stderr = false;
	};

	struct result
	{
		std::string name;
		std::string tag;
		std::string command_line; // for display only; never re-executed
		std::string working_dir;
		bool started = false; // false when the process could not be launched at all
		bool cancelled = false;
		int exit_code = 0;
		int64_t elapsed_ms = 0;
		std::vector<output_line> output;
		size_t dropped_lines = 0;
		std::vector<diagnostic> diagnostics;
		std::vector<std::string> failed_targets;

		[[nodiscard]] bool succeeded() const { return started && !cancelled && exit_code == 0; }
	};

	[[nodiscard]] std::string display_command_line(const command& cmd);
	[[nodiscard]] std::string to_markdown(const result& r);

	// One tool at a time, but nothing is refused while another runs: a second
	// request waits in a bounded queue with the identity of whoever asked.
	class runner
	{
	public:
		using run_id = int;

		static constexpr size_t max_queued = 8;
		static constexpr size_t head_lines = 2000;
		static constexpr size_t tail_lines = 2000;

		using spawn_function = std::function<pf::child_process_ptr(const pf::file_path&,
		                                                           std::span<const std::string>,
		                                                           const pf::file_path&,
		                                                           pf::child_process_callbacks)>;

		runner() = default;
		~runner();

		runner(const runner&) = delete;
		runner& operator=(const runner&) = delete;

		// Injectable so unit tests never launch PowerShell, CMake or a compiler
		spawn_function spawn = pf::spawn_child_process;

		std::function<void(const command&)> on_started;
		std::function<void(std::string_view)> on_output; // newest line, for the message bar
		std::function<void(const result&)> on_finished;

		// 0 when the queue is full
		run_id queue(command cmd, requester who = requester::user);

		// Cancels a queued run, or terminates the running one
		bool cancel(run_id id);
		void stop_current();

		[[nodiscard]] bool busy() const { return _current_id != 0; }
		[[nodiscard]] size_t queued() const { return _queued.size(); }
		[[nodiscard]] run_id current() const { return _current_id; }
		[[nodiscard]] requester current_requester() const { return _current_who; }
		[[nodiscard]] std::string_view current_name() const { return _current.name; }
		[[nodiscard]] std::string_view progress() const { return _parser.progress(); }

	private:
		struct entry
		{
			run_id id = 0;
			command cmd;
			requester who = requester::user;
		};

		void start_next();
		void add_output(std::string_view raw, bool from_stderr);
		void finish(int exit_code);

		std::vector<entry> _queued;
		command _current;
		requester _current_who = requester::user;
		run_id _current_id = 0;
		run_id _next_id = 1;
		bool _launched = false;
		bool _cancelling = false;
		int64_t _started_ms = 0;
		pf::child_process_ptr _process;
		output_parser _parser;
		std::vector<output_line> _head;
		std::vector<output_line> _tail;
		size_t _tail_next = 0;
		size_t _dropped = 0;
	};

	//
	// Helper script discovery
	//

	struct script_option
	{
		std::string name; // 'Config'
		std::vector<std::string> values; // 'Debug', 'Release'
	};

	struct script_interface
	{
		std::vector<std::string> commands; // the first positional parameter's ValidateSet
		std::vector<script_option> options;

		[[nodiscard]] bool empty() const { return commands.empty() && options.empty(); }
	};

	struct script_argument
	{
		std::string name;
		std::string value;
	};

	// Reads the JSON the probe below prints
	[[nodiscard]] script_interface parse_script_interface(std::string_view text);

	// Escapes for a single-quoted PowerShell literal, which has no other escapes
	[[nodiscard]] std::string powershell_literal(std::string_view text);

	// Asks PowerShell to parse the script with its own grammar. Reading a script is
	// not running it, so this is safe on a folder that was merely opened.
	[[nodiscard]] command probe_script_command(const pf::file_path& powershell, const pf::file_path& script);

	[[nodiscard]] command script_command(const pf::file_path& powershell,
	                                     const pf::file_path& script,
	                                     std::string_view command_name,
	                                     std::span<const script_argument> arguments,
	                                     const pf::file_path& working_dir);
}
