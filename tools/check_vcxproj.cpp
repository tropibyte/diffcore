// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Checks msvc/diffcore.vcxproj and its .filters file: well-formed XML,
// written as Visual Studio writes them (UTF-8 BOM, CRLF), listing exactly
// the files in src/ and include/diffcore/, with the configurations and
// compiler settings the project must keep.
//
//   check_vcxproj <source dir>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Element {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Element> children;
    std::string text;

    const std::string* attribute(std::string_view key) const {
        for (const auto& a : attributes) {
            if (a.first == key) return &a.second;
        }
        return nullptr;
    }
    const Element* child(std::string_view key) const {
        for (const Element& c : children) {
            if (c.name == key) return &c;
        }
        return nullptr;
    }
};

// Enough XML for project files: elements, attributes, character data,
// entities, comments, CDATA and processing instructions.
class XmlParser {
public:
    explicit XmlParser(std::string_view text) : s_(text) {}

    Element parse_document() {
        skip_misc();
        if (!at("<")) fail("expected the root element");
        Element root = parse_element();
        skip_misc();
        if (pos_ != s_.size()) fail("content after the root element");
        return root;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        const auto line = std::count(s_.begin(), s_.begin() + static_cast<std::ptrdiff_t>(pos_), '\n') + 1;
        throw std::runtime_error("XML error at line " + std::to_string(line) + ": " + what);
    }

    bool at(std::string_view token) const { return s_.substr(pos_, token.size()) == token; }

    void expect(std::string_view token) {
        if (!at(token)) fail("expected '" + std::string(token) + "'");
        pos_ += token.size();
    }

    void skip_until(std::string_view end) {
        const std::size_t found = s_.find(end, pos_);
        if (found == std::string_view::npos) fail("missing '" + std::string(end) + "'");
        pos_ = found + end.size();
    }

    void skip_space() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    void skip_misc() {
        for (;;) {
            skip_space();
            if (at("<?")) {
                skip_until("?>");
            } else if (at("<!--")) {
                skip_until("-->");
            } else {
                return;
            }
        }
    }

    static bool name_char(char c, bool first) {
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
        return letter || (!first && ((c >= '0' && c <= '9') || c == '-' || c == '.'));
    }

