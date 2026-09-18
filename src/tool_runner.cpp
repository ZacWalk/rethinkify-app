// tool_runner.cpp — Child-process execution and helper-script discovery

#include "pch.h"
#include "tool_runner.h"
#include "json.h"

namespace tools
{
	namespace
	{
		int64_t now_ms()
		{
			const auto since = std::chrono::steady_clock::now().time_since_epoch();
			return std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
		}

		// Long enough that a line of the tool's own output cannot close it
		constexpr std::string_view output_fence = "`````";
	}

	std::string display_command_line(const command& cmd)
	{
		std::string out = pf::quote_command_arg(cmd.exe.view());

		for (const auto& arg : cmd.args)
		{
			out += ' ';
			out += pf::quote_command_arg(arg);
		}

		return out;
	}

	std::string to_markdown(const result& r)
	{
		auto out = std::format("# {}\n\n`{}`\n\n", r.name, r.command_line);

		if (!r.working_dir.empty())
			out += std::format("in `{}`\n\n", r.working_dir);

		if (!r.started)
			out += "**The tool could not be started.**\n\n";
		else if (r.cancelled)
			out += std::format("**Stopped** after {:.1f} s\n\n", static_cast<double>(r.elapsed_ms) / 1000.0);
		else
			out += std::format("**Exit code {}** after {:.1f} s\n\n", r.exit_code,
			                   static_cast<double>(r.elapsed_ms) / 1000.0);

		if (!r.diagnostics.empty())
		{
			const auto errors = std::ranges::count(r.diagnostics, severity::error, &diagnostic::level);
			const auto warnings = std::ranges::count(r.diagnostics, severity::warning, &diagnostic::level);
			out += std::format("## {} errors, {} warnings\n\n", errors, warnings);

			for (const auto& d : r.diagnostics)
			{
				out += "- ";

				if (!d.file.empty())
					out += d.line > 0 ? std::format("`{}({})` ", d.file, d.line) : std::format("`{}` ", d.file);

				if (!d.code.empty())
					out += std::format("**{}** ", d.code);

				out += d.text;
				out += '\n';

				for (const auto& note : d.notes)
					out += std::format("  - {}\n", note);
			}

			out += '\n';
		}

		if (!r.failed_targets.empty())
		{
			out += "## Failed targets\n\n";

			for (const auto& target : r.failed_targets)
				out += std::format("- `{}`\n", target);

			out += '\n';
		}

		out += std::format("## Output\n\n{}\n", output_fence);

		for (const auto& line : r.output)
		{
			out += line.text;
			out += '\n';
		}

		out += std::format("{}\n", output_fence);
		return out;
	}

	runner::~runner()
	{
		if (_process)
			_process->terminate();
	}

	runner::run_id runner::queue(command cmd, const requester who)
	{
		if (_queued.size() >= max_queued)
			return 0;

		const auto id = _next_id++;
		_queued.push_back({id, std::move(cmd), who});

		if (!busy())
			start_next();

		return id;
	}

	bool runner::cancel(const run_id id)
	{
		if (id == 0)
			return false;

		if (id == _current_id)
		{
			_cancelling = true;

			if (_process)
				_process->terminate();
			else
				finish(-1);

			return true;
		}

		const auto it = std::ranges::find(_queued, id, &entry::id);

		if (it == _queued.end())
			return false;

		_queued.erase(it);
		return true;
	}

	void runner::stop_current()
	{
		cancel(_current_id);
	}

	void runner::start_next()
	{
		if (busy() || _queued.empty())
			return;

		auto next = std::move(_queued.front());
		_queued.erase(_queued.begin());

		_current = std::move(next.cmd);
		_current_who = next.who;
		_current_id = next.id;
		_launched = false;
		_cancelling = false;
		_started_ms = now_ms();
		_head.clear();
		_tail.clear();
		_tail_next = 0;
		_dropped = 0;
		_parser = output_parser{};

		pf::child_process_callbacks callbacks;
		callbacks.on_stdout_line = [this](const std::string_view line) { add_output(line, false); };
		callbacks.on_stderr_line = [this](const std::string_view line) { add_output(line, true); };
		callbacks.on_exit = [this](const int code) { finish(code); };

		_process = spawn ? spawn(_current.exe, _current.args, _current.working_dir, std::move(callbacks)) : nullptr;

		if (!_process)
		{
			finish(-1);
			return;
		}

		_launched = true;

		if (on_started)
			on_started(_current);
	}

	void runner::add_output(const std::string_view raw, const bool from_stderr)
	{
		auto text = strip_ansi(raw);
		_parser.add_line(text);

		const output_line* stored = nullptr;

		if (_head.size() < head_lines)
		{
			stored = &_head.emplace_back(output_line{std::move(text), from_stderr});
		}
		else if (_tail.size() < tail_lines)
		{
			stored = &_tail.emplace_back(output_line{std::move(text), from_stderr});
		}
		else
		{
			_tail[_tail_next] = {std::move(text), from_stderr};
			stored = &_tail[_tail_next];
			_tail_next = (_tail_next + 1) % tail_lines;
			++_dropped;
		}

		if (on_output)
			on_output(stored->text);
	}

