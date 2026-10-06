// Celluloid Pro - Linux workbench MOCK of the tiny Foundation subset used by src/gpu/metal and
// tests/gpu/test_env_metal.mm, for `clang -fsyntax-only -fobjc-arc` only (check_mm_mock.sh).
// Not Apple's header; signatures follow the public Apple documentation.
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#define nil ((id)0)
typedef unsigned long NSUInteger;
typedef long NSInteger;
typedef struct _NSRange { NSUInteger location; NSUInteger length; } NSRange;
static inline NSRange NSMakeRange(NSUInteger loc, NSUInteger len) { NSRange r; r.location = loc; r.length = len; return r; }
#define NS_ASSUME_NONNULL_BEGIN
#define NS_ASSUME_NONNULL_END
#define __MAC_OS_X_VERSION_MAX_ALLOWED 150000
#define __MAC_15_0 150000
typedef NSUInteger NSStringEncoding;
enum { NSUTF8StringEncoding = 4 };
@protocol NSObject
@end
__attribute__((objc_root_class))
@interface NSObject <NSObject>
+ (instancetype)alloc;
- (instancetype)init;
+ (instancetype)new;
@end
@interface NSString : NSObject
- (instancetype)initWithBytes:(const void*)bytes length:(NSUInteger)len encoding:(NSStringEncoding)enc;
+ (instancetype)stringWithUTF8String:(const char*)s;
@property(readonly) const char* UTF8String;
@end
@interface NSError : NSObject
@property(readonly, copy) NSString* localizedDescription;
@end
@interface NSArray<ObjectType> : NSObject
@property(readonly) NSUInteger count;
- (ObjectType)objectAtIndexedSubscript:(NSUInteger)idx;
- (NSUInteger)countByEnumeratingWithState:(void*)state objects:(id __unsafe_unretained*)buffer count:(NSUInteger)len;
@end
