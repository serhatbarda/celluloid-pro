// ofxhost - parameters and OfxParameterSuiteV1.
#include "Params.h"

#include "ofxParametricParam.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <sstream>

namespace ofxhost {

// ---------------------------------------------------------------- kinds
struct KindName { ParamKind k; const char* ofx; const char* shortName; };
static const KindName kKinds[] = {
    {ParamKind::Integer, kOfxParamTypeInteger, "Integer"},
    {ParamKind::Double, kOfxParamTypeDouble, "Double"},
    {ParamKind::Boolean, kOfxParamTypeBoolean, "Boolean"},
    {ParamKind::Choice, kOfxParamTypeChoice, "Choice"},
    {ParamKind::StrChoice, kOfxParamTypeStrChoice, "StrChoice"},
    {ParamKind::RGBA, kOfxParamTypeRGBA, "RGBA"},
    {ParamKind::RGB, kOfxParamTypeRGB, "RGB"},
    {ParamKind::Double2D, kOfxParamTypeDouble2D, "Double2D"},
    {ParamKind::Integer2D, kOfxParamTypeInteger2D, "Integer2D"},
    {ParamKind::Double3D, kOfxParamTypeDouble3D, "Double3D"},
    {ParamKind::Integer3D, kOfxParamTypeInteger3D, "Integer3D"},
    {ParamKind::String, kOfxParamTypeString, "String"},
    {ParamKind::Custom, kOfxParamTypeCustom, "Custom"},
    {ParamKind::Bytes, kOfxParamTypeBytes, "Bytes"},
    {ParamKind::Group, kOfxParamTypeGroup, "Group"},
    {ParamKind::Page, kOfxParamTypePage, "Page"},
    {ParamKind::PushButton, kOfxParamTypePushButton, "PushButton"},
    {ParamKind::Parametric, kOfxParamTypeParametric, "Parametric"},
};

ParamKind paramKindFromOfx(const std::string& t) {
    for (auto& k : kKinds) if (t == k.ofx) return k.k;
    return ParamKind::Unknown;
}
const char* paramKindToOfx(ParamKind k) {
    for (auto& e : kKinds) if (e.k == k) return e.ofx;
    return "?";
}
const char* paramKindShort(ParamKind k) {
    for (auto& e : kKinds) if (e.k == k) return e.shortName;
    return "Unknown";
}

// ---------------------------------------------------------------- props init
static void initParamProps(PropertySet& ps, ParamKind k, const std::string& name) {
    ps.defString(kOfxPropType, kOfxTypeParameter, true);
    ps.defString(kOfxPropName, name, true);
    ps.defString(kOfxParamPropType, paramKindToOfx(k), true);
    ps.defString(kOfxPropLabel, name);
    ps.defString(kOfxPropShortLabel, name);
    ps.defString(kOfxPropLongLabel, name);
    ps.defString(kOfxPropIcon, std::vector<std::string>{"", ""});
    ps.defInt(kOfxParamPropSecret, 0);
    ps.defString(kOfxParamPropHint, "");
    ps.defString(kOfxParamPropScriptName, name);
    ps.defString(kOfxParamPropParent, "");
    ps.defInt(kOfxParamPropEnabled, 1);
    ps.defPointer(kOfxParamPropDataPtr, nullptr);
    if (k == ParamKind::Page || k == ParamKind::Group) {
        if (k == ParamKind::Page) ps.defString(kOfxParamPropPageChild, std::vector<std::string>{});
        if (k == ParamKind::Group) ps.defInt(kOfxParamPropGroupOpen, 1);
        return;
    }
    ps.defPointer(kOfxParamPropInteractV1, nullptr);
    ps.defDouble(kOfxParamPropInteractSize, std::vector<double>{0.0, 0.0});
    ps.defDouble(kOfxParamPropInteractSizeAspect, 1.0);
    ps.defInt(kOfxParamPropInteractMinimumSize, std::vector<int>{10, 10});
    ps.defInt(kOfxParamPropInteractPreferedSize, std::vector<int>{10, 10});
    ps.defInt(kOfxParamPropHasHostOverlayHandle, 0, true);
    ps.defInt(kOfxParamPropUseHostOverlayHandle, 0);
    if (k == ParamKind::PushButton) return;

    ps.defInt(kOfxParamPropAnimates, (k == ParamKind::String || k == ParamKind::Custom || k == ParamKind::Bytes ||
                                      k == ParamKind::StrChoice) ? 0 : 1);
    ps.defInt(kOfxParamPropIsAnimating, 0, true);
    ps.defInt(kOfxParamPropIsAutoKeying, 0, true);
    ps.defInt(kOfxParamPropPersistant, 1);
    ps.defInt(kOfxParamPropEvaluateOnChange, 1);
    ps.defInt(kOfxParamPropPluginMayWrite, 0);
    ps.defString(kOfxParamPropCacheInvalidation, kOfxParamInvalidateValueChange);
    ps.defInt(kOfxParamPropCanUndo, 1);

    const double big = DBL_MAX;
    switch (k) {
    case ParamKind::Integer:
        ps.defInt(kOfxParamPropDefault, 0);
        ps.defInt(kOfxParamPropMin, INT_MIN); ps.defInt(kOfxParamPropMax, INT_MAX);
        ps.defInt(kOfxParamPropDisplayMin, INT_MIN); ps.defInt(kOfxParamPropDisplayMax, INT_MAX);
        break;
    case ParamKind::Integer2D:
    case ParamKind::Integer3D: {
        size_t n = k == ParamKind::Integer2D ? 2 : 3;
        ps.defInt(kOfxParamPropDefault, std::vector<int>(n, 0));
        ps.defInt(kOfxParamPropMin, std::vector<int>(n, INT_MIN)); ps.defInt(kOfxParamPropMax, std::vector<int>(n, INT_MAX));
        ps.defInt(kOfxParamPropDisplayMin, std::vector<int>(n, INT_MIN)); ps.defInt(kOfxParamPropDisplayMax, std::vector<int>(n, INT_MAX));
        ps.defString(kOfxParamPropDimensionLabel, n == 2 ? std::vector<std::string>{"x", "y"} : std::vector<std::string>{"x", "y", "z"});
        break;
    }
    case ParamKind::Double:
    case ParamKind::Double2D:
    case ParamKind::Double3D: {
        size_t n = k == ParamKind::Double ? 1 : (k == ParamKind::Double2D ? 2 : 3);
        ps.defDouble(kOfxParamPropDefault, std::vector<double>(n, 0.0));
        ps.defDouble(kOfxParamPropMin, std::vector<double>(n, -big)); ps.defDouble(kOfxParamPropMax, std::vector<double>(n, big));
        ps.defDouble(kOfxParamPropDisplayMin, std::vector<double>(n, -big)); ps.defDouble(kOfxParamPropDisplayMax, std::vector<double>(n, big));
        ps.defDouble(kOfxParamPropIncrement, 1.0);
        ps.defInt(kOfxParamPropDigits, 2);
        ps.defString(kOfxParamPropDoubleType, kOfxParamDoubleTypePlain);
        ps.defString(kOfxParamPropDefaultCoordinateSystem, kOfxParamCoordinatesCanonical);
        if (n == 1) ps.defInt(kOfxParamPropShowTimeMarker, 0);
        if (n == 2) ps.defString(kOfxParamPropDimensionLabel, std::vector<std::string>{"x", "y"});
        if (n == 3) ps.defString(kOfxParamPropDimensionLabel, std::vector<std::string>{"x", "y", "z"});
        break;
    }
    case ParamKind::RGB:
    case ParamKind::RGBA: {
        size_t n = k == ParamKind::RGB ? 3 : 4;
        ps.defDouble(kOfxParamPropDefault, std::vector<double>(n, 0.0));
        ps.defDouble(kOfxParamPropMin, std::vector<double>(n, -big)); ps.defDouble(kOfxParamPropMax, std::vector<double>(n, big));
        ps.defDouble(kOfxParamPropDisplayMin, std::vector<double>(n, 0.0)); ps.defDouble(kOfxParamPropDisplayMax, std::vector<double>(n, 1.0));
        ps.defString(kOfxParamPropDimensionLabel, n == 3 ? std::vector<std::string>{"r", "g", "b"}
                                                         : std::vector<std::string>{"r", "g", "b", "a"});
        break;
    }
    case ParamKind::Boolean:
        ps.defInt(kOfxParamPropDefault, 0);
        break;
    case ParamKind::Choice:
        ps.defInt(kOfxParamPropDefault, 0);
        ps.defString(kOfxParamPropChoiceOption, std::vector<std::string>{});
        ps.defInt(kOfxParamPropChoiceOrder, std::vector<int>{});
        break;
    case ParamKind::StrChoice:
        ps.defString(kOfxParamPropDefault, "");
        ps.defString(kOfxParamPropChoiceOption, std::vector<std::string>{});
        ps.defString(kOfxParamPropChoiceEnum, std::vector<std::string>{});
        break;
    case ParamKind::String:
        ps.defString(kOfxParamPropDefault, "");
        ps.defString(kOfxParamPropStringMode, kOfxParamStringIsSingleLine);
        ps.defInt(kOfxParamPropStringFilePathExists, 1);
        break;
    case ParamKind::Custom:
        ps.defString(kOfxParamPropDefault, "");
        ps.defPointer(kOfxParamPropCustomInterpCallbackV1, nullptr);
        break;
    case ParamKind::Bytes:
        ps.defString(kOfxParamPropDefault, "");
        break;
    case ParamKind::Parametric:
        ps.defInt(kOfxParamPropParametricDimension, 1);
        ps.defDouble(kOfxParamPropParametricUIColour, std::vector<double>{});
        ps.defPointer(kOfxParamPropParametricInteractBackground, nullptr);
        ps.defDouble(kOfxParamPropParametricRange, std::vector<double>{0.0, 1.0});
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- Param
Param::Param(ParamKind k, std::string name, ParamSet* owner)
    : kind_(k), name_(std::move(name)), owner_(owner), props_("Param:" + name_) {
    initParamProps(props_, k, name_);
}

Param* Param::from(OfxParamHandle h) {
    auto* p = reinterpret_cast<Param*>(h);
    if (!p || p->magic_ != kMagic) return nullptr;
    return p;
}

int Param::dim() const {
    switch (kind_) {
    case ParamKind::Integer: case ParamKind::Double: case ParamKind::Boolean: case ParamKind::Choice: return 1;
    case ParamKind::Double2D: case ParamKind::Integer2D: return 2;
    case ParamKind::Double3D: case ParamKind::Integer3D: case ParamKind::RGB: return 3;
    case ParamKind::RGBA: return 4;
    default: return 0;
    }
}
bool Param::hasValue() const {
    return !(kind_ == ParamKind::Group || kind_ == ParamKind::Page || kind_ == ParamKind::PushButton ||
             kind_ == ParamKind::Parametric || kind_ == ParamKind::Unknown);
}
bool Param::isStringValued() const {
    return kind_ == ParamKind::String || kind_ == ParamKind::StrChoice || kind_ == ParamKind::Custom || kind_ == ParamKind::Bytes;
}
bool Param::isIntValued() const {
    return kind_ == ParamKind::Integer || kind_ == ParamKind::Integer2D || kind_ == ParamKind::Integer3D ||
           kind_ == ParamKind::Boolean || kind_ == ParamKind::Choice;
}
bool Param::animates() const { return props_.getInt(kOfxParamPropAnimates, 0, 0) != 0; }

ParamValue Param::defaultValue() const {
    ParamValue v;
    if (isStringValued()) { v.str = props_.getString(kOfxParamPropDefault); return v; }
    int n = dim();
    v.num.resize(size_t(n), 0.0);
    for (int k = 0; k < n; ++k) {
        if (isIntValued()) v.num[size_t(k)] = props_.getInt(kOfxParamPropDefault, k, 0);
        else v.num[size_t(k)] = props_.getDouble(kOfxParamPropDefault, k, 0.0);
    }
    return v;
}

void Param::updateAnimating() { props_.setInt(kOfxParamPropIsAnimating, 0, keys_.empty() ? 0 : 1); }

ParamValue Param::valueAt(double t) const {
    if (keys_.empty()) return value_;
    auto hi = keys_.lower_bound(t);
    if (hi == keys_.end()) return std::prev(hi)->second;
    if (hi->first == t || hi == keys_.begin()) return hi->second;
    auto lo = std::prev(hi);
    bool interp = !isStringValued() && kind_ != ParamKind::Boolean && kind_ != ParamKind::Choice;
    if (!interp) return lo->second;
    double a = (t - lo->first) / (hi->first - lo->first);
    ParamValue r = lo->second;
    for (size_t k = 0; k < r.num.size() && k < hi->second.num.size(); ++k) {
        r.num[k] = lo->second.num[k] + a * (hi->second.num[k] - lo->second.num[k]);
        if (isIntValued()) r.num[k] = std::floor(r.num[k] + 0.5);
    }
    return r;
}

// ---------------------------------------------------------------- ParamSet
ParamSet* ParamSet::from(OfxParamSetHandle h) {
    auto* p = reinterpret_cast<ParamSet*>(h);
    if (!p || p->magic_ != kMagic) return nullptr;
    return p;
}

Param* ParamSet::define(ParamKind k, const std::string& name) {
    if (byName_.count(name)) return nullptr;
    params_.emplace_back(new Param(k, name, this));
    Param* p = params_.back().get();
    byName_[name] = p;
    p->setStatic(p->defaultValue());
    return p;
}

Param* ParamSet::get(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

void ParamSet::instantiateFrom(const ParamSet& desc) {
    props_ = desc.props_;
    for (auto& dp : desc.params_) {
        params_.emplace_back(new Param(dp->kind(), dp->name(), this));
        Param* p = params_.back().get();
        p->props() = dp->props();
        p->props().setSetName("ParamInstance:" + dp->name());
        p->setStatic(p->defaultValue());
        byName_[p->name()] = p;
    }
}

// ---------------------------------------------------------------- suite
namespace {

OfxStatus paramDefine(OfxParamSetHandle hs, const char* paramType, const char* name, OfxPropertySetHandle* propertySet) {
    ParamSet* ps = ParamSet::from(hs);
    if (!ps) return kOfxStatErrBadHandle;
    if (!paramType || !name) return kOfxStatErrValue;
    if (!ps->isDescriptor()) {
        diag().warn(std::string("paramDefine called on an instance param set for '") + name + "'");
        return kOfxStatErrBadHandle;
    }
    ParamKind k = paramKindFromOfx(paramType);
    if (k == ParamKind::Unknown) {
        diag().warn(std::string("paramDefine with unknown type '") + paramType + "' for '" + name + "'");
        return kOfxStatErrUnknown;
    }
    if (ps->get(name)) {
        diag().warn(std::string("paramDefine: duplicate parameter '") + name + "'");
        return kOfxStatErrExists;
    }
    Param* p = ps->define(k, name);
    if (propertySet) *propertySet = p->props().handle();
    return kOfxStatOK;
}

OfxStatus paramGetHandle(OfxParamSetHandle hs, const char* name, OfxParamHandle* param, OfxPropertySetHandle* propertySet) {
    ParamSet* ps = ParamSet::from(hs);
    if (!ps) return kOfxStatErrBadHandle;
    if (!name) return kOfxStatErrUnknown;
    Param* p = ps->get(name);
    if (!p) return kOfxStatErrUnknown;
    if (param) *param = p->handle();
    if (propertySet) *propertySet = p->props().handle();
    return kOfxStatOK;
}

OfxStatus paramSetGetPropertySet(OfxParamSetHandle hs, OfxPropertySetHandle* propHandle) {
    ParamSet* ps = ParamSet::from(hs);
    if (!ps) return kOfxStatErrBadHandle;
    if (propHandle) *propHandle = ps->props().handle();
    return kOfxStatOK;
}

OfxStatus paramGetPropertySet(OfxParamHandle h, OfxPropertySetHandle* propHandle) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    if (propHandle) *propHandle = p->props().handle();
    return kOfxStatOK;
}

// Numeric values only (string-valued parameters are handled in getValueImpl).
OfxStatus writeValue(const Param& p, const ParamValue& v, va_list ap) {
    for (int k = 0; k < p.dim(); ++k) {
        if (p.isIntValued()) {
            int* out = va_arg(ap, int*);
            if (!out) return kOfxStatErrValue;
            *out = int(v.num[size_t(k)]);
        } else {
            double* out = va_arg(ap, double*);
            if (!out) return kOfxStatErrValue;
            *out = v.num[size_t(k)];
        }
    }
    return kOfxStatOK;
}

// String values at time: we keep a per-thread scratch so the returned pointer stays valid
// until the next call on the same thread (documented host behaviour; plug-ins copy at once).
thread_local std::string t_strScratch;

OfxStatus getValueImpl(Param* p, double t, bool atTime, va_list ap) {
    if (!p->hasValue()) return kOfxStatErrBadHandle;
    if (p->isStringValued()) {
        const char** out = va_arg(ap, const char**);
        if (!out) return kOfxStatErrValue;
        if (!atTime || !p->hasKeys()) { *out = p->staticValue().str.c_str(); return kOfxStatOK; }
        t_strScratch = p->valueAt(t).str;
        *out = t_strScratch.c_str();
        return kOfxStatOK;
    }
    ParamValue v = atTime ? p->valueAt(t) : p->valueAt(p->owner()->currentTime());
    return writeValue(*p, v, ap);
}

OfxStatus paramGetValue(OfxParamHandle h, ...) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    va_list ap; va_start(ap, h);
    OfxStatus st = getValueImpl(p, 0.0, false, ap);
    va_end(ap);
    return st;
}

OfxStatus paramGetValueAtTime(OfxParamHandle h, OfxTime time, ...) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    va_list ap; va_start(ap, time);
    OfxStatus st = getValueImpl(p, time, true, ap);
    va_end(ap);
    return st;
}

OfxStatus paramGetDerivative(OfxParamHandle h, OfxTime time, ...) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    if (p->isStringValued() || p->isIntValued() || !p->hasValue()) return kOfxStatErrBadHandle;
    const double eps = 1e-3;
    ParamValue a = p->valueAt(time - eps), b = p->valueAt(time + eps);
    va_list ap; va_start(ap, time);
    for (int k = 0; k < p->dim(); ++k) {
        double* out = va_arg(ap, double*);
        if (out) *out = (b.num[size_t(k)] - a.num[size_t(k)]) / (2 * eps);
    }
    va_end(ap);
    return kOfxStatOK;
}

OfxStatus paramGetIntegral(OfxParamHandle h, OfxTime t1, OfxTime t2, ...) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    if (p->isStringValued() || p->isIntValued() || !p->hasValue()) return kOfxStatErrBadHandle;
    const int n = 256;
    std::vector<double> acc(size_t(p->dim()), 0.0);
    double dt = (t2 - t1) / n;
    for (int s = 0; s <= n; ++s) {
        double w = (s == 0 || s == n) ? 0.5 : 1.0;
        ParamValue v = p->valueAt(t1 + dt * s);
        for (int k = 0; k < p->dim(); ++k) acc[size_t(k)] += w * v.num[size_t(k)] * dt;
    }
    va_list ap; va_start(ap, t2);
    for (int k = 0; k < p->dim(); ++k) {
        double* out = va_arg(ap, double*);
        if (out) *out = acc[size_t(k)];
    }
    va_end(ap);
    return kOfxStatOK;
}

