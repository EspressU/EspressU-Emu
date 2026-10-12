// SPDX-License-Identifier: MPL-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^StikJITHelperReply)(NSDictionary<NSString *, id> *_Nullable reply, NSString *_Nullable failure);

@interface StikJITHelperLauncher : NSObject

+ (nullable NSString *)helperIdentifier;

+ (BOOL)hostHasGetTaskAllow;

+ (BOOL)isBusy;

+ (BOOL)sendRequest:(NSDictionary<NSString *, id> *)request
              reply:(StikJITHelperReply)reply
            failure:(NSString *_Nullable *_Nullable)failure;

+ (void)cancelWithReason:(NSString *)reason;

+ (id)observeStagesWithPrefix:(NSString *)prefix
                       stages:(NSArray<NSString *> *)stages
                      handler:(void (^)(NSString *stage))handler;
+ (void)stopObserving:(id)token;

@end

NS_ASSUME_NONNULL_END
