// Reafilm Pro - Linux workbench MOCK of the Metal API subset used by src/gpu/metal and
// tests/gpu/test_env_metal.mm, for `clang -fsyntax-only -fobjc-arc` only (check_mm_mock.sh).
// Not Apple's header; signatures follow the public Apple Metal documentation.
#pragma once
#import <Foundation/Foundation.h>
typedef struct { NSUInteger width, height, depth; } MTLSize;
static inline MTLSize MTLSizeMake(NSUInteger w, NSUInteger h, NSUInteger d) { MTLSize s = {w, h, d}; return s; }
typedef NSUInteger MTLResourceOptions;
enum { MTLResourceStorageModeShared = 0, MTLResourceStorageModeManaged = 16, MTLResourceStorageModePrivate = 32 };
typedef NSUInteger MTLLanguageVersion;
enum { MTLLanguageVersion2_3 = 131075 };
typedef NSInteger MTLMathMode;
enum { MTLMathModeSafe = 0, MTLMathModeRelaxed = 1, MTLMathModeFast = 2 };
typedef NSInteger MTLMathFloatingPointFunctions;
enum { MTLMathFloatingPointFunctionsFast = 0, MTLMathFloatingPointFunctionsPrecise = 1 };
typedef NSUInteger MTLCommandBufferStatus;
enum { MTLCommandBufferStatusNotEnqueued = 0, MTLCommandBufferStatusCompleted = 4, MTLCommandBufferStatusError = 5 };

@protocol MTLDevice, MTLCommandBuffer;
@protocol MTLResource <NSObject>
@property(readonly) id<MTLDevice> device;
@end
@protocol MTLBuffer <MTLResource>
@property(readonly) NSUInteger length;
- (void*)contents;
@end
@protocol MTLFunction <NSObject>
@end
@protocol MTLLibrary <NSObject>
- (id<MTLFunction>)newFunctionWithName:(NSString*)name;
@end
@protocol MTLComputePipelineState <NSObject>
@property(readonly) NSUInteger maxTotalThreadsPerThreadgroup;
@property(readonly) NSUInteger threadExecutionWidth;
@end
@protocol MTLCommandEncoder <NSObject>
@property(copy) NSString* label;
- (void)endEncoding;
@end
@protocol MTLComputeCommandEncoder <MTLCommandEncoder>
- (void)setComputePipelineState:(id<MTLComputePipelineState>)state;
- (void)setBytes:(const void*)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setBuffer:(id<MTLBuffer>)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)dispatchThreadgroups:(MTLSize)threadgroupsPerGrid threadsPerThreadgroup:(MTLSize)threadsPerThreadgroup;
@end
@protocol MTLBlitCommandEncoder <MTLCommandEncoder>
- (void)copyFromBuffer:(id<MTLBuffer>)sourceBuffer sourceOffset:(NSUInteger)sourceOffset toBuffer:(id<MTLBuffer>)destinationBuffer destinationOffset:(NSUInteger)destinationOffset size:(NSUInteger)size;
- (void)fillBuffer:(id<MTLBuffer>)buffer range:(NSRange)range value:(uint8_t)value;
@end
typedef void (^MTLCommandBufferHandler)(id<MTLCommandBuffer>);
@protocol MTLCommandBuffer <NSObject>
@property(copy) NSString* label;
@property(readonly) MTLCommandBufferStatus status;
@property(readonly) NSError* error;
- (id<MTLComputeCommandEncoder>)computeCommandEncoder;
- (id<MTLBlitCommandEncoder>)blitCommandEncoder;
- (void)addCompletedHandler:(MTLCommandBufferHandler)block;
- (void)commit;
- (void)waitUntilCompleted;
@end
@protocol MTLCommandQueue <NSObject>
@property(readonly) id<MTLDevice> device;
- (id<MTLCommandBuffer>)commandBuffer;
@end
@interface MTLCompileOptions : NSObject
@property BOOL fastMathEnabled __attribute__((deprecated));
@property MTLMathMode mathMode;
@property MTLMathFloatingPointFunctions mathFloatingPointFunctions;
@property MTLLanguageVersion languageVersion;
@end
@protocol MTLDevice <NSObject>
@property(readonly) NSString* name;
- (id<MTLCommandQueue>)newCommandQueue;
- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLBuffer>)newBufferWithBytes:(const void*)pointer length:(NSUInteger)length options:(MTLResourceOptions)options;
- (id<MTLLibrary>)newLibraryWithSource:(NSString*)source options:(MTLCompileOptions*)options error:(NSError**)error;
- (id<MTLComputePipelineState>)newComputePipelineStateWithFunction:(id<MTLFunction>)fn error:(NSError**)error;
@end
id<MTLDevice> MTLCreateSystemDefaultDevice(void);
NSArray<id<MTLDevice>>* MTLCopyAllDevices(void) __attribute__((ns_returns_retained));