OfxStatus readArgs(Param* p, va_list ap, ParamValue& v) {
    if (p->isStringValued()) {
        const char* s = va_arg(ap, const char*);
        v.str = s ? s : "";
        return kOfxStatOK;
    }
    v.num.resize(size_t(p->dim()));
    for (int k = 0; k < p->dim(); ++k) {
        if (p->isIntValued()) v.num[size_t(k)] = va_arg(ap, int);
        else v.num[size_t(k)] = va_arg(ap, double);
    }
    return kOfxStatOK;
}

OfxStatus setValueImpl(Param* p, bool atTime, double time, va_list ap) {
    if (!p->hasValue()) return kOfxStatErrBadHandle;
    if (p->owner()->isDescriptor()) {
        diag().warn("paramSetValue on a descriptor parameter '" + p->name() + "'");
        return kOfxStatErrBadHandle;
    }
    if (p->owner()->inRender()) {
        diag().warn("paramSetValue called during render on '" + p->name() + "' (illegal in OFX)");
    }
    ParamValue v;
    OfxStatus st = readArgs(p, ap, v);
    if (st != kOfxStatOK) return st;
    if (atTime) {
        p->setKey(time, v);
    } else if (p->hasKeys()) {
        p->setKey(p->owner()->currentTime(), v);
    } else {
        p->setStatic(v);
    }
    if (p->owner()->onPluginChanged) p->owner()->onPluginChanged(p);
    return kOfxStatOK;
}

