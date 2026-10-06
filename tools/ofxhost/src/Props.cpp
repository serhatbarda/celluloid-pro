// ofxhost - property sets and OfxPropertySuiteV1.
#include "Props.h"

#include <cstdio>
#include <sstream>

namespace ofxhost {

const char* propTypeName(PropType t) {
    switch (t) {
    case PropType::Int: return "int";
    case PropType::Double: return "double";
    case PropType::String: return "string";
    case PropType::Pointer: return "pointer";
    }
    return "?";
}

int Property::dimension() const {
    switch (type) {
    case PropType::Int: return int(i.size());
    case PropType::Double: return int(d.size());
    case PropType::String: return int(s.size());
    case PropType::Pointer: return int(p.size());
    }
    return 0;
}

void Property::resize(int n) {
    switch (type) {
    case PropType::Int: i.resize(size_t(n), 0); break;
    case PropType::Double: d.resize(size_t(n), 0.0); break;
    case PropType::String: s.resize(size_t(n)); break;
    case PropType::Pointer: p.resize(size_t(n), nullptr); break;
    }
}

// ---------------------------------------------------------------- Diagnostics
static Diagnostics g_diag;
Diagnostics& diag() { return g_diag; }

void Diagnostics::warn(const std::string& w) {
    std::lock_guard<std::mutex> lk(mtx);
    if (warnings.size() < 2000) warnings.push_back(w);
    if (verbose) std::fprintf(stderr, "[ofxhost] warning: %s\n", w.c_str());
}
void Diagnostics::noteUnknownGet(const std::string& set, const std::string& prop) {
    std::lock_guard<std::mutex> lk(mtx);
    unknownGets[set + ":" + prop]++;
    if (traceProps) std::fprintf(stderr, "[ofxhost] unknown property get %s:%s\n", set.c_str(), prop.c_str());
}
void Diagnostics::noteDynamicSet(const std::string& set, const std::string& prop) {
    std::lock_guard<std::mutex> lk(mtx);
    dynamicSets[set + ":" + prop]++;
    if (traceProps) std::fprintf(stderr, "[ofxhost] plug-in created property %s:%s\n", set.c_str(), prop.c_str());
}

// ---------------------------------------------------------------- PropertySet
PropertySet::PropertySet(const PropertySet& o) {
    std::lock_guard<std::recursive_mutex> lk(o.mtx_);
    name_ = o.name_;
    instanceSemantics_ = o.instanceSemantics_;
    props_ = o.props_;
}

PropertySet& PropertySet::operator=(const PropertySet& o) {
    if (this == &o) return *this;
    std::map<std::string, Property> copy;
    {
        std::lock_guard<std::recursive_mutex> lk(o.mtx_);
        copy = o.props_;
    }
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    props_ = std::move(copy);
    name_ = o.name_;
    instanceSemantics_ = o.instanceSemantics_;
    return *this;
}

PropertySet* PropertySet::from(OfxPropertySetHandle h) {
    auto* ps = reinterpret_cast<PropertySet*>(h);
    if (!ps || ps->magic_ != kMagic) return nullptr;
    return ps;
}

void PropertySet::defInt(const std::string& n, std::vector<int> v, bool hostOwned) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    Property& p = props_[n];
    p = Property{};
    p.type = PropType::Int; p.hostOwned = hostOwned; p.i = v; p.i0 = std::move(v);
}
void PropertySet::defDouble(const std::string& n, std::vector<double> v, bool hostOwned) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    Property& p = props_[n];
    p = Property{};
    p.type = PropType::Double; p.hostOwned = hostOwned; p.d = v; p.d0 = std::move(v);
}
void PropertySet::defString(const std::string& n, std::vector<std::string> v, bool hostOwned) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    Property& p = props_[n];
    p = Property{};
    p.type = PropType::String; p.hostOwned = hostOwned; p.s = v; p.s0 = std::move(v);
}
void PropertySet::defPointer(const std::string& n, std::vector<void*> v, bool hostOwned) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    Property& p = props_[n];
    p = Property{};
    p.type = PropType::Pointer; p.hostOwned = hostOwned; p.p = v; p.p0 = std::move(v);
}

