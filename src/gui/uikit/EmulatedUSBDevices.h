#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, CemuUSBDevice) {
    CemuUSBDeviceSkylanders,
    CemuUSBDeviceInfinity,
    CemuUSBDeviceDimensions,
};

@interface CemuUSBFigure : NSObject
@property(nonatomic) uint32_t figureID;
@property(nonatomic) uint16_t variant;
@property(nonatomic, copy) NSString *name;
@end

@interface CemuEmulatedUSBDevices : NSObject
+ (NSArray<NSString *> *)slotNamesForDevice:(CemuUSBDevice)device NS_SWIFT_NAME(slotNames(for:));
+ (NSArray<CemuUSBFigure *> *)figuresForDevice:(CemuUSBDevice)device slot:(NSInteger)slot NS_SWIFT_NAME(figures(for:slot:));
+ (nullable NSString *)loadDevice:(CemuUSBDevice)device slot:(NSInteger)slot path:(NSString *)path NS_SWIFT_NAME(load(_:slot:path:));
+ (nullable NSString *)clearDevice:(CemuUSBDevice)device slot:(NSInteger)slot NS_SWIFT_NAME(clear(_:slot:));
+ (nullable NSString *)createDevice:(CemuUSBDevice)device figureID:(uint32_t)figureID variant:(uint16_t)variant path:(NSString *)path NS_SWIFT_NAME(create(_:figureID:variant:path:));
+ (nullable NSString *)moveDimensionsSlot:(NSInteger)source toSlot:(NSInteger)destination NS_SWIFT_NAME(moveDimensions(from:to:));
@end

NS_ASSUME_NONNULL_END