OfxStatus paramSetValue(OfxParamHandle h, ...) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    va_list ap; va_start(ap, h);
    OfxStatus st = setValueImpl(p, false, 0.0, ap);
    va_end(ap);
    return st;
}

OfxStatus paramSetValueAtTime(OfxParamHandle h, OfxTime time, ...) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    va_list ap; va_start(ap, time);
    OfxStatus st = setValueImpl(p, true, time, ap);
    va_end(ap);
    return st;
}

OfxStatus paramGetNumKeys(OfxParamHandle h, unsigned int* numberOfKeys) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    if (numberOfKeys) *numberOfKeys = unsigned(p->keys().size());
    return kOfxStatOK;
}

OfxStatus paramGetKeyTime(OfxParamHandle h, unsigned int nthKey, OfxTime* time) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    if (nthKey >= p->keys().size()) return kOfxStatErrBadIndex;
    auto it = p->keys().begin();
    std::advance(it, nthKey);
    if (time) *time = it->first;
    return kOfxStatOK;
}

OfxStatus paramGetKeyIndex(OfxParamHandle h, OfxTime time, int direction, int* index) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    int i = 0;
    int found = -1;
    for (auto& kv : p->keys()) {
        if (direction == 0 && kv.first == time) { found = i; break; }
        if (direction < 0 && kv.first < time) found = i;
        if (direction > 0 && kv.first > time) { found = i; break; }
        ++i;
    }
    if (found < 0) return kOfxStatFailed;
    if (index) *index = found;
    return kOfxStatOK;
}