bool PropertySet::has(const std::string& n) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    return props_.count(n) != 0;
}
const Property* PropertySet::find(const std::string& n) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    return it == props_.end() ? nullptr : &it->second;
}
void PropertySet::erase(const std::string& n) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    props_.erase(n);
}
int PropertySet::getInt(const std::string& n, int idx, int fb) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) return fb;
    const Property& p = it->second;
    if (p.type == PropType::Int && idx < int(p.i.size())) return p.i[size_t(idx)];
    if (p.type == PropType::Double && idx < int(p.d.size())) return int(p.d[size_t(idx)]);
    return fb;
}
double PropertySet::getDouble(const std::string& n, int idx, double fb) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) return fb;
    const Property& p = it->second;
    if (p.type == PropType::Double && idx < int(p.d.size())) return p.d[size_t(idx)];
    if (p.type == PropType::Int && idx < int(p.i.size())) return p.i[size_t(idx)];
    return fb;
}
std::string PropertySet::getString(const std::string& n, int idx, const std::string& fb) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) return fb;
    const Property& p = it->second;
    if (p.type == PropType::String && idx < int(p.s.size())) return p.s[size_t(idx)];
    return fb;
}
void* PropertySet::getPointer(const std::string& n, int idx, void* fb) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) return fb;
    const Property& p = it->second;
    if (p.type == PropType::Pointer && idx < int(p.p.size())) return p.p[size_t(idx)];
    return fb;
}
int PropertySet::dimension(const std::string& n) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    return it == props_.end() ? 0 : it->second.dimension();
}
std::vector<std::string> PropertySet::getStrings(const std::string& n) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end() || it->second.type != PropType::String) return {};
    return it->second.s;
}
std::vector<int> PropertySet::getInts(const std::string& n) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end() || it->second.type != PropType::Int) return {};
    return it->second.i;
}
std::vector<double> PropertySet::getDoubles(const std::string& n) const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end() || it->second.type != PropType::Double) return {};
    return it->second.d;
}

void PropertySet::setInt(const std::string& n, int idx, int v) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) { defInt(n, std::vector<int>(size_t(idx) + 1, 0), true); it = props_.find(n); }
    Property& p = it->second;
    if (p.type == PropType::Double) { if (idx >= int(p.d.size())) p.resize(idx + 1); p.d[size_t(idx)] = v; return; }
    if (p.type != PropType::Int) return;
    if (idx >= int(p.i.size())) p.resize(idx + 1);
    p.i[size_t(idx)] = v;
}
void PropertySet::setDouble(const std::string& n, int idx, double v) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) { defDouble(n, std::vector<double>(size_t(idx) + 1, 0.0), true); it = props_.find(n); }
    Property& p = it->second;
    if (p.type != PropType::Double) return;
    if (idx >= int(p.d.size())) p.resize(idx + 1);
    p.d[size_t(idx)] = v;
}
void PropertySet::setString(const std::string& n, int idx, const std::string& v) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) { defString(n, std::vector<std::string>(size_t(idx) + 1), true); it = props_.find(n); }
    Property& p = it->second;
    if (p.type != PropType::String) return;
    if (idx >= int(p.s.size())) p.resize(idx + 1);
    p.s[size_t(idx)] = v;
}
void PropertySet::setPointer(const std::string& n, int idx, void* v) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(n);
    if (it == props_.end()) { defPointer(n, std::vector<void*>(size_t(idx) + 1, nullptr), true); it = props_.find(n); }
    Property& p = it->second;
    if (p.type != PropType::Pointer) return;
    if (idx >= int(p.p.size())) p.resize(idx + 1);
    p.p[size_t(idx)] = v;
}

void PropertySet::merge(const PropertySet& o, bool overwrite) {
    if (&o == this) return;
    std::map<std::string, Property> copy;
    {
        std::lock_guard<std::recursive_mutex> lk(o.mtx_);
        copy = o.props_;
    }
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    for (auto& kv : copy) {
        if (overwrite || !props_.count(kv.first)) props_[kv.first] = kv.second;
    }
}

std::vector<std::string> PropertySet::names() const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    std::vector<std::string> r;
    for (auto& kv : props_) r.push_back(kv.first);
    return r;
}

std::string PropertySet::dump() const {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    std::ostringstream os;
    for (auto& kv : props_) {
        const Property& p = kv.second;
        os << "  " << kv.first << " (" << propTypeName(p.type) << (p.dynamic ? ", dynamic" : "") << ") =";
        for (int k = 0; k < p.dimension(); ++k) {
            switch (p.type) {
            case PropType::Int: os << ' ' << p.i[size_t(k)]; break;
            case PropType::Double: os << ' ' << p.d[size_t(k)]; break;
            case PropType::String: os << " \"" << p.s[size_t(k)] << '"'; break;
            case PropType::Pointer: os << ' ' << p.p[size_t(k)]; break;
            }
        }
        os << '\n';
    }
    return os.str();
}

void PropertySet::assignOne(Property& p, PropType t, int index, const void* value) {
    if (index >= p.dimension()) p.resize(index + 1);
    switch (t) {
    case PropType::Int: p.i[size_t(index)] = *static_cast<const int*>(value); break;
    case PropType::Double: p.d[size_t(index)] = *static_cast<const double*>(value); break;
    case PropType::String: {
        const char* s = *static_cast<const char* const*>(value);
        p.s[size_t(index)] = s ? s : "";
        break;
    }
    case PropType::Pointer: p.p[size_t(index)] = *static_cast<void* const*>(value); break;
    }
}

