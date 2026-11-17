// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// diffcore command-line tool.

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

#include <cstdio>
#endif

#include "diffcore/diffcore.hpp"
#include "docgen.hpp"

namespace fs = std::filesystem;
using namespace diffcore;

namespace {

const char* const kUsage = R"USAGE(diffcore - line-based diff and patch

Usage:
  diffcore diff [options] OLD NEW
  diffcore stat [options] OLD NEW
  diffcore patch [options] FILE PATCHFILE
  diffcore bench --lines N --change-rate P [options]

diff    prints the differences between two files as a unified diff.
stat    counts the lines added and removed, and the hunks.
patch   applies a unified diff to FILE. If any hunk fails, nothing is written.
bench   diffs two pseudo-random documents and reports the time taken.

diff and stat options:
  -u, --unified        unified diff with 3 lines of context (the default)
  -U N, --unified=N    N lines of context
  -L, --label LABEL    use LABEL for the old file, then for the new file,
                       instead of the file name and modification time
      --max-cost N     give up when more than N lines differ (default 10000)

patch options:
  -R, --reverse        apply the patch in reverse
      --dry-run        report what would happen without writing anything
  -o, --output OUT     write the result to OUT instead of FILE

bench options:
      --lines N        lines in the old document
      --change-rate P  chance, from 0 to 1, that each line is changed
      --seed S         random seed (default 1)
      --vocab K        draw lines from K distinct texts (default: all distinct)
      --max-cost N     as for diff
      --save OLD NEW   also write the two documents to files

  -h, --help           show this help
      --version        show the version

Exit status: diff and stat exit with 0 if the files are the same, 1 if they
differ; patch exits with 0 if every hunk applied, 1 if any failed. All exit
with 2 on trouble.
)USAGE";

constexpr int kSame = 0;
constexpr int kDifferent = 1;
constexpr int kTrouble = 2;

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

bool starts_with(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// Command-line arguments after the command name.
class Args {
public:
    Args(int argc, char** argv) : args_(argv + 2, argv + argc) {}

    bool done() const { return pos_ >= args_.size(); }
    const std::string& current() const { return args_[pos_]; }
    void skip() { ++pos_; }

    bool flag(std::string_view short_name, std::string_view long_name) {
        const std::string& a = current();
        if (a.empty() || (a != short_name && a != long_name)) return false;
        ++pos_;
        return true;
    }

    // "--name VALUE" and "--name=VALUE"; with a short name, "-n VALUE" and
    // "-nVALUE".
    std::optional<std::string> value(std::string_view short_name, std::string_view long_name) {
        const std::string a = current();
        if (!long_name.empty()) {
            if (a == long_name) return next_value(a);
            if (starts_with(a, long_name) && a.size() > long_name.size() && a[long_name.size()] == '=') {
                ++pos_;
                return a.substr(long_name.size() + 1);
            }
        }
        if (!short_name.empty() && starts_with(a, short_name)) {
            if (a.size() == short_name.size()) return next_value(a);
            ++pos_;
            return a.substr(short_name.size());
        }
        return std::nullopt;
    }

    // The next argument, which is not an option.
    std::string operand() { return args_[pos_++]; }

private:
    std::string next_value(const std::string& option) {
        if (pos_ + 1 >= args_.size()) throw UsageError(option + " needs a value");
        pos_ += 2;
        return args_[pos_ - 1];
    }

    std::vector<std::string> args_;
    std::size_t pos_ = 0;
};

template <typename T>
T parse_number(const std::string& text, const std::string& what) {
    T value{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || text.empty()) throw UsageError("invalid " + what + ": '" + text + "'");
    return value;
}

std::string read_file(const std::string& path) {
    const fs::path p(path);
    std::error_code ec;
    if (fs::is_directory(p, ec)) throw std::runtime_error(path + ": is a directory");
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw std::runtime_error(path + (fs::exists(p, ec) ? ": cannot open file" : ": No such file or directory"));
    }
    std::string data;
    char buffer[1 << 16];
    while (in.read(buffer, sizeof buffer) || in.gcount() > 0) data.append(buffer, static_cast<std::size_t>(in.gcount()));
    if (in.bad()) throw std::runtime_error(path + ": read error");
    return data;
}