OfxStatus paramDeleteKey(OfxParamHandle h, OfxTime time) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    auto it = p->keys().find(time);
    if (it == p->keys().end()) return kOfxStatErrBadIndex;
    if (p->keys().size() == 1) p->setStatic(it->second);
    p->keys().erase(it);
    p->updateAnimating();
    if (p->owner()->onPluginChanged) p->owner()->onPluginChanged(p);
    return kOfxStatOK;
}

OfxStatus paramDeleteAllKeys(OfxParamHandle h) {
    Param* p = Param::from(h);
    if (!p) return kOfxStatErrBadHandle;
    if (p->hasKeys()) {
        p->setStatic(p->valueAt(p->owner()->currentTime()));
        p->keys().clear();
        p->updateAnimating();
        if (p->owner()->onPluginChanged) p->owner()->onPluginChanged(p);
    }
    return kOfxStatOK;
}

OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime dstOffset, const OfxRangeD* frameRange) {
    Param* a = Param::from(to);
    Param* b = Param::from(from);
    if (!a || !b) return kOfxStatErrBadHandle;
    if (a->kind() != b->kind()) return kOfxStatErrValue;
    if (!frameRange) {
        a->setStatic(b->staticValue());
        a->keys().clear();
        for (auto& kv : b->keys()) a->keys()[kv.first + dstOffset] = kv.second;
    } else {
        for (auto& kv : b->keys())
            if (kv.first >= frameRange->min && kv.first <= frameRange->max) a->keys()[kv.first + dstOffset] = kv.second;
    }
    a->updateAnimating();
    if (a->owner()->onPluginChanged) a->owner()->onPluginChanged(a);
    return kOfxStatOK;
}

