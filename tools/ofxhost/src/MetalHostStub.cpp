// ofxhost - Metal host side for platforms without Metal (every call fails cleanly).
#include "MetalHost.h"

namespace ofxhost {
bool MetalHost::compiledIn() { return false; }
MetalHost::~MetalHost() {}
bool MetalHost::init(std::string& err) { err = "Metal is not compiled into this host"; return false; }
void* MetalHost::createBuffer(size_t, std::string& err) { err = "no Metal"; return nullptr; }
bool MetalHost::upload(void*, const void*, size_t, std::string& err) { err = "no Metal"; return false; }
bool MetalHost::download(void*, void*, size_t, std::string& err) { err = "no Metal"; return false; }
bool MetalHost::fillPattern(void*, size_t, std::string& err) { err = "no Metal"; return false; }
void MetalHost::release(void*) {}
bool MetalHost::finish(std::string& err) { err = "no Metal"; return false; }
} // namespace ofxhost

