// Celluloid Pro - stock library accessors (data: StockData.inc, generated).
#include "Stocks.h"
#include <algorithm>

namespace cel { namespace core { namespace stocks {

namespace {
using spec::Lobe;
#include "StockData.inc"
}

int numNegatives() { return kNumNegatives; }
int numPrints() { return kNumPrints; }
const NegDef& negative(int i) { return kNegatives[std::min(std::max(i, 0), kNumNegatives - 1)]; }
const PrintDef& print(int i) { return kPrints[std::min(std::max(i, 0), kNumPrints - 1)]; }
const ScannerDef& scanner() { return kScanner; }
const LampSet& lamps() { return kLamps; }
const char* libraryHash() { return CEL_STOCK_HASH; }

}}} // namespace cel::core::stocks