OfxStatus paramEditBegin(OfxParamSetHandle hs, const char*) {
    return ParamSet::from(hs) ? kOfxStatOK : kOfxStatErrBadHandle;
}
OfxStatus paramEditEnd(OfxParamSetHandle hs) {
    return ParamSet::from(hs) ? kOfxStatOK : kOfxStatErrBadHandle;
}

OfxParameterSuiteV1 g_paramSuite = {
    paramDefine, paramGetHandle, paramSetGetPropertySet, paramGetPropertySet,
    paramGetValue, paramGetValueAtTime, paramGetDerivative, paramGetIntegral,
    paramSetValue, paramSetValueAtTime, paramGetNumKeys, paramGetKeyTime, paramGetKeyIndex,
    paramDeleteKey, paramDeleteAllKeys, paramCopy, paramEditBegin, paramEditEnd,
};

} // namespace

OfxParameterSuiteV1* parameterSuite() { return &g_paramSuite; }

// ---------------------------------------------------------------- CLI helpers
static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
static bool parseDouble(const std::string& s, double& v) {
    std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    v = std::strtod(t.c_str(), &end);
    return end && *end == 0;
}
static bool parseIntStrict(const std::string& s, long& v) {
    std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    v = std::strtol(t.c_str(), &end, 10);
    return end && *end == 0;
}
static std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> r;
    std::string cur;
    for (char c : s) {
        if (c == ',') { r.push_back(cur); cur.clear(); } else cur += c;
    }
    r.push_back(cur);
    return r;
}

