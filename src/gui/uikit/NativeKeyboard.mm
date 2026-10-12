#import <UIKit/UIKit.h>
#include "NativeKeyboard.h"
#include <algorithm>
#include <mutex>

namespace
{
std::mutex mailboxMutex;
uint64_t generation = 0;
std::u16string pendingText;
bool pending = false;
bool submitted = false;

bool IsCurrent(uint64_t token)
{
    std::lock_guard lock(mailboxMutex);
    return generation == token;
}
}

@interface CemuNativeKeyboard : NSObject <UITextFieldDelegate>
@property(nonatomic, strong) UIAlertController* alert;
@property(nonatomic) uint64_t token;
@property(nonatomic) NSUInteger maxLength;
- (void)changed:(UITextField*)field;
- (void)submit;
- (void)presentWhenReady;
@end

static CemuNativeKeyboard* keyboard;

@implementation CemuNativeKeyboard
- (void)changed:(UITextField*)field
{
    if (field.markedTextRange)
        return;
    NSString* value = field.text ?: @"";
    if (value.length > self.maxLength)
    {
        NSUInteger length = self.maxLength;
        if (length && CFStringIsSurrogateHighCharacter([value characterAtIndex:length - 1]))
            --length;
        value = [value substringToIndex:length];
        field.text = value;
    }
    std::u16string text(value.length, u'\0');
    if (!text.empty())
        [value getCharacters:reinterpret_cast<unichar*>(text.data()) range:NSMakeRange(0, value.length)];
    std::lock_guard lock(mailboxMutex);
    if (generation != self.token || submitted)
        return;
    pendingText = std::move(text);
    pending = true;
}

- (void)submit
{
    UITextField* field = self.alert.textFields.firstObject;
    [field unmarkText];
    [self changed:field];
    {
        std::lock_guard lock(mailboxMutex);
        if (generation != self.token)
            return;
        submitted = true;
        pending = true;
    }
    [self.alert dismissViewControllerAnimated:YES completion:nil];
}

- (BOOL)textFieldShouldReturn:(UITextField*)field
{
    [self submit];
    return NO;
}

- (void)presentWhenReady
{
    if (!IsCurrent(self.token))
        return;

    UIViewController* presenter = nil;
    for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
    {
        if (scene.activationState != UISceneActivationStateForegroundActive ||
            ![scene isKindOfClass:UIWindowScene.class] ||
            ![scene.session.role isEqualToString:UIWindowSceneSessionRoleApplication])
            continue;
        for (UIWindow* window in ((UIWindowScene*)scene).windows)
            if (window.isKeyWindow)
                presenter = window.rootViewController;
    }
    while (presenter.presentedViewController)
        presenter = presenter.presentedViewController;
    if (!presenter.view.window || presenter.isBeingDismissed || presenter.isBeingPresented ||
        [presenter isKindOfClass:UIAlertController.class])
    {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            [self presentWhenReady];
        });
        return;
    }
    [presenter presentViewController:self.alert animated:YES completion:nil];
}
@end

namespace WindowSystem
{
void ShowNativeKeyboard(std::u16string text, size_t maxLength, size_t cursor)
{
    uint64_t token;
    {
        std::lock_guard lock(mailboxMutex);
        token = ++generation;
        pendingText.clear();
        pending = submitted = false;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!IsCurrent(token))
            return;
        [keyboard.alert dismissViewControllerAnimated:NO completion:nil];
        keyboard = [CemuNativeKeyboard new];
        keyboard.token = token;
        keyboard.maxLength = maxLength;
        keyboard.alert = [UIAlertController alertControllerWithTitle:@"Keyboard Input"
            message:nil preferredStyle:UIAlertControllerStyleAlert];
        CemuNativeKeyboard* owner = keyboard;
        __weak CemuNativeKeyboard* weakOwner = owner;
        [owner.alert addTextFieldWithConfigurationHandler:^(UITextField* field) {
            field.text = [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar*>(text.data()) length:text.size()];
            field.delegate = weakOwner;
            field.autocorrectionType = UITextAutocorrectionTypeNo;
            field.autocapitalizationType = UITextAutocapitalizationTypeNone;
            field.returnKeyType = UIReturnKeyDone;
            [field addTarget:weakOwner action:@selector(changed:) forControlEvents:UIControlEventEditingChanged];
            UITextPosition* position = [field positionFromPosition:field.beginningOfDocument offset:std::min(cursor, text.size())];
            field.selectedTextRange = [field textRangeFromPosition:position toPosition:position];
        }];
        [owner.alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleDefault
            handler:^(UIAlertAction* action) { [weakOwner submit]; }]];
        [owner presentWhenReady];
    });
}

void HideNativeKeyboard()
{
    uint64_t token;
    {
        std::lock_guard lock(mailboxMutex);
        token = ++generation;
        pending = submitted = false;
        pendingText.clear();
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!IsCurrent(token))
            return;
        [keyboard.alert dismissViewControllerAnimated:NO completion:nil];
        keyboard = nil;
    });
}

bool PollNativeKeyboard(std::u16string& text, bool& accepted)
{
    std::lock_guard lock(mailboxMutex);
    if (!pending)
        return false;
    text = pendingText;
    accepted = submitted;
    pending = false;
    return true;
}
}
