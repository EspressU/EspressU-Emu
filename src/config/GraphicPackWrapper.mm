#import <Foundation/Foundation.h>
#import "GraphicPackWrapper.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "CemuConfig.h"
#include "config/ActiveSettings.h"
#include "Common/FileStream.h"

@implementation GraphicPackPresetEntry
@end

@implementation ObjCGraphicPackEntry
@end

@implementation GraphicPackManager

+ (instancetype)shared {
    static GraphicPackManager* instance = nil;
    static dispatch_once_t token;
    dispatch_once(&token, ^{ instance = [[self alloc] init]; });
    return instance;
}

- (NSArray<ObjCGraphicPackEntry*>*)allPacks {
    NSMutableArray<ObjCGraphicPackEntry*>* result = [NSMutableArray new];

    for (const auto& gp : GraphicPack2::GetGraphicPacks()) {
        ObjCGraphicPackEntry* entry = [[ObjCGraphicPackEntry alloc] init];
        entry.normalizedPath = [NSString stringWithUTF8String:gp->GetNormalizedPathString().c_str()];
        entry.name = [NSString stringWithUTF8String:gp->GetName().c_str()];
        entry.virtualPath = [NSString stringWithUTF8String:gp->GetVirtualPath().c_str()];
        entry.packDescription = [NSString stringWithUTF8String:gp->GetDescription().c_str()];
        entry.version = gp->GetVersion();
        entry.enabled = gp->IsEnabled();
        entry.activated = gp->IsActivated();
        entry.defaultEnabled = gp->IsDefaultEnabled();

        NSMutableArray<NSNumber*>* titleIds = [NSMutableArray new];
        for (uint64_t tid : gp->GetTitleIds()) {
            [titleIds addObject:@(tid)];
        }
        entry.titleIds = titleIds;

        std::vector<std::string> categoryOrder;
        auto categorized = gp->GetCategorizedPresets(categoryOrder);

        NSMutableArray<NSString*>* categories = [NSMutableArray new];
        for (const auto& cat : categoryOrder) {
            [categories addObject:[NSString stringWithUTF8String:cat.c_str()]];
        }
        entry.presetCategories = categories;

        NSMutableArray<GraphicPackPresetEntry*>* presets = [NSMutableArray new];
        for (const auto& preset : gp->GetPresets()) {
            GraphicPackPresetEntry* pe = [[GraphicPackPresetEntry alloc] init];
            pe.category = [NSString stringWithUTF8String:preset->category.c_str()];
            pe.name = [NSString stringWithUTF8String:preset->name.c_str()];
            pe.active = preset->active;
            pe.visible = preset->visible;
            pe.isDefault = preset->is_default;
            [presets addObject:pe];
        }
        entry.presets = presets;

        [result addObject:entry];
    }

    return result;
}

- (void)setEnabled:(BOOL)enabled forPack:(NSString*)normalizedPath {
    std::string path = normalizedPath.UTF8String;
    for (const auto& gp : GraphicPack2::GetGraphicPacks()) {
        if (gp->GetNormalizedPathString() == path) {
            gp->SetEnabled(enabled);

            auto& entries = GetConfig().graphic_pack_entries;
            fs::path configPath(path);

            if (enabled) {
                entries[configPath].erase("_disabled");
                if (entries[configPath].empty() && !gp->GetPresets().empty()) {
                    for (const auto& preset : gp->GetPresets()) {
                        if (preset->active) {
                            entries[configPath][preset->category] = preset->name;
                        }
                    }
                }
            } else {
                entries[configPath]["_disabled"] = "true";
            }

            GetConfigHandle().Save();
            break;
        }
    }
}

- (void)setActivePreset:(NSString*)presetName category:(NSString*)category forPack:(NSString*)normalizedPath {
    std::string path = normalizedPath.UTF8String;
    std::string cat = category.UTF8String;
    std::string name = presetName.UTF8String;

    for (const auto& gp : GraphicPack2::GetGraphicPacks()) {
        if (gp->GetNormalizedPathString() == path) {
            gp->SetActivePreset(cat, name);

            auto& entries = GetConfig().graphic_pack_entries;
            fs::path configPath(path);
            entries[configPath][cat] = name;
            GetConfigHandle().Save();
            break;
        }
    }
}

- (void)refreshPacks {
    GraphicPack2::ClearGraphicPacks();
    GraphicPack2::LoadAll();
}

- (NSString*)graphicPacksBasePath {
    auto path = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks");
    return [NSString stringWithUTF8String:path.string().c_str()];
}

- (NSString* _Nullable)installedVersion {
    auto path = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks/version.txt");

    std::unique_ptr<FileStream> file(FileStream::openFile2(path));
    if (!file) return nil;

    std::string version;

    if (file->readLine(version)) {
        return [NSString stringWithUTF8String:version.c_str()];
    }
    return nil;
}

@end
