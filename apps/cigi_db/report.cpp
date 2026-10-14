#include "cigi_db/report.hpp"

#include <glaze/json.hpp>

namespace cigi::db {

ReportDiagnostic to_report(const mock::Diagnostic& d) {
    return {d.file, d.line, d.column, d.message};
}

ReportDatabase to_report(const mock::Database& d, bool duplicate) {
    ReportDatabase out;
    out.manifest = d.manifest.string();
    out.number = d.number;
    out.name = d.name;
    out.origin_lat = d.origin.lat_deg;
    out.origin_lon = d.origin.lon_deg;
    out.origin_alt = d.origin.alt_m;
    out.lat_min = d.bounds.lat_min_deg;
    out.lat_max = d.bounds.lat_max_deg;
    out.lon_min = d.bounds.lon_min_deg;
    out.lon_max = d.bounds.lon_max_deg;
    out.load_frames = d.load_frames;
    out.terrain = d.terrain.kind == mock::TerrainSpec::Kind::Heightmap ? "heightmap" : "synthetic";
    out.default_category = std::string(mock::to_string(d.default_category));
    out.duplicate = duplicate;
    for (const auto& m : d.materials) {
        out.materials.push_back({m.code, m.name, mock::to_hex(m.colour)});
    }
    for (const auto& e : d.entities) {
        out.entities.push_back({e.first_type, e.last_type, e.name,
                                std::string(mock::to_string(e.category)), e.lods, e.line});
    }
    return out;
}

namespace {

void add_warnings(Report& report, const mock::Database& d) {
    for (const auto& w : d.warnings) {
        report.warnings.push_back(to_report(w));
    }
}

} // namespace

Report report_for_catalog(const mock::DatabaseCatalog& catalog, const std::filesystem::path& root) {
    Report report;
    report.path = root.string();
    for (const auto& item : catalog.items()) {
        if (item.database) {
            report.databases.push_back(to_report(*item.database, item.duplicate));
            add_warnings(report, *item.database);
        }
        if (item.error) {
            report.errors.push_back(to_report(*item.error));
        }
    }
    for (const auto& d : catalog.duplicates()) {
        report.errors.push_back(to_report(d));
    }
    if (catalog.items().empty()) {
        report.errors.push_back(
            {root.string(), 0, 0,
             "no database.toml found (expected a database folder or a root of them)"});
    }
    report.ok = report.errors.empty();
    return report;
}

Report report_for_manifest(const std::filesystem::path& manifest) {
    Report report;
    report.path = manifest.string();
    auto db = mock::load_database(manifest);
    if (db) {
        report.databases.push_back(to_report(*db));
        add_warnings(report, *db);
    } else {
        report.errors.push_back(to_report(db.error()));
    }
    report.ok = report.errors.empty();
    return report;
}

Report report_for_failure(const std::filesystem::path& path, const mock::Diagnostic& error) {
    Report report;
    report.path = path.string();
    report.errors.push_back(to_report(error));
    report.ok = false;
    return report;
}

std::string to_json(const Report& report) {
    std::string out;
    if (const auto ec = glz::write<glz::opts{.prettify = true}>(report, out)) {
        return {};
    }
    return out;
}

std::expected<Report, std::string> report_from_json(std::string_view json) {
    Report report;
    const auto ec = glz::read_json(report, json);
    if (ec) {
        return std::unexpected(glz::format_error(ec, json));
    }
    return report;
}

} // namespace cigi::db