void PropertySet::readOne(const Property& p, PropType t, int index, void* value) const {
    switch (t) {
    case PropType::Int: *static_cast<int*>(value) = p.i[size_t(index)]; break;
    case PropType::Double: *static_cast<double*>(value) = p.d[size_t(index)]; break;
    case PropType::String: *static_cast<const char**>(value) = p.s[size_t(index)].c_str(); break;
    case PropType::Pointer: *static_cast<void**>(value) = p.p[size_t(index)]; break;
    }
}

static const int kMaxDim = 1 << 16;

OfxStatus PropertySet::sSet(const char* prop, PropType t, int index, const void* value) {
    if (!prop) return kOfxStatErrBadHandle;
    if (index < 0 || index > kMaxDim) return kOfxStatErrBadIndex;
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(prop);
    if (it == props_.end()) {
        Property np;
        np.type = t;
        np.dynamic = true;
        it = props_.emplace(prop, np).first;
        diag().noteDynamicSet(name_, prop);
    }
    Property& p = it->second;
    if (p.type != t) {
        diag().warn("type mismatch: plug-in set " + std::string(propTypeName(t)) + " on " + name_ + ":" + prop +
                    " which is " + propTypeName(p.type));
        return kOfxStatErrValue;
    }
    if (instanceSemantics_ && p.hostOwned) {
        diag().warn("plug-in wrote host-owned property " + name_ + ":" + prop);
    }
    assignOne(p, t, index, value);
    return kOfxStatOK;
}

OfxStatus PropertySet::sSetN(const char* prop, PropType t, int count, const void* values) {
    if (!prop) return kOfxStatErrBadHandle;
    if (count < 0 || count > kMaxDim) return kOfxStatErrBadIndex;
    if (count > 0 && !values) return kOfxStatErrValue;
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(prop);
    if (it == props_.end()) {
        Property np;
        np.type = t;
        np.dynamic = true;
        it = props_.emplace(prop, np).first;
        diag().noteDynamicSet(name_, prop);
    }
    Property& p = it->second;
    if (p.type != t) {
        diag().warn("type mismatch: plug-in setN " + std::string(propTypeName(t)) + " on " + name_ + ":" + prop +
                    " which is " + propTypeName(p.type));
        return kOfxStatErrValue;
    }
    if (instanceSemantics_ && p.hostOwned) {
        diag().warn("plug-in wrote host-owned property " + name_ + ":" + prop);
    }
    p.resize(count);
    for (int k = 0; k < count; ++k) {
        switch (t) {
        case PropType::Int: p.i[size_t(k)] = static_cast<const int*>(values)[k]; break;
        case PropType::Double: p.d[size_t(k)] = static_cast<const double*>(values)[k]; break;
        case PropType::String: {
            const char* s = static_cast<const char* const*>(values)[k];
            p.s[size_t(k)] = s ? s : "";
            break;
        }
        case PropType::Pointer: p.p[size_t(k)] = static_cast<void* const*>(values)[k]; break;
        }
    }
    return kOfxStatOK;
}

OfxStatus PropertySet::sGet(const char* prop, PropType t, int index, void* value) {
    if (!prop || !value) return kOfxStatErrBadHandle;
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(prop);
    if (it == props_.end()) {
        diag().noteUnknownGet(name_, prop);
        return kOfxStatErrUnknown;
    }
    const Property& p = it->second;
    if (p.type != t) {
        diag().warn("type mismatch: plug-in get " + std::string(propTypeName(t)) + " of " + name_ + ":" + prop +
                    " which is " + propTypeName(p.type));
        return kOfxStatErrValue;
    }
    if (index < 0 || index >= p.dimension()) return kOfxStatErrBadIndex;
    readOne(p, t, index, value);
    return kOfxStatOK;
}

OfxStatus PropertySet::sGetN(const char* prop, PropType t, int count, void* values) {
    if (!prop || (count > 0 && !values)) return kOfxStatErrBadHandle;
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(prop);
    if (it == props_.end()) {
        diag().noteUnknownGet(name_, prop);
        return kOfxStatErrUnknown;
    }
    const Property& p = it->second;
    if (p.type != t) {
        diag().warn("type mismatch: plug-in getN " + std::string(propTypeName(t)) + " of " + name_ + ":" + prop +
                    " which is " + propTypeName(p.type));
        return kOfxStatErrValue;
    }
    if (count < 0 || count > p.dimension()) return kOfxStatErrBadIndex;
    for (int k = 0; k < count; ++k) {
        switch (t) {
        case PropType::Int: static_cast<int*>(values)[k] = p.i[size_t(k)]; break;
        case PropType::Double: static_cast<double*>(values)[k] = p.d[size_t(k)]; break;
        case PropType::String: static_cast<const char**>(values)[k] = p.s[size_t(k)].c_str(); break;
        case PropType::Pointer: static_cast<void**>(values)[k] = p.p[size_t(k)]; break;
        }
    }
    return kOfxStatOK;
}

