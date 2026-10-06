#define CEL_DEVICE static inline
CEL_DEVICE float cel_blur(int x) { return (float)x; }
CEL_DEVICE void cel_k_blur(int x) { (void)cel_blur(x); }
CEL_KERNEL(blur)