	void runner::finish(const int exit_code)
	{
		if (_current_id == 0)
			return;

		_parser.finish();

		result r;
		r.name = _current.name;
		r.tag = _current.tag;
		r.command_line = display_command_line(_current);
		r.working_dir = std::string(_current.working_dir.view());
		r.started = _launched;
		r.cancelled = _cancelling;
		r.exit_code = exit_code;
		r.elapsed_ms = now_ms() - _started_ms;
		r.dropped_lines = _dropped;
		r.output = _head;

		if (_dropped > 0)
			r.output.push_back({std::format("... {} lines omitted ...", _dropped), false});

		for (size_t i = 0; i < _tail.size(); ++i)
			r.output.push_back(_tail[(_tail_next + i) % _tail.size()]);

		r.diagnostics.assign(_parser.diagnostics().begin(), _parser.diagnostics().end());
		r.failed_targets.assign(_parser.failed_targets().begin(), _parser.failed_targets().end());

		_process.reset();
		_current_id = 0;
		_launched = false;
		_cancelling = false;

		if (on_finished)
			on_finished(r);

		start_next();
	}

	std::string powershell_literal(const std::string_view text)
	{
		std::string out = "'";

		for (const auto c : text)
		{
			if (c == '\'')
				out += '\'';

			out += c;
		}

		out += '\'';
		return out;
	}

	command probe_script_command(const pf::file_path& powershell, const pf::file_path& script)
	{
		// Walks the parameter AST with PowerShell's own grammar. Parsing a script is
		// not running it, so this is safe on a folder that was merely opened.
		const auto source = std::format(
			"$t=$null;$e=$null;"
			"$a=[System.Management.Automation.Language.Parser]::ParseFile({},[ref]$t,[ref]$e);"
			"$ps=@();"
			"if($a -and $a.ParamBlock){{"
			"$i=0;"
			"foreach($p in $a.ParamBlock.Parameters){{"
			"$v=@();"
			"foreach($at in $p.Attributes){{"
			"if($at -is [System.Management.Automation.Language.AttributeAst] -and $at.TypeName.Name -eq 'ValidateSet'){{"
			"foreach($x in $at.PositionalArguments){{if($x.PSObject.Properties.Name -contains 'Value'){{$v+=[string]$x.Value}}}}"
			"}}}}"
			"$ps+=@{{name=[string]$p.Name.VariablePath.UserPath;position=$i;values=@($v)}};"
			"$i++"
			"}}}}"
			"ConvertTo-Json -Depth 6 -Compress -InputObject @{{parameters=@($ps)}}",
			powershell_literal(script.view()));

		command cmd;
		cmd.name = "Read script parameters";
		cmd.tag = "probe";
		cmd.exe = powershell;
		cmd.working_dir = script.folder();
		cmd.args = {"-NoProfile", "-NonInteractive", "-Command", source};
		return cmd;
	}

	script_interface parse_script_interface(const std::string_view text)
	{
		script_interface result;
		const auto parsed = json::parse(text);

		if (!parsed)
			return result;

		const auto& parameters = parsed.root["parameters"];

		// ConvertTo-Json collapses a one-element array, so an object is also valid here
		std::vector<const json::value*> entries;

		if (parameters.is_array())
		{
			for (const auto& p : parameters.items())
				entries.push_back(&p);
		}
		else if (parameters.is_object())
		{
			entries.push_back(&parameters);
		}

		for (const auto* p : entries)
		{
			const auto name = (*p)["name"].text();

			if (name.empty())
				continue;

			std::vector<std::string> values;
			const auto& listed = (*p)["values"];

			if (listed.is_array())
			{
				for (const auto& item : listed.items())
					if (item.is_string())
						values.emplace_back(item.text());
			}
			else if (listed.is_string())
			{
				values.emplace_back(listed.text());
			}

			if (values.empty())
				continue;

			if ((*p)["position"].integer(-1) == 0 && result.commands.empty())
				result.commands = std::move(values);
			else
				result.options.push_back({std::string(name), std::move(values)});
		}

		return result;
	}

	command script_command(const pf::file_path& powershell,
	                       const pf::file_path& script,
	                       const std::string_view command_name,
	                       const std::span<const script_argument> arguments,
	                       const pf::file_path& working_dir)
	{
		command cmd;
		cmd.name = command_name.empty() ? std::string(script.view()) : std::string(command_name);
		cmd.exe = powershell;
		cmd.working_dir = working_dir;

		// An argument array, never an assembled shell string, and no execution policy override
		cmd.args = {"-NoProfile", "-NonInteractive", "-File", std::string(script.view())};

		if (!command_name.empty())
			cmd.args.emplace_back(command_name);

		for (const auto& argument : arguments)
		{
			cmd.args.push_back("-" + argument.name);
			cmd.args.push_back(argument.value);
		}

		return cmd;
	}
}
