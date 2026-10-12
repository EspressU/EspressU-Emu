
#pragma once
#import <Foundation/Foundation.h>

@interface GraphicPackPresetEntry : NSObject
@property (nonatomic, copy) NSString* category;
@property (nonatomic, copy) NSString* name;
@property (nonatomic) BOOL active;
@property (nonatomic) BOOL visible;
@property (nonatomic) BOOL isDefault;
@end

@interface ObjCGraphicPackEntry : NSObject
@property (nonatomic, copy) NSString* normalizedPath;
@property (nonatomic, copy) NSString* name;
@property (nonatomic, copy) NSString* virtualPath;
@property (nonatomic, copy) NSString* packDescription;
@property (nonatomic) int version;
@property (nonatomic) BOOL enabled;
@property (nonatomic) BOOL activated;
@property (nonatomic) BOOL defaultEnabled;
@property (nonatomic, copy) NSArray<NSNumber*>* titleIds;
@property (nonatomic, copy) NSArray<GraphicPackPresetEntry*>* presets;
@property (nonatomic, copy) NSArray<NSString*>* presetCategories;
@end

@interface GraphicPackManager : NSObject

+ (instancetype)shared;

- (NSArray<ObjCGraphicPackEntry*>*)allPacks;
- (void)setEnabled:(BOOL)enabled forPack:(NSString*)normalizedPath;
- (void)setActivePreset:(NSString*)presetName category:(NSString*)category forPack:(NSString*)normalizedPath;
- (void)refreshPacks;
- (NSString*)graphicPacksBasePath;
- (NSString* _Nullable)installedVersion;

@end