    std::string parse_name() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && name_char(s_[pos_], pos_ == start)) ++pos_;
        if (pos_ == start) fail("expected a name");
        return std::string(s_.substr(start, pos_ - start));
    }

    std::string decode(std::string_view raw) const {
        static const std::map<std::string, std::string, std::less<>> kEntities = {
            {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}};
        std::string out;
        std::size_t i = 0;
        while (i < raw.size()) {
            if (raw[i] != '&') {
                out += raw[i++];
                continue;
            }
            const std::size_t semi = raw.find(';', i);
            if (semi == std::string_view::npos) fail("unterminated entity reference");
            const std::string_view name = raw.substr(i + 1, semi - i - 1);
            if (!name.empty() && name.front() == '#') {
                out += '?';  // character references are not needed here
            } else {
                const auto it = kEntities.find(name);
                if (it == kEntities.end()) fail("unknown entity '&" + std::string(name) + ";'");
                out += it->second;
            }
            i = semi + 1;
        }
        return out;
    }

    Element parse_element() {
        Element e;
        expect("<");
        e.name = parse_name();
        for (;;) {
            skip_space();
            if (at("/>")) {
                pos_ += 2;
                return e;
            }
            if (at(">")) {
                ++pos_;
                break;
            }
            std::string key = parse_name();
            skip_space();
            expect("=");
            skip_space();
            if (pos_ >= s_.size() || (s_[pos_] != '"' && s_[pos_] != '\'')) fail("expected a quoted attribute value");
            const char quote = s_[pos_++];
            const std::size_t end = s_.find(quote, pos_);
            if (end == std::string_view::npos) fail("unterminated attribute value");
            const std::string_view raw = s_.substr(pos_, end - pos_);
            if (raw.find('<') != std::string_view::npos) fail("'<' in an attribute value");
            if (e.attribute(key) != nullptr) fail("duplicate attribute '" + key + "'");
            e.attributes.emplace_back(std::move(key), decode(raw));
            pos_ = end + 1;
        }
        for (;;) {
            if (pos_ >= s_.size()) fail("element <" + e.name + "> is not closed");
            if (at("</")) {
                pos_ += 2;
                const std::string close = parse_name();
                if (close != e.name) fail("</" + close + "> closes <" + e.name + ">");
                skip_space();
                expect(">");
                return e;
            }
            if (at("<!--")) {
                skip_until("-->");
            } else if (at("<![CDATA[")) {
                pos_ += 9;
                const std::size_t end = s_.find("]]>", pos_);
                if (end == std::string_view::npos) fail("unterminated CDATA section");
                e.text += s_.substr(pos_, end - pos_);
                pos_ = end + 3;
            } else if (at("<?")) {
                skip_until("?>");
            } else if (at("<")) {
                e.children.push_back(parse_element());
            } else {
                const std::size_t end = (std::min)(s_.find('<', pos_), s_.size());
                e.text += decode(s_.substr(pos_, end - pos_));
                pos_ = end;
            }
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

class Checker {
public:
    explicit Checker(fs::path root) : root_(std::move(root)) {}

    int run() {
        const std::set<std::string> sources = list_files("src", ".cpp");
        const std::set<std::string> headers = list_files("include/diffcore", ".hpp");
        if (sources.empty() || headers.empty()) problem("found no sources or headers under " + root_.string());

        if (const auto project = load("msvc/diffcore.vcxproj")) {
            compare_lists("diffcore.vcxproj", "ClCompile", items(*project, "ClCompile"), sources);
            compare_lists("diffcore.vcxproj", "ClInclude", items(*project, "ClInclude"), headers);
            check_settings(*project);
        }
        if (const auto filters = load("msvc/diffcore.vcxproj.filters")) {
            compare_lists("diffcore.vcxproj.filters", "ClCompile", items(*filters, "ClCompile"), sources);
            compare_lists("diffcore.vcxproj.filters", "ClInclude", items(*filters, "ClInclude"), headers);
        }
        for (const std::string& p : problems_) std::cerr << "check_vcxproj: " << p << '\n';
        if (problems_.empty()) std::cout << "msvc project files are in sync with src/ and include/diffcore/\n";
        return problems_.empty() ? 0 : 1;
    }

private:
    void problem(std::string text) { problems_.push_back(std::move(text)); }

    // Files with the extension in a directory, as the project refers to
    // them from msvc/: "..\src\apply.cpp".
    std::set<std::string> list_files(const std::string& dir, const std::string& extension) {
        std::set<std::string> out;
        std::error_code ec;
        for (fs::directory_iterator it(root_ / dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file() || it->path().extension().string() != extension) continue;
            std::string relative = "..\\" + dir + "\\" + it->path().filename().string();
            std::replace(relative.begin(), relative.end(), '/', '\\');
            out.insert(relative);
        }
        if (ec) problem("cannot list " + (root_ / dir).string() + ": " + ec.message());
        return out;
    }

    std::optional<Element> load(const std::string& relative) {
        std::ifstream in(root_ / relative, std::ios::binary);
        if (!in) {
            problem("cannot read " + relative);
            return std::nullopt;
        }
        const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string_view bom = "\xEF\xBB\xBF";
        if (data.compare(0, bom.size(), bom) != 0) problem(relative + ": does not start with a UTF-8 byte order mark");
        for (std::size_t i = 0; i < data.size(); ++i) {
            const bool lone_lf = data[i] == '\n' && (i == 0 || data[i - 1] != '\r');
            const bool lone_cr = data[i] == '\r' && (i + 1 == data.size() || data[i + 1] != '\n');
            if (lone_lf || lone_cr) {
                problem(relative + ": line endings must be CRLF throughout");
                break;
            }
        }
        try {
            const std::string_view body = std::string_view(data).substr(data.compare(0, 3, bom) == 0 ? 3 : 0);
            return XmlParser(body).parse_document();
        } catch (const std::exception& e) {
            problem(relative + ": " + e.what());
            return std::nullopt;
        }
    }

    static void collect(const Element& e, std::string_view name, std::vector<const Element*>& out) {
        if (e.name == name) out.push_back(&e);
        for (const Element& c : e.children) collect(c, name, out);
    }

    static std::vector<const Element*> all(const Element& root, std::string_view name) {
        std::vector<const Element*> out;
        collect(root, name, out);
        return out;
    }

    // Include="..." of every <name> element inside an <ItemGroup>.
    static std::set<std::string> items(const Element& root, std::string_view name) {
        std::set<std::string> out;
        for (const Element* group : all(root, "ItemGroup")) {
            for (const Element& c : group->children) {
                if (c.name != name) continue;
                if (const std::string* include = c.attribute("Include")) out.insert(*include);
            }
        }
        return out;
    }

    void compare_lists(const std::string& file, const std::string& kind, const std::set<std::string>& listed,
                       const std::set<std::string>& on_disk) {
        for (const std::string& f : on_disk) {
            if (!listed.count(f)) problem(file + ": " + kind + " is missing " + f);
        }
        for (const std::string& f : listed) {
            if (!on_disk.count(f)) problem(file + ": " + kind + " lists " + f + ", which does not exist");
        }
    }

    // "Debug|x64" from Condition="'$(Configuration)|$(Platform)'=='Debug|x64'".
    static std::string condition_config(const Element& e) {
        const std::string* condition = e.attribute("Condition");
        if (condition == nullptr) return {};
        const std::size_t eq = condition->find("=='");
        if (eq == std::string::npos) return {};
        const std::size_t end = condition->find('\'', eq + 3);
        return end == std::string::npos ? std::string{} : condition->substr(eq + 3, end - eq - 3);
    }

    static std::string text_of(const Element& parent, std::string_view name) {
        const Element* c = parent.child(name);
        return c == nullptr ? std::string{} : c->text;
    }

    void expect_value(const std::string& where, const Element& parent, std::string_view name,
                      const std::string& wanted) {
        const std::string actual = text_of(parent, name);
        if (actual != wanted) {
            problem("diffcore.vcxproj: " + where + ": " + std::string(name) + " is '" + actual + "', expected '" +
                    wanted + "'");
        }
    }

    void expect_contains(const std::string& where, const Element& parent, std::string_view name,
                         const std::string& wanted) {
        if (text_of(parent, name).find(wanted) == std::string::npos) {
            problem("diffcore.vcxproj: " + where + ": " + std::string(name) + " lacks " + wanted);
        }
    }

    void check_settings(const Element& project) {
        const std::set<std::string> wanted = {"Debug|Win32", "Release|Win32", "Debug|x64", "Release|x64"};
        std::set<std::string> configs;
        for (const Element* c : all(project, "ProjectConfiguration")) {
            if (const std::string* include = c->attribute("Include")) configs.insert(*include);
        }
        if (configs != wanted) problem("diffcore.vcxproj: configurations must be exactly Debug/Release x Win32/x64");

        for (const Element* group : all(project, "PropertyGroup")) {
            const std::string* label = group->attribute("Label");
            if (label != nullptr && *label == "Globals") {
                expect_value("Globals", *group, "ProjectGuid", "{D6580229-C7F3-4E80-BD7E-7B57A8F1AE25}");
                expect_value("Globals", *group, "RootNamespace", "diffcore");
                expect_value("Globals", *group, "WindowsTargetPlatformVersion", "10.0");
            }
            if (label != nullptr && *label == "Configuration") {
                const std::string where = condition_config(*group);
                expect_value(where, *group, "ConfigurationType", "StaticLibrary");
                expect_value(where, *group, "PlatformToolset", "v143");
                expect_value(where, *group, "CharacterSet", "Unicode");
            }
        }

        std::set<std::string> seen;
        for (const Element* group : all(project, "ItemDefinitionGroup")) {
            const std::string config = condition_config(*group);
            const Element* cl = group->child("ClCompile");
            if (cl == nullptr) {
                problem("diffcore.vcxproj: " + config + ": no ClCompile settings");
                continue;
            }
            seen.insert(config);
            // x64 links the static CRT; Win32 keeps the compiler's default.
            const bool x64 = config.find("|x64") != std::string::npos;
            const bool debug = config.rfind("Debug|", 0) == 0;
            const std::string runtime = x64 ? (debug ? "MultiThreadedDebug" : "MultiThreaded") : "";
            expect_value(config, *cl, "RuntimeLibrary", runtime);
            expect_value(config, *cl, "LanguageStandard", "stdcpp17");
            expect_value(config, *cl, "WarningLevel", "Level4");
            expect_value(config, *cl, "ConformanceMode", "true");
            expect_contains(config, *cl, "PreprocessorDefinitions", "_HAS_STD_BYTE=0");
            expect_contains(config, *cl, "AdditionalIncludeDirectories", "$(ProjectDir)..\\include");
            for (const char* option : {"/utf-8", "/Zc:__cplusplus", "/Zc:preprocessor", "/MP"}) {
                expect_contains(config, *cl, "AdditionalOptions", option);
            }
        }
        if (seen != wanted) problem("diffcore.vcxproj: every configuration needs an ItemDefinitionGroup");
    }

    fs::path root_;
    std::vector<std::string> problems_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: check_vcxproj <source dir>\n";
        return 2;
    }
    try {
        return Checker(argv[1]).run();
    } catch (const std::exception& e) {
        std::cerr << "check_vcxproj: " << e.what() << '\n';
        return 2;
    }
}
