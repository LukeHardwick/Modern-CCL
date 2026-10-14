#pragma once

// The machine-readable report of `cigi_db validate --json` (JSON, written and read with glaze in
// report.cpp; nothing in this header depends on glaze). Plain aggregates, so the JSON field names
// are the member names.

#include "mock_ig/database.hpp"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cigi::db {

struct ReportDiagnostic {
    std::string file;
    std::uint32_t line = 0; ///< 0: not known.
    std::uint32_t column = 0;
    std::string message;
    bool operator==(const ReportDiagnostic&) const = default;
};

struct ReportEntity {
    std::uint16_t first_type = 0;
    std::uint16_t last_type = 0;
    std::string name;
    std::string category;
    std::vector<std::string> lods;
    std::uint32_t line = 0;
    bool operator==(const ReportEntity&) const = default;
};

struct ReportMaterial {
    std::uint32_t code = 0;
    std::string name;
    std::string colour; ///< "#rrggbb".
    bool operator==(const ReportMaterial&) const = default;
};

struct ReportDatabase {
    std::string manifest;
    int number = 0;
    std::string name;
    double origin_lat = 0;
    double origin_lon = 0;
    double origin_alt = 0;
    double lat_min = 0;
    double lat_max = 0;
    double lon_min = 0;
    double lon_max = 0;
    std::uint32_t load_frames = 0;
    std::string terrain; ///< "synthetic" or "heightmap".
    std::string default_category;
    bool duplicate = false; ///< Its number is already used by an earlier manifest.
    std::vector<ReportMaterial> materials;
    std::vector<ReportEntity> entities;
    bool operator==(const ReportDatabase&) const = default;
};

struct Report {
    std::string tool = "cigi_db";
    int format = 1;   ///< Bumped when the JSON layout changes incompatibly.
    std::string path; ///< What was validated (a root folder or one manifest).
    bool ok = true;   ///< No errors (warnings do not count).
    std::vector<ReportDatabase> databases;
    std::vector<ReportDiagnostic> errors;
    std::vector<ReportDiagnostic> warnings;
    bool operator==(const Report&) const = default;
};

[[nodiscard]] ReportDiagnostic to_report(const mock::Diagnostic& d);
[[nodiscard]] ReportDatabase to_report(const mock::Database& d, bool duplicate = false);

/// Report on a database root (every database under it, duplicates and failures included).
[[nodiscard]] Report report_for_catalog(const mock::DatabaseCatalog& catalog,
                                        const std::filesystem::path& root);
/// Report on one manifest.
[[nodiscard]] Report report_for_manifest(const std::filesystem::path& manifest);
/// A report that only carries an error (e.g. the path does not exist).
[[nodiscard]] Report report_for_failure(const std::filesystem::path& path,
                                        const mock::Diagnostic& error);

/// Pretty-printed JSON; empty string if glaze cannot serialise (never in practice).
[[nodiscard]] std::string to_json(const Report& report);
/// Parse JSON written by to_json(); the error is glaze's message.
[[nodiscard]] std::expected<Report, std::string> report_from_json(std::string_view json);

} // namespace cigi::db
