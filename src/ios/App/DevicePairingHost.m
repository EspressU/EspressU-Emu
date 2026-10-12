// SPDX-License-Identifier: MPL-2.0

#import "DevicePairingHost.h"
#import "idevice.h"

#import <netinet/in.h>
#import <stdatomic.h>
#import <sys/socket.h>
#import <unistd.h>

static NSString *DescribeError(IdeviceFfiError *error)
{
    if (!error) return nil;
    NSString *text = error->message ? [NSString stringWithUTF8String:error->message] : nil;
    if (text.length == 0) text = [NSString stringWithFormat:@"idevice error %d", error->code];
    idevice_error_free(error);
    return text;
}

static void PINCallback(const char *pin, void *context)
{
    if (!pin || !context) return;
    NSString *text = [NSString stringWithUTF8String:pin];
    void (^handler)(NSString *) = (__bridge void (^)(NSString *))context;
    handler(text);
}

@implementation DevicePairingHost {
    PairableHostHandle *_handle;
    _Atomic int _listener;
    atomic_bool _cancelled;
    atomic_bool _running;
    atomic_bool _pinShown;
}

+ (NSString *)serviceType
{
    return @"_remotepairing-pairable-host._tcp";
}

- (nullable instancetype)initWithName:(NSString *)name failure:(NSString **)failure
{
    if (!(self = [super init])) return nil;
    atomic_init(&_listener, -1);

    char *serviceID = NULL;
    uint8_t *txt = NULL;
    uintptr_t txtLength = 0;
    NSString *problem = DescribeError(pairable_host_prepare(name.UTF8String, NULL, false, &_handle,
                                                            &serviceID, &txt, &txtLength, NULL));
    if (problem) {
        if (failure) *failure = [@"Couldn't create the pairing identity: " stringByAppendingString:problem];
        return nil;
    }
    _serviceName = serviceID ? [NSString stringWithUTF8String:serviceID] : name;
    if (serviceID) idevice_string_free(serviceID);

    NSMutableDictionary<NSString *, NSData *> *record = [NSMutableDictionary dictionary];
    if (txt && txtLength > 0) {
        NSData *plist = [NSData dataWithBytes:txt length:txtLength];
        id parsed = [NSPropertyListSerialization propertyListWithData:plist options:0 format:NULL error:NULL];
        if ([parsed isKindOfClass:NSDictionary.class]) {
            [(NSDictionary *)parsed enumerateKeysAndObjectsUsingBlock:^(id key, id value, BOOL *stop) {
                if (![key isKindOfClass:NSString.class]) return;
                if ([value isKindOfClass:NSData.class]) record[key] = value;
                else if ([value isKindOfClass:NSString.class]) record[key] = [value dataUsingEncoding:NSUTF8StringEncoding];
                else if ([value respondsToSelector:@selector(stringValue)])
                    record[key] = [[value stringValue] dataUsingEncoding:NSUTF8StringEncoding];
            }];
        }
    }
    if (txt) idevice_data_free(txt, txtLength);
    _txtRecord = record;

    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        if (failure) *failure = [NSString stringWithFormat:@"Couldn't open a network socket (%s).", strerror(errno)];
        return nil;
    }
    int no = 0, yes = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in6 address = {0};
    address.sin6_len = sizeof(address);
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    socklen_t length = sizeof(address);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, 4) != 0 ||
        getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        if (failure) *failure = [NSString stringWithFormat:@"Couldn't listen for the device (%s).", strerror(errno)];
        close(fd);
        return nil;
    }
    _port = ntohs(address.sin6_port);
    atomic_store(&_listener, fd);
    return self;
}

- (void)runWithPIN:(void (^)(NSString *))pin
        completion:(void (^)(NSData *, NSString *, NSString *, BOOL))completion
{
    if (atomic_exchange(&_running, true)) return;
    __weak DevicePairingHost *weakSelf = self;
    void (^pinHandler)(NSString *) = [^(NSString *code) {
        DevicePairingHost *host = weakSelf;
        if (host) atomic_store(&host->_pinShown, true);
        dispatch_async(dispatch_get_main_queue(), ^{ pin(code); });
    } copy];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSData *file = nil;
        NSString *deviceName = nil;
        NSString *failure = nil;
        int listener = atomic_load(&self->_listener);

        for (int attempt = 0; attempt < 8 && !file; attempt++) {
            int client = listener >= 0 ? accept(listener, NULL, NULL) : -1;
            if (client < 0 || atomic_load(&self->_cancelled)) {
                if (client >= 0) close(client);
                failure = failure ?: @"Pairing was stopped.";
                break;
            }
            int on = 1;
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
            RpPairingPeerDeviceC *peer = NULL;
            RpPairingFileHandle *pairing = NULL;
            failure = DescribeError(pairable_host_accept_fd(self->_handle, client, PINCallback,
                                                            (__bridge void *)pinHandler, &peer, &pairing));
            close(client);
            if (!failure && pairing) {
                uint8_t *bytes = NULL;
                uintptr_t count = 0;
                failure = DescribeError(rp_pairing_file_to_bytes(pairing, &bytes, &count));
                if (!failure && bytes && count > 0) file = [NSData dataWithBytes:bytes length:count];
                if (bytes) idevice_data_free(bytes, count);
                if (!failure && !file) failure = @"The device paired but no pairing file came back.";
            }
            if (peer && peer->name) deviceName = [NSString stringWithUTF8String:peer->name];
            if (peer) rppairing_peer_device_free(peer);
            if (pairing) rp_pairing_file_free(pairing);
            if (atomic_load(&self->_pinShown) || atomic_load(&self->_cancelled)) break;
        }
        int closing = atomic_exchange(&self->_listener, -1);
        if (closing >= 0) close(closing);
        BOOL cancelled = atomic_load(&self->_cancelled);
        (void)pinHandler;
        dispatch_async(dispatch_get_main_queue(), ^{
            completion(file, deviceName, file ? nil : failure, file ? NO : cancelled);
        });
    });
}

- (void)cancel
{
    if (atomic_exchange(&_cancelled, true)) return;
    if (!atomic_load(&_running)) return;
    uint16_t port = _port;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        int fd = socket(AF_INET6, SOCK_STREAM, 0);
        if (fd < 0) return;
        struct sockaddr_in6 address = {0};
        address.sin6_len = sizeof(address);
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(port);
        address.sin6_addr = in6addr_loopback;
        connect(fd, (struct sockaddr *)&address, sizeof(address));
        close(fd);
    });
}

- (void)dealloc
{
    int fd = atomic_exchange(&_listener, -1);
    if (fd >= 0) close(fd);
    if (_handle) pairable_host_free(_handle);
}

@end
