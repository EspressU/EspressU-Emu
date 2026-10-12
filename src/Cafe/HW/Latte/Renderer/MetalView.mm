#include "Cafe/HW/Latte/Renderer/MetalView.h"
#include "Metal/Metal.h"

@implementation MetalView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupMetalLayer];
    }
    return self;
}

- (void)awakeFromNib {
    [super awakeFromNib];
    [self setupMetalLayer];
}

- (void)setupMetalLayer {
    CAMetalLayer *metalLayer = (CAMetalLayer *)self.layer;
    metalLayer.contentsScale = [UIScreen mainScreen].scale;
    metalLayer.device = MTLCreateSystemDefaultDevice();
    metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metalLayer.framebufferOnly = YES;
}

@end