// Match a label among options. Exact, then case-insensitive, then unique case-insensitive prefix,
// then unique case-insensitive substring.
static int matchLabel(const std::vector<std::string>& opts, const std::string& want) {
    for (size_t i = 0; i < opts.size(); ++i) if (opts[i] == want) return int(i);
    std::string w = lower(want);
    for (size_t i = 0; i < opts.size(); ++i) if (lower(opts[i]) == w) return int(i);
    int hit = -1, n = 0;
    for (size_t i = 0; i < opts.size(); ++i) if (lower(opts[i]).rfind(w, 0) == 0) { hit = int(i); ++n; }
    if (n == 1) return hit;
    hit = -1; n = 0;
    for (size_t i = 0; i < opts.size(); ++i) if (lower(opts[i]).find(w) != std::string::npos) { hit = int(i); ++n; }
    if (n == 1) return hit;
    return -1;
}

bool parseParamValue(const Param& p, const std::string& textIn, ParamValue& out, std::string& err) {
    std::string text = textIn;
    out = ParamValue{};
    switch (p.kind()) {
    case ParamKind::String:
    case ParamKind::Custom:
    case ParamKind::Bytes:
        out.str = text;
        return true;
    case ParamKind::StrChoice: {
        auto en = p.props().getStrings(kOfxParamPropChoiceEnum);
        auto op = p.props().getStrings(kOfxParamPropChoiceOption);
        if (text.rfind("label:", 0) == 0) {
            int i = matchLabel(op, text.substr(6));
            if (i < 0 || size_t(i) >= en.size()) { err = "no option label matches '" + text.substr(6) + "'"; return false; }
            out.str = en[size_t(i)];
            return true;
        }
        for (auto& e : en) if (e == text) { out.str = e; return true; }
        int i = matchLabel(op, text);
        if (i >= 0 && size_t(i) < en.size()) { out.str = en[size_t(i)]; return true; }
        i = matchLabel(en, text);
        if (i >= 0) { out.str = en[size_t(i)]; return true; }
        err = "no enum/label matches '" + text + "'";
        return false;
    }
    case ParamKind::Choice: {
        auto op = p.props().getStrings(kOfxParamPropChoiceOption);
        long idx;
        if (text.rfind("index:", 0) == 0) {
            if (!parseIntStrict(text.substr(6), idx) || idx < 0 || idx >= long(op.size())) {
                err = "choice index out of range (0.." + std::to_string(op.size() ? op.size() - 1 : 0) + ")";
                return false;
            }
            out.num = {double(idx)};
            return true;
        }
        if (text.rfind("label:", 0) == 0) {
            int i = matchLabel(op, text.substr(6));
            if (i < 0) { err = "no option label matches '" + text.substr(6) + "'"; return false; }
            out.num = {double(i)};
            return true;
        }
        if (parseIntStrict(text, idx) && idx >= 0 && idx < long(op.size())) { out.num = {double(idx)}; return true; }
        int i = matchLabel(op, text);
        if (i >= 0) { out.num = {double(i)}; return true; }
        err = "'" + text + "' is neither a valid index nor a unique option label";
        return false;
    }
    case ParamKind::Boolean: {
        std::string t = lower(trim(text));
        if (t == "1" || t == "true" || t == "yes" || t == "on") { out.num = {1}; return true; }
        if (t == "0" || t == "false" || t == "no" || t == "off") { out.num = {0}; return true; }
        err = "expected a boolean (true/false/1/0/on/off)";
        return false;
    }
    default:
        break;
    }
    if (!p.hasValue()) { err = "parameter has no value (use --press for push buttons)"; return false; }
    auto parts = splitComma(text);
    int n = p.dim();
    if (int(parts.size()) == 1 && n > 1) parts.assign(size_t(n), parts[0]);
    if (int(parts.size()) != n) { err = "expected " + std::to_string(n) + " comma separated values"; return false; }
    out.num.resize(size_t(n));
    for (int k = 0; k < n; ++k) {
        double v;
        if (!parseDouble(parts[size_t(k)], v)) { err = "cannot parse number '" + parts[size_t(k)] + "'"; return false; }
        if (p.isIntValued()) {
            if (v != std::floor(v)) { err = "integer parameter needs an integer value"; return false; }
        }
        out.num[size_t(k)] = v;
    }
    return true;
}

