/* root of the flattener fixture.  A mid-line mention: #include "does_not_exist.h" is ignored. */
#include "a.h"
#include "a.h"
#define ITEM(n) int item_##n;
#include "list.inc"
#undef ITEM
#define ITEM(n) int again_##n;
  #  include "list.inc"
#undef ITEM
#include "sub/b.h"
#include "once.h"
#include "once.h"
int root_end;
