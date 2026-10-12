// SPDX-License-Identifier: MPL-2.0

#import "StikJITHelperLauncher.h"
#import <dlfcn.h>

@protocol SJHExtension <NSObject>
+ (nullable instancetype)extensionWithIdentifier:(NSString *)identifier error:(NSError **)error;
- (void)beginExtensionRequestWithInputItems:(NSArray *)inputItems completion:(void (^)(NSUUID *_Nullable requestIdentifier))completion;
- (void)cancelExtensionRequestWithIdentifier:(NSUUID *)requestIdentifier;
- (void)setRequestCompletionBlock:(void (^)(NSUUID *requestIdentifier, NSArray *extensionItems))block;
- (void)setRequestCancellationBlock:(void (^)(NSUUID *requestIdentifier, NSError *error))block;
- (void)setRequestInterruptionBlock:(void (^)(NSUUID *requestIdentifier))block;
@end

@interface SJHStageObserver : NSObject
@property (nonatomic, copy) NSString *prefix;
@property (nonatomic, copy) NSArray<NSString *> *names;
@property (nonatomic, copy) void (^handler)(NSString *stage);
@end
@implementation SJHStageObserver
@end

static id<SJHExtension> sExtension;
static NSUUID *sRequestID;
static StikJITHelperReply sReply;
static BOOL sOpen;
static NSUInteger sGeneration;

static void SJHFinish(NSUInteger generation, NSDictionary *reply, NSString *failure)
{
    if (!sOpen || generation != sGeneration)
        return;
    sOpen = NO;
    StikJITHelperReply callback = sReply;
    sReply = nil;
    sRequestID = nil;
    sExtension = nil;
    if (callback)
        callback(reply, failure);
}

static void SJHStageCallback(CFNotificationCenterRef center, void *observer, CFNotificationName name,
                             const void *object, CFDictionaryRef userInfo)
{
    SJHStageObserver *stageObserver = (__bridge SJHStageObserver *)observer;
    NSString *full = (__bridge NSString *)name;
    NSString *prefix = [stageObserver.prefix stringByAppendingString:@"."];
    if (![full hasPrefix:prefix])
        return;
    NSString *stage = [full substringFromIndex:prefix.length];
    void (^handler)(NSString *) = stageObserver.handler;
    dispatch_async(dispatch_get_main_queue(), ^{ handler(stage); });
}

@implementation StikJITHelperLauncher

+ (NSString *)helperIdentifier
{
    NSURL *plugIns = NSBundle.mainBundle.builtInPlugInsURL;
    if (!plugIns)
        return nil;
    NSArray<NSURL *> *entries = [NSFileManager.defaultManager contentsOfDirectoryAtURL:plugIns
                                                            includingPropertiesForKeys:nil
                                                                               options:0
                                                                                 error:nil];
    for (NSURL *entry in entries)
    {
        if (![entry.lastPathComponent isEqualToString:@"StikJITHelper.appex"])
            continue;
        NSBundle *bundle = [NSBundle bundleWithURL:entry];
        if (bundle.bundleIdentifier.length)
            return bundle.bundleIdentifier;
    }
    return nil;
}

+ (BOOL)hostHasGetTaskAllow
{
    typedef void *(*CreateFromSelf)(CFAllocatorRef);
    typedef CFTypeRef (*CopyValue)(void *, CFStringRef, CFErrorRef *);
    void *security = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
    if (!security)
        return NO;
    CreateFromSelf create = (CreateFromSelf)dlsym(security, "SecTaskCreateFromSelf");
    CopyValue copyValue = (CopyValue)dlsym(security, "SecTaskCopyValueForEntitlement");
    if (!create || !copyValue)
        return NO;
    void *task = create(NULL);
    if (!task)
        return NO;
    CFTypeRef value = copyValue(task, CFSTR("get-task-allow"), NULL);
    BOOL result = value == kCFBooleanTrue;
    if (value)
        CFRelease(value);
    CFRelease((CFTypeRef)task);
    return result;
}

+ (BOOL)isBusy
{
    return sOpen;
}

