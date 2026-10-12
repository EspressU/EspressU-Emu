#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace MlcTitleDirectoryCase
{
template<typename Log>
void Normalize(const std::filesystem::path& mlc, Log log)
{
    namespace fs = std::filesystem;
    const auto visit = [&](const auto& self, const fs::path& parent, int depth) -> void
    {
        std::error_code ec;
        fs::directory_iterator it(parent, ec);
        if (ec)
        {
            if (ec != std::errc::no_such_file_or_directory)
                log("scan failed", parent, ec.message());
            return;
        }

        std::vector<fs::path> entries;
        for (; it != fs::directory_iterator{}; it.increment(ec))
        {
            if (ec)
                break;
            entries.push_back(it->path());
        }

        if (ec)
        {
            log("scan failed", parent, ec.message());
            return;
        }

        for (auto source : entries)
        {
            auto name = source.filename().string();

            if (name.size() != 8 || name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                continue;

            const auto status = fs::symlink_status(source, ec);
            if (ec || !fs::is_directory(status))
                continue;

            auto lower = name;

            for (char& c : lower)
                if (c >= 'A' && c <= 'F')
                    c += 'a' - 'A';

            if (lower != name)
            {

                const auto target = parent / lower;
                const auto targetStatus = fs::symlink_status(target, ec);

                if (ec && ec != std::errc::no_such_file_or_directory)
                {
                    log("destination check failed", target, ec.message());
                    continue;
                }
                ec.clear();
                if (fs::exists(targetStatus))
                {
                    if (fs::is_symlink(targetStatus) || !fs::equivalent(source, target, ec) || ec)
                    {
                        log("conflict; skipped", source, target.string());

                        continue;
                    }
                }
                else
                {
                    fs::rename(source, target, ec);
                    if (ec)
                    {
                        log("rename failed", source, ec.message());

                        continue;
                    }
                    log("renamed", source, target.string());

                    source = target;
                }

            }
            if (depth > 1)
                self(self, source, depth - 1);
        }
    };
    visit(visit, mlc / "sys/title", 2);
}
}