std::string formatParamValue(const Param& p, const ParamValue& v) {
    if (p.isStringValued()) return v.str;
    std::ostringstream os;
    os.precision(10);
    for (size_t k = 0; k < v.num.size(); ++k) {
        if (k) os << ',';
        if (p.isIntValued()) os << long(v.num[k]); else os << v.num[k];
    }
    if (p.kind() == ParamKind::Choice && !v.num.empty()) {
        auto op = p.props().getStrings(kOfxParamPropChoiceOption);
        size_t i = size_t(v.num[0]);
        if (i < op.size()) os << " (" << op[i] << ")";
    }
    return os.str();
}

bool clampParamValue(const Param& p, ParamValue& v) {
    if (p.isStringValued() || !p.hasValue()) return false;
    if (p.kind() == ParamKind::Boolean || p.kind() == ParamKind::Choice) return false;
    bool clamped = false;
    for (int k = 0; k < p.dim(); ++k) {
        double lo = p.props().getDouble(kOfxParamPropMin, k, -DBL_MAX);
        double hi = p.props().getDouble(kOfxParamPropMax, k, DBL_MAX);
        double& x = v.num[size_t(k)];
        if (x < lo) { x = lo; clamped = true; }
        if (x > hi) { x = hi; clamped = true; }
    }
    return clamped;
}

} // namespace ofxhost
