#import "GraphicPackDownloader.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/ActiveSettings.h"
#include "Common/FileStream.h"
#include <zip.h>

static void deleteDownloadedPacks() {
    auto path = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks");
    std::error_code ec;
    if (!fs::exists(path, ec)) return;
    for (auto& p : fs::directory_iterator(path, ec)) {
        fs::remove_all(p.path(), ec);
    }
}

@implementation GraphicPackDownloader

+ (NSString* _Nullable)extractGraphicPackZip:(NSString*)zipPath version:(NSString*)version {
    NSData* zipData = [NSData dataWithContentsOfFile:zipPath];
    if (!zipData) {
        return @"Failed to read downloaded ZIP file";
    }

    zip_error_t zipError;
    zip_error_init(&zipError);

    zip_source_t* src = zip_source_buffer_create(zipData.bytes, zipData.length, 0, &zipError);
    if (!src) {
        zip_error_fini(&zipError);
        return @"Failed to read ZIP data";
    }

    zip_t* za = zip_open_from_source(src, 0, &zipError);
    if (!za) {
        zip_source_free(src);
        zip_error_fini(&zipError);
        return @"Failed to open ZIP archive";
    }

    auto basePath = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks");
    std::error_code ec;
    deleteDownloadedPacks();
    fs::create_directories(basePath, ec);

    int numEntries = (int)zip_get_num_entries(za, 0);
    for (int i = 0; i < numEntries; i++) {
        zip_stat_t sb{};
        if (zip_stat_index(za, i, 0, &sb) != 0) continue;

        if (std::strstr(sb.name, "../") || std::strstr(sb.name, "..\\")) continue;

        auto entryPath = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks/{}", sb.name);

        size_t nameLen = strlen(sb.name);
        if (nameLen == 0) continue;

        if (sb.name[nameLen - 1] == '/') {
            fs::create_directories(entryPath, ec);
            continue;
        }

        if (sb.size == 0 || sb.size > 128 * 1024 * 1024) continue;

        zip_file_t* zf = zip_fopen_index(za, i, 0);
        if (!zf) continue;

        std::vector<uint8_t> buf(sb.size);
        if (zip_fread(zf, buf.data(), sb.size) == (zip_int64_t)sb.size) {
            fs::create_directories(entryPath.parent_path(), ec);
            FileStream* outFile = FileStream::createFile2(entryPath);
            if (outFile) {
                outFile->writeData(buf.data(), buf.size());
                delete outFile;
            }
        }
        zip_fclose(zf);
    }

    zip_close(za);
    zip_error_fini(&zipError);

    auto versionPath = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks/version.txt");
    FileStream* vf = FileStream::createFile2(versionPath);
    if (vf) {
        vf->writeString(version.UTF8String);
        delete vf;
    }

    GraphicPack2::ClearGraphicPacks();
    GraphicPack2::LoadAll();

    return nil;
}

@end
