#include <unordered_map>
#include <string>
#include <vector>

#include "arg_parser.h"
#include "test_helpers.h"
#include "test_harness.h"

auto get_test_group(const std::string& name)
{
    auto it = test_groups().find(name);
    if (it == test_groups().end()) {
        std::fprintf(
            stderr, "No such test group: %s\n", name.c_str()
        );
        std::exit(EXIT_FAILURE);
    }

    return it;
}

auto get_test(const auto& group, const std::string& name)
{
    auto& cases = group->second.test_cases;

    auto it = std::find_if(
        cases.begin(), cases.end(),
        [&name](const auto* test) {
            return name == test->name;
        }
    );

    if (it == cases.end()) {
        std::fprintf(
            stderr, "No test %s inside group %s\n",
            name.c_str(), group->first.c_str()
        );
        std::exit(EXIT_FAILURE);
    }

    return *it;
}

int main(int argc, char **argv)
{
    ArgParser args {};
    args.add_param(
        "select", 's', "only run a specific test group{/case}"
    )
    .add_param(
        "ls", 'l', "show all tests inside a test group"
    )
    .add_help(
        "help", 'h', "Display this menu and exit",
        [&]() {
            std::puts("Ultra userspace test suite");

            std::cout << "Available test groups: ";
            bool first = true;

            for (auto &group : test_groups()) {
                if (!first)
                    std::cout << ", ";

                std::cout << group.first;
                first = false;
            }

            std::cout << "\n" << args;
        }
    );

    try {
        args.parse(argc, argv);
    } catch (const std::exception& e) {
        std::cout << "Unexpected error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (args.is_set("ls")) {
        const auto& name = args.get("ls");
        auto group = get_test_group(name);

        std::printf("Test group %s:\n", name.c_str());

        for (auto& test_case : group->second.test_cases)
            std::printf("  - %s\n", test_case->name);

        return EXIT_SUCCESS;
    }

    size_t total = 0, failed = 0;
    auto run_one = [&] (const struct test_case* test) {
        total++;

        try {
            auto guard = ScopeGuard(reset_phys_ranges);

            std::printf("Running test %s...", test->name);
            std::fflush(stdout);
            test->run();
            std::puts("OK");
        } catch (const std::exception& ex) {
            std::printf("FAILED (reason below)\n%s\n\n", ex.what());
            failed++;
        }
    };

    auto run_group = [&] (const std::string& name, const auto& group) {
        #define GROUP_HEADER "===== Tests for '%s' ====="
        std::printf(GROUP_HEADER"\n", name.c_str());

        for (auto* test : group.test_cases)
            run_one(test);

        // Subtract 2 for '%s' and 1 for \0
        std::string terminator(sizeof(GROUP_HEADER) - 3 + name.length(), '=');
        std::cout << terminator << "\n";

        if (group.teardown_callback)
            group.teardown_callback();
    };

    if (!args.is_set("select")) {
        for (auto& group : test_groups())
            run_group(group.first, group.second);
    } else {
        auto name = args.get("select");

        auto sep_idx = name.find("/");
        auto group = get_test_group(name.substr(0, sep_idx));

        if (sep_idx == std::string::npos) {
            run_group(group->first, group->second);
        } else {
            run_one(get_test(group, name.substr(sep_idx + 1)));
            if (group->second.teardown_callback)
                group->second.teardown_callback();
        }
    }

    std::cout << "Summary: ";

    if (failed == 0) {
        std::printf("all PASS! (%zu total)\n", total);
        return EXIT_SUCCESS;
    }

    std::printf("%zu/%zu (%zu failed)\n", total - failed, total, failed);
    return EXIT_FAILURE;
}
