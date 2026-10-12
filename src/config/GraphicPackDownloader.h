
#pragma once
#import <Foundation/Foundation.h>

@interface GraphicPackDownloader : NSObject

+ (NSString* _Nullable)extractGraphicPackZip:(NSString*)zipPath version:(NSString*)version;

@end
