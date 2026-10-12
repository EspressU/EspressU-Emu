// SPDX-License-Identifier: MPL-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface DevicePairingHost : NSObject

@property (class, readonly) NSString *serviceType;

- (nullable instancetype)initWithName:(NSString *)name failure:(NSString *_Nullable *_Nullable)failure;

@property (readonly) NSString *serviceName;
@property (readonly) uint16_t port;
@property (readonly) NSDictionary<NSString *, NSData *> *txtRecord;

- (void)runWithPIN:(void (^)(NSString *pin))pin
        completion:(void (^)(NSData *_Nullable pairingFile, NSString *_Nullable deviceName,
                             NSString *_Nullable failure, BOOL cancelled))completion;

- (void)cancel;

@end

NS_ASSUME_NONNULL_END
