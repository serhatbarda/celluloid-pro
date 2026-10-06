// ofxhost - headless OpenFX test host.
// Property sets and the OfxPropertySuiteV1 implementation.
//
// Semantics (chosen to match what production hosts do and what the OFX C++
// Support library expects):
//   * get of an unknown property      -> kOfxStatErrUnknown (never a crash)
//   * get with index >= dimension     -> kOfxStatErrBadIndex
//   * get/set with the wrong type     -> kOfxStatErrValue
//   * set of an unknown property      -> the property is created (lenient, like
//     most hosts) and recorded as a "dynamic" property so --strict can flag it
//   * set with index >= dimension     -> the property grows (needed for
//     appending choice options etc.)
//   * read-only (host-owned) props    -> set is still accepted on descriptors;
//     on instances a plugin write to a host-owned property is recorded as a
//     protocol warning (value is updated, as most hosts do not police this).
#pragma once

#include "ofxCore.h"
#include "ofxProperty.h"

#include <mutex>
#include <string>
#include <vector>
#include <map>
#include <atomic>

namespace ofxhost {

enum class PropType { Int, Double, String, Pointer };
const char* propTypeName(PropType t);

struct Property {
    PropType type = PropType::Int;
    bool hostOwned = false;   // written by the host, read-only for the plug-in
    bool dynamic = false;     // created by a plug-in set (not predefined)
    std::vector<int> i;
    std::vector<double> d;
    std::vector<std::string> s;
    std::vector<void*> p;
    int dimension() const;
    void resize(int n);
    // default values restored by propReset
    std::vector<int> i0;
    std::vector<double> d0;
    std::vector<std::string> s0;
    std::vector<void*> p0;
};

class PropertySet {
public:
    static constexpr unsigned kMagic = 0x0F1A7E57u;
    explicit PropertySet(std::string setName = "") : name_(std::move(setName)) {}
    PropertySet(const PropertySet& o);
    PropertySet& operator=(const PropertySet& o);
    ~PropertySet() { magic_ = 0; }

    OfxPropertySetHandle handle() { return reinterpret_cast<OfxPropertySetHandle>(this); }
    static PropertySet* from(OfxPropertySetHandle h);

    const std::string& setName() const { return name_; }
    void setSetName(const std::string& n) { name_ = n; }
    // When true, plug-in writes to host-owned props are recorded as warnings.
    void setInstanceSemantics(bool v) { instanceSemantics_ = v; }

    // ---- host-side definition helpers (create or overwrite, mark ownership) ----
    void defInt(const std::string& n, std::vector<int> v, bool hostOwned = false);
    void defDouble(const std::string& n, std::vector<double> v, bool hostOwned = false);
    void defString(const std::string& n, std::vector<std::string> v, bool hostOwned = false);
    void defPointer(const std::string& n, std::vector<void*> v, bool hostOwned = false);
    void defInt(const std::string& n, int v, bool hostOwned = false) { defInt(n, std::vector<int>{v}, hostOwned); }
    void defDouble(const std::string& n, double v, bool hostOwned = false) { defDouble(n, std::vector<double>{v}, hostOwned); }
    void defString(const std::string& n, const std::string& v, bool hostOwned = false) { defString(n, std::vector<std::string>{v}, hostOwned); }
    void defPointer(const std::string& n, void* v, bool hostOwned = false) { defPointer(n, std::vector<void*>{v}, hostOwned); }

    // ---- host-side accessors (return fallback when missing) ----
    bool has(const std::string& n) const;
    int getInt(const std::string& n, int idx = 0, int fallback = 0) const;
    double getDouble(const std::string& n, int idx = 0, double fallback = 0) const;
    std::string getString(const std::string& n, int idx = 0, const std::string& fallback = "") const;
    void* getPointer(const std::string& n, int idx = 0, void* fallback = nullptr) const;
    int dimension(const std::string& n) const;
    std::vector<std::string> getStrings(const std::string& n) const;
    std::vector<int> getInts(const std::string& n) const;
    std::vector<double> getDoubles(const std::string& n) const;
    const Property* find(const std::string& n) const;
    void erase(const std::string& n);

    // Host-side set that keeps existing ownership (creates host-owned if missing).
    void setInt(const std::string& n, int idx, int v);
    void setDouble(const std::string& n, int idx, double v);
    void setString(const std::string& n, int idx, const std::string& v);
    void setPointer(const std::string& n, int idx, void* v);

    // Copy all properties of o whose name is not already present here (or all with overwrite).
    void merge(const PropertySet& o, bool overwrite);

    std::vector<std::string> names() const;
    std::string dump() const;  // human readable

    // ---- suite entry points (plug-in side) ----
    OfxStatus sSet(const char* prop, PropType t, int index, const void* value);
    OfxStatus sSetN(const char* prop, PropType t, int count, const void* values);
    OfxStatus sGet(const char* prop, PropType t, int index, void* value);
    OfxStatus sGetN(const char* prop, PropType t, int count, void* values);
    OfxStatus sReset(const char* prop);
    OfxStatus sDim(const char* prop, int* count);

    unsigned magic() const { return magic_; }

private:
    void assignOne(Property& p, PropType t, int index, const void* value);
    void readOne(const Property& p, PropType t, int index, void* value) const;

    unsigned magic_ = kMagic;
    std::string name_;
    bool instanceSemantics_ = false;
    mutable std::recursive_mutex mtx_;
    std::map<std::string, Property> props_;
};

// Global protocol-diagnostics sink shared by all suites.
struct Diagnostics {
    std::mutex mtx;
    std::vector<std::string> warnings;     // protocol violations / suspicious calls
    std::map<std::string, int> unknownGets; // "set:prop" -> count (informational)
    std::map<std::string, int> dynamicSets; // "set:prop" -> count
    std::atomic<int> badHandles{0};
    bool verbose = false;
    bool traceProps = false;
    void warn(const std::string& w);
    void noteUnknownGet(const std::string& set, const std::string& prop);
    void noteDynamicSet(const std::string& set, const std::string& prop);
};
Diagnostics& diag();

OfxPropertySuiteV1* propertySuite();

} // namespace ofxhost