void write_file(const std::string& path, std::string_view data) {
    std::ofstream out(fs::path(path), std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error(path + ": cannot open for writing");
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    if (!out) throw std::runtime_error(path + ": write error");
}

void write_stdout(std::string_view data) {
    std::cout.write(data.data(), static_cast<std::streamsize>(data.size()));
    std::cout.flush();
}

// Gregorian date of a day count since 1970-01-01.
void civil_from_days(std::int64_t days, std::int64_t& year, unsigned& month, unsigned& day) {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const auto day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned shifted_month = (5 * day_of_year + 2) / 153;  // March is 0
    day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
    month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    year = static_cast<std::int64_t>(year_of_era) + era * 400 + (month <= 2 ? 1 : 0);
}

// "path<TAB>2026-11-17 09:30:00.123456789 +0000": the label diff -u gives a
// file. The time is shown in UTC.
std::string file_label(const std::string& path) {
    using namespace std::chrono;
    std::error_code ec;
    const fs::file_time_type mtime = fs::last_write_time(fs::path(path), ec);
    if (ec) return path;
    // The file clock's epoch is unspecified in C++17, but on every
    // implementation it differs from the system clock's by whole seconds.
    const auto file_now = duration_cast<microseconds>(fs::file_time_type::clock::now().time_since_epoch());
    const auto system_now = duration_cast<microseconds>(system_clock::now().time_since_epoch());
    const seconds epoch_shift = round<seconds>(file_now - system_now);
    const auto since_1970 = mtime.time_since_epoch() - epoch_shift;
    const seconds whole = floor<seconds>(since_1970);
    const auto nanos = duration_cast<nanoseconds>(since_1970 - whole).count();

    std::int64_t days = whole.count() / 86400;
    std::int64_t rest = whole.count() % 86400;
    if (rest < 0) {
        rest += 86400;
        --days;
    }
    std::int64_t year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civil_from_days(days, year, month, day);

    std::ostringstream out;
    out << path << '\t' << year << '-' << std::setfill('0') << std::setw(2) << month << '-' << std::setw(2) << day
        << ' ' << std::setw(2) << rest / 3600 << ':' << std::setw(2) << rest / 60 % 60 << ':' << std::setw(2)
        << rest % 60 << '.' << std::setw(9) << nanos << " +0000";
    return out.str();
}

struct DiffArgs {
    DiffOptions options;
    std::vector<std::string> labels;
    std::vector<std::string> files;
};

DiffArgs parse_diff_args(int argc, char** argv) {
    DiffArgs d;
    Args args(argc, argv);
    bool options_done = false;
    while (!args.done()) {
        const std::string& a = args.current();
        if (options_done || a == "-" || !starts_with(a, "-")) {
            d.files.push_back(args.operand());
        } else if (a == "--") {
            options_done = true;
            args.skip();
        } else if (args.flag("-u", "--unified")) {
            d.options.context = kDefaultContext;
        } else if (auto u = args.value("-U", "--unified")) {
            d.options.context = parse_number<std::size_t>(*u, "context length");
        } else if (auto label = args.value("-L", "--label")) {
            d.labels.push_back(*label);
        } else if (auto cost = args.value("", "--max-cost")) {
            d.options.max_cost = parse_number<std::size_t>(*cost, "--max-cost");
        } else {
            throw UsageError("unknown option '" + a + "'");
        }
    }
    if (d.files.size() != 2) throw UsageError("expected two files, OLD and NEW");
    if (d.labels.size() > 2) throw UsageError("--label given more than twice");
    return d;
}

int run_diff(int argc, char** argv) {
    const DiffArgs d = parse_diff_args(argc, argv);
    const std::string old_text = read_file(d.files[0]);
    const std::string new_text = read_file(d.files[1]);
    const FilePatch patch = make_patch(old_text, new_text, d.labels.size() > 0 ? d.labels[0] : file_label(d.files[0]),
                                       d.labels.size() > 1 ? d.labels[1] : file_label(d.files[1]), d.options);
    if (patch.hunks.empty()) return kSame;
    write_stdout(format_unified(patch));
    return kDifferent;
}

std::string count_of(std::size_t n, const char* singular, const char* plural) {
    return std::to_string(n) + ' ' + (n == 1 ? singular : plural);
}

int run_stat(int argc, char** argv) {
    const DiffArgs d = parse_diff_args(argc, argv);
    if (!d.labels.empty()) throw UsageError("stat takes no labels");
    const std::string old_text = read_file(d.files[0]);
    const std::string new_text = read_file(d.files[1]);
    const Lines a = split_lines(old_text);
    const Lines b = split_lines(new_text);
    const EditScript script = diff(a, b, d.options);
    const ChangeCounts counts = count_changes(script);
    const std::size_t hunks = make_hunks(script, d.options.context).size();
    std::cout << count_of(counts.inserted, "line", "lines") << " added, " << count_of(counts.deleted, "line", "lines")
              << " removed, " << count_of(hunks, "hunk", "hunks") << '\n';
    return hunks == 0 ? kSame : kDifferent;
}

int run_patch(int argc, char** argv) {
    ApplyOptions options;
    std::optional<std::string> output;
    std::vector<std::string> files;
    Args args(argc, argv);
    bool options_done = false;
    while (!args.done()) {
        const std::string& a = args.current();
        if (options_done || a == "-" || !starts_with(a, "-")) {
            files.push_back(args.operand());
        } else if (a == "--") {
            options_done = true;
            args.skip();
        } else if (args.flag("-R", "--reverse")) {
            options.reverse = true;
        } else if (args.flag("", "--dry-run")) {
            options.dry_run = true;
        } else if (auto out = args.value("-o", "--output")) {
            output = *out;
        } else {
            throw UsageError("unknown option '" + a + "'");
        }
    }
    if (files.size() != 2) throw UsageError("expected FILE and PATCHFILE");
    const std::string& target = files[0];
    const std::string& patch_path = files[1];

    const std::string patch_text = read_file(patch_path);
    Patch patch;
    try {
        patch = parse_patch(patch_text);
    } catch (const ParseError& e) {
        throw std::runtime_error(patch_path + ":" + std::to_string(e.line()) + ": " + e.message());
    }
    if (patch.files.empty()) throw std::runtime_error(patch_path + ": only garbage was found in the patch input");
    if (patch.files.size() > 1) {
        throw std::runtime_error(patch_path + ": the patch changes " + std::to_string(patch.files.size()) +
                                 " files; diffcore patch applies one file's changes");
    }
    const std::string text = read_file(target);
    const ApplyResult result = apply_patch(text, patch.files.front(), options);

    const std::string& destination = output ? *output : target;
    std::ostringstream report;
    report << (options.dry_run ? "checking file " : "patching file ") << destination;
    if (output) report << " (read from " << target << ')';
    report << '\n';
    for (std::size_t i = 0; i < result.hunks.size(); ++i) {
        const HunkResult& h = result.hunks[i];
        if (!h.applied) {
            report << "Hunk #" << i + 1 << " FAILED at " << h.line << ".\n";
        } else if (h.offset != 0) {
            report << "Hunk #" << i + 1 << " succeeded at " << h.line << " (offset " << h.offset
                   << (h.offset == 1 ? " line" : " lines") << ").\n";
        }
    }
    if (!result.ok()) {
        report << result.failed_count() << " out of " << count_of(result.hunks.size(), "hunk", "hunks") << " FAILED";
        if (!options.dry_run) report << " -- nothing written";
        report << '\n';
    }
    write_stdout(report.str());
    if (!result.ok()) return kDifferent;
    if (!options.dry_run) write_file(destination, result.output);
    return kSame;
}

int run_bench(int argc, char** argv) {
    docgen::Params params;
    bool have_lines = false;
    bool have_rate = false;
    std::uint64_t seed = 1;
    DiffOptions options;
    std::vector<std::string> save;
    Args args(argc, argv);
    while (!args.done()) {
        if (auto lines = args.value("", "--lines")) {
            params.lines = parse_number<std::size_t>(*lines, "--lines");
            have_lines = true;
        } else if (auto rate = args.value("", "--change-rate")) {
            params.change_rate = parse_number<double>(*rate, "--change-rate");
            if (!(params.change_rate >= 0.0 && params.change_rate <= 1.0)) {
                throw UsageError("--change-rate must be between 0 and 1");
            }
            have_rate = true;
        } else if (auto s = args.value("", "--seed")) {
            seed = parse_number<std::uint64_t>(*s, "--seed");
        } else if (auto vocab = args.value("", "--vocab")) {
            params.vocabulary = parse_number<std::size_t>(*vocab, "--vocab");
        } else if (auto cost = args.value("", "--max-cost")) {
            options.max_cost = parse_number<std::size_t>(*cost, "--max-cost");
        } else if (args.current() == "--save") {
            args.skip();
            for (int k = 0; k < 2; ++k) {
                if (args.done()) throw UsageError("--save needs two file names");
                save.push_back(args.operand());
            }
        } else {
            throw UsageError("unknown option '" + args.current() + "'");
        }
    }
    if (!have_lines || !have_rate) throw UsageError("bench needs --lines and --change-rate");

    const docgen::DocPair docs = docgen::generate(params, seed);
    if (!save.empty()) {
        write_file(save[0], docs.old_text);
        write_file(save[1], docs.new_text);
    }
    const auto start = std::chrono::steady_clock::now();
    const Lines a = split_lines(docs.old_text);
    const Lines b = split_lines(docs.new_text);
    const EditScript script = diff(a, b, options);
    const auto stop = std::chrono::steady_clock::now();
    const ChangeCounts counts = count_changes(script);
    const std::size_t hunks = make_hunks(script, options.context).size();
    const double ms = std::chrono::duration<double, std::milli>(stop - start).count();

    std::ostringstream out;
    out << "old: " << count_of(a.size(), "line", "lines") << ", new: " << count_of(b.size(), "line", "lines") << '\n'
        << "edit distance D: " << counts.distance() << " (" << counts.deleted << " deleted, " << counts.inserted
        << " inserted), " << count_of(hunks, "hunk", "hunks") << '\n'
        << "diff time: " << std::fixed << std::setprecision(2) << ms << " ms\n";
    write_stdout(out.str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // Diffs are bytes: no "\n" to "\r\n" translation on the way out.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    try {
        if (argc < 2) {
            std::cerr << kUsage;
            return kTrouble;
        }
        const std::string_view command = argv[1];
        if (command == "-h" || command == "--help" || command == "help") {
            std::cout << kUsage;
            return 0;
        }
        if (command == "--version") {
            std::cout << "diffcore " << kVersion << '\n';
            return 0;
        }
        if (command == "diff") return run_diff(argc, argv);
        if (command == "stat") return run_stat(argc, argv);
        if (command == "patch") return run_patch(argc, argv);
        if (command == "bench") return run_bench(argc, argv);
        throw UsageError("unknown command '" + std::string(command) + "'");
    } catch (const UsageError& e) {
        std::cerr << "diffcore: " << e.what() << "\nTry 'diffcore --help' for more information.\n";
    } catch (const std::exception& e) {
        std::cout.flush();
        std::cerr << "diffcore: " << e.what() << '\n';
    }
    return kTrouble;
}