OfxStatus PropertySet::sReset(const char* prop) {
    if (!prop) return kOfxStatErrBadHandle;
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(prop);
    if (it == props_.end()) {
        diag().noteUnknownGet(name_, prop);
        return kOfxStatErrUnknown;
    }
    Property& p = it->second;
    p.i = p.i0; p.d = p.d0; p.s = p.s0; p.p = p.p0;
    return kOfxStatOK;
}

OfxStatus PropertySet::sDim(const char* prop, int* count) {
    if (!prop || !count) return kOfxStatErrBadHandle;
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    auto it = props_.find(prop);
    if (it == props_.end()) {
        diag().noteUnknownGet(name_, prop);
        return kOfxStatErrUnknown;
    }
    *count = it->second.dimension();
    return kOfxStatOK;
}

// ---------------------------------------------------------------- suite
namespace {

inline PropertySet* PS(OfxPropertySetHandle h) {
    PropertySet* ps = PropertySet::from(h);
    if (!ps) diag().badHandles++;
    return ps;
}

OfxStatus propSetPointer(OfxPropertySetHandle h, const char* n, int i, void* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSet(n, PropType::Pointer, i, &v);
}
OfxStatus propSetString(OfxPropertySetHandle h, const char* n, int i, const char* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSet(n, PropType::String, i, &v);
}
OfxStatus propSetDouble(OfxPropertySetHandle h, const char* n, int i, double v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSet(n, PropType::Double, i, &v);
}
OfxStatus propSetInt(OfxPropertySetHandle h, const char* n, int i, int v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSet(n, PropType::Int, i, &v);
}
OfxStatus propSetPointerN(OfxPropertySetHandle h, const char* n, int c, void* const* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSetN(n, PropType::Pointer, c, v);
}
OfxStatus propSetStringN(OfxPropertySetHandle h, const char* n, int c, const char* const* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSetN(n, PropType::String, c, v);
}
OfxStatus propSetDoubleN(OfxPropertySetHandle h, const char* n, int c, const double* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSetN(n, PropType::Double, c, v);
}
OfxStatus propSetIntN(OfxPropertySetHandle h, const char* n, int c, const int* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sSetN(n, PropType::Int, c, v);
}
OfxStatus propGetPointer(OfxPropertySetHandle h, const char* n, int i, void** v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGet(n, PropType::Pointer, i, v);
}
OfxStatus propGetString(OfxPropertySetHandle h, const char* n, int i, char** v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGet(n, PropType::String, i, v);
}
OfxStatus propGetDouble(OfxPropertySetHandle h, const char* n, int i, double* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGet(n, PropType::Double, i, v);
}
OfxStatus propGetInt(OfxPropertySetHandle h, const char* n, int i, int* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGet(n, PropType::Int, i, v);
}
OfxStatus propGetPointerN(OfxPropertySetHandle h, const char* n, int c, void** v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGetN(n, PropType::Pointer, c, v);
}
OfxStatus propGetStringN(OfxPropertySetHandle h, const char* n, int c, char** v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGetN(n, PropType::String, c, v);
}
OfxStatus propGetDoubleN(OfxPropertySetHandle h, const char* n, int c, double* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGetN(n, PropType::Double, c, v);
}
OfxStatus propGetIntN(OfxPropertySetHandle h, const char* n, int c, int* v) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sGetN(n, PropType::Int, c, v);
}
OfxStatus propReset(OfxPropertySetHandle h, const char* n) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sReset(n);
}
OfxStatus propGetDimension(OfxPropertySetHandle h, const char* n, int* c) {
    PropertySet* ps = PS(h); if (!ps) return kOfxStatErrBadHandle;
    return ps->sDim(n, c);
}

OfxPropertySuiteV1 g_propSuite = {
    propSetPointer, propSetString, propSetDouble, propSetInt,
    propSetPointerN, propSetStringN, propSetDoubleN, propSetIntN,
    propGetPointer, propGetString, propGetDouble, propGetInt,
    propGetPointerN, propGetStringN, propGetDoubleN, propGetIntN,
    propReset, propGetDimension,
};

} // namespace

OfxPropertySuiteV1* propertySuite() { return &g_propSuite; }

} // namespace ofxhost