+ (BOOL)sendRequest:(NSDictionary<NSString *, id> *)request reply:(StikJITHelperReply)reply failure:(NSString **)failure
{
    if (sOpen)
    {
        if (failure)
            *failure = @"StikJIT is still busy with the last request.";
        return NO;
    }
    NSString *identifier = [self helperIdentifier];
    if (!identifier)
    {
        if (failure)
            *failure = @"This build of EspressU doesn't contain the StikJIT helper.";
        return NO;
    }
    Class extensionClass = NSClassFromString(@"NSExtension");
    if (![extensionClass respondsToSelector:@selector(extensionWithIdentifier:error:)])
    {
        if (failure)
            *failure = @"This version of iOS doesn't let EspressU start its StikJIT helper.";
        return NO;
    }
    NSError *error = nil;
    id<SJHExtension> extension = [(Class<SJHExtension>)extensionClass extensionWithIdentifier:identifier error:&error];
    if (!extension)
    {
        if (failure)
            *failure = [NSString stringWithFormat:@"iOS couldn't find EspressU's StikJIT helper (%@). Reinstall EspressU with a signature that keeps its extension.",
                        error.localizedDescription ?: identifier];
        return NO;
    }

    sExtension = extension;
    sReply = [reply copy];
    const NSUInteger generation = ++sGeneration;
    __weak id<SJHExtension> weakExtension = extension;
    sOpen = YES;

    [extension setRequestCompletionBlock:^(NSUUID *requestIdentifier, NSArray *extensionItems) {
        NSDictionary *answer = nil;
        for (id item in extensionItems)
            if ([item isKindOfClass:NSExtensionItem.class] && [((NSExtensionItem *)item).userInfo isKindOfClass:NSDictionary.class])
                answer = ((NSExtensionItem *)item).userInfo;
        dispatch_async(dispatch_get_main_queue(), ^{
            SJHFinish(generation, answer, answer ? nil : @"The StikJIT helper finished without an answer.");
        });
    }];
    [extension setRequestCancellationBlock:^(NSUUID *requestIdentifier, NSError *cancelError) {
        NSString *text = cancelError.localizedDescription ?: @"The StikJIT helper stopped.";
        dispatch_async(dispatch_get_main_queue(), ^{ SJHFinish(generation, nil, text); });
    }];
    [extension setRequestInterruptionBlock:^(NSUUID *requestIdentifier) {
        dispatch_async(dispatch_get_main_queue(), ^{
            SJHFinish(generation, nil, @"The StikJIT helper quit unexpectedly. iOS may have closed it; try again.");
        });
    }];

    NSExtensionItem *item = [NSExtensionItem new];
    item.userInfo = request;
    [extension beginExtensionRequestWithInputItems:@[item] completion:^(NSUUID *requestIdentifier) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!requestIdentifier)
            {
                SJHFinish(generation, nil, @"iOS refused to start EspressU's StikJIT helper.");
                return;
            }
            if (sOpen && generation == sGeneration && weakExtension)
                sRequestID = requestIdentifier;
        });
    }];
    return YES;
}

+ (void)cancelWithReason:(NSString *)reason
{
    if (!sOpen)
        return;
    if (sExtension && sRequestID)
        [sExtension cancelExtensionRequestWithIdentifier:sRequestID];
    SJHFinish(sGeneration, nil, reason);
}

+ (id)observeStagesWithPrefix:(NSString *)prefix stages:(NSArray<NSString *> *)stages handler:(void (^)(NSString *))handler
{
    SJHStageObserver *observer = [SJHStageObserver new];
    observer.prefix = prefix;
    observer.names = stages;
    observer.handler = handler;
    CFNotificationCenterRef center = CFNotificationCenterGetDarwinNotifyCenter();
    for (NSString *stage in stages)
    {
        NSString *name = [NSString stringWithFormat:@"%@.%@", prefix, stage];
        CFNotificationCenterAddObserver(center, (__bridge const void *)observer, SJHStageCallback,
                                        (__bridge CFStringRef)name, NULL,
                                        CFNotificationSuspensionBehaviorDeliverImmediately);
    }
    return observer;
}

+ (void)stopObserving:(id)token
{
    if (![token isKindOfClass:SJHStageObserver.class])
        return;
    CFNotificationCenterRemoveEveryObserver(CFNotificationCenterGetDarwinNotifyCenter(), (__bridge const void *)token);
}

@end
