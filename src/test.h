// test.h — Lightweight test framework: assertions and test runner

#pragma once

class should
{
public:
	static void is_equal(const std::string_view expected, const std::string_view actual,
	                     const std::string_view message = "Test")
	{
		if (actual != expected)
		{
			throw std::format("{}: expected '{}', got '{}'", message, expected, actual);
		}
	}

	static void is_equal(const int expected, const int actual, const std::string_view message = "Test")
	{
		is_equal(to_str(expected), to_str(actual), message);
	}

	static void is_equal(const size_t expected, const size_t actual, const std::string_view message = "Test")
	{
		if (actual != expected)
		{
			throw std::format("{}: expected '{}', got '{}'", message, expected, actual);
		}
	}

	static void is_equal(const bool expected, const bool actual, const std::string_view message = "Test")
	{
		is_equal(to_str(expected), to_str(actual), message);
	}

	static void is_equal_true(const bool actual, const std::string_view message = "Test")
	{
		is_equal(true, actual, message);
	}
};

class tests
{
public:
	struct run_result
	{
		std::string output;
		int pass_count = 0;
		int fail_count = 0;
	};

private:
	static std::chrono::high_resolution_clock::time_point now()
	{
		return std::chrono::high_resolution_clock::now();
	}

	static long long duration_in_microseconds(const std::chrono::high_resolution_clock::time_point& started)
	{
		const auto dur = now() - started;
		return std::chrono::duration_cast<std::chrono::microseconds>(dur).count();
	}

	std::map<std::string_view, std::function<void()>> _tests;

public:
	void register_test(const std::string_view name, const std::function<void()>& f)
	{
		_tests[name] = f;
	}

	run_result run_all_result() const
	{
		const auto all_started = now();
		auto pass_count = 0;
		auto fail_count = 0;
		std::string details;

		for (auto& test : _tests)
		{
			details += std::format("Running '{}' ... ", test.first);
			auto test_started = now();

			try
			{
				test.second();
				details += std::format(" success in {} microseconds\n", duration_in_microseconds(test_started));
				pass_count += 1;
			}
			catch (const std::string& message)
			{
				details += std::format(" FAILED in {} microseconds\n\n{}\n\n", duration_in_microseconds(test_started),
				                       message);
				fail_count += 1;
			}
			catch (const std::exception& e)
			{
				details += std::format(" FAILED in {} microseconds\n\n{}\n\n", duration_in_microseconds(test_started),
				                       std::string_view(e.what()));
				fail_count += 1;
			}
			catch (...)
			{
				details += std::format(" FAILED in {} microseconds\n\nunknown exception\n\n",
				                       duration_in_microseconds(test_started));
				fail_count += 1;
			}
		}

		const auto total = pass_count + fail_count;
		const auto elapsed = duration_in_microseconds(all_started);

		run_result result;
		result.pass_count = pass_count;
		result.fail_count = fail_count;
		if (fail_count == 0)
			result.output += std::format("**{} tests passed** in {} microseconds\n", total, elapsed);
		else
			result.output += std::format("**{} of {} tests failed** in {} microseconds\n", fail_count, total,
			                             elapsed);

		result.output += "\n";
		result.output += details;
		return result;
	}

	std::string run_all() const
	{
		return run_all_result().output;
	}
};

// Lives in tests_cpp.cpp, which holds the C++ tooling suites
void register_cpp_tests(tests& suite);
