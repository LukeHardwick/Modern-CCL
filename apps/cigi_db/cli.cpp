#include "cigi_db/cli.hpp"

#include "cigi_db/report.hpp"
#include "mock_ig/database.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ostream>
#include <string>

namespace cigi::db {

namespace fs = std::filesystem;

namespace {

constexpr int exit_ok = 0;
constexpr int exit_errors = 1;
constexpr int exit_usage = 2;

void print_diagnostics(std::ostream& out, std::string_view label,
                       const std::vector<ReportDiagnostic>& list) {
    for (const auto& d : list) {
        out << label << ": "
            << mock::to_string(mock::Diagnostic{d.file, d.line, d.column, d.message}) << '\n';
    }
}

void print_database(std::ostream& out, const ReportDatabase& d) {
    out << std::format("database {} \"{}\" ({}): {} entity entries, {} materials, terrain {}, "
                       "load {} frames{}\n",
                       d.number, d.name, d.manifest, d.entities.size(), d.materials.size(),
                       d.terrain, d.load_frames, d.duplicate ? " [DUPLICATE NUMBER]" : "");
}

int finish_validate(const Report& report, const std::optional<std::string_view>& json_path,
                    std::ostream& out, std::ostream& err) {
    for (const auto& d : report.databases) {
        print_database(out, d);
    }
    print_diagnostics(err, "warning", report.warnings);
    print_diagnostics(err, "error", report.errors);
    out << std::format("{}: {} database(s), {} error(s), {} warning(s)\n",
                       report.ok ? "OK" : "FAILED", report.databases.size(), report.errors.size(),
                       report.warnings.size());
    if (json_path) {
        std::ofstream file{fs::path(*json_path), std::ios::binary};
        file << to_json(report);
        if (!file) {
            err << "error: cannot write " << *json_path << '\n';
            return exit_errors;
        }
    }
    return report.ok ? exit_ok : exit_errors;
}

int validate(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    std::optional<std::string_view> path;
    std::optional<std::string_view> json_path;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--json") {
            if (i + 1 >= args.size() || json_path) {
                err << "error: --json needs exactly one file name\n" << usage();
                return exit_usage;
            }
            json_path = args[++i];
        } else if (!args[i].empty() && args[i].front() == '-') {
            err << "error: unknown option " << args[i] << '\n' << usage();
            return exit_usage;
        } else if (path) {
            err << "error: only one path may be given\n" << usage();
            return exit_usage;
        } else {
            path = args[i];
        }
    }
    if (!path) {
        err << "error: validate needs a database root or manifest\n" << usage();
        return exit_usage;
    }

    const fs::path target{*path};
    std::error_code ec;
    if (fs::is_directory(target, ec)) {
        mock::DatabaseCatalog catalog;
        if (auto scanned = catalog.scan(target); !scanned) {
            return finish_validate(report_for_failure(target, scanned.error()), json_path, out,
                                   err);
        }
        return finish_validate(report_for_catalog(catalog, target), json_path, out, err);
    }
    if (!fs::is_regular_file(target, ec)) {
        return finish_validate(
            report_for_failure(target, {target.string(), 0, 0, "no such file or folder"}),
            json_path, out, err);
    }
    return finish_validate(report_for_manifest(target), json_path, out, err);
}

int list(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    if (args.size() != 1 || args[0].starts_with('-')) {
        err << "error: list needs exactly one database root\n" << usage();
        return exit_usage;
    }
    mock::DatabaseCatalog catalog;
    if (auto scanned = catalog.scan(fs::path(args[0])); !scanned) {
        err << "error: " << mock::to_string(scanned.error()) << '\n';
        return exit_errors;
    }
    out << std::format("{:>3}  {:<28} {:>8} {:>6} {}\n", "no.", "name", "entities", "frames",
                       "manifest");
    for (const auto& item : catalog.items()) {
        if (item.database) {
            const auto& d = *item.database;
            out << std::format("{:>3}  {:<28} {:>8} {:>6} {}{}\n", d.number, d.name,
                               d.entities.size(), d.load_frames, item.manifest.string(),
                               item.duplicate ? "  [duplicate number, unused]" : "");
        } else {
            out << std::format("{:>3}  {:<28} {:>8} {:>6} {}  [ERROR: {}]\n", "-", "-", "-", "-",
                               item.manifest.string(), mock::to_string(*item.error));
        }
    }
    if (catalog.items().empty()) {
        out << "(no databases)\n";
    }
    return exit_ok;
}

} // namespace

std::string_view usage() noexcept {
    return "usage: cigi_db validate <root-or-manifest> [--json FILE]\n"
           "       cigi_db list <root>\n"
           "validate checks the database.toml files (errors with line numbers; exit code 1 on "
           "errors) and\n"
           "optionally writes the report as JSON; list shows the databases under a root.\n";
}

int run(std::span<const std::string_view> args, std::ostream& out, std::ostream& err) {
    if (args.empty()) {
        err << usage();
        return exit_usage;
    }
    const std::string_view command = args[0];
    if (command == "--help" || command == "-h" || command == "help") {
        out << usage();
        return exit_ok;
    }
    if (command == "validate") {
        return validate(args.subspan(1), out, err);
    }
    if (command == "list") {
        return list(args.subspan(1), out, err);
    }
    err << "error: unknown command '" << command << "'\n" << usage();
    return exit_usage;
}

} // namespace cigi::db
