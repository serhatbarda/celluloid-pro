// ofxhost - parameters (descriptors + instances) and OfxParameterSuiteV1.
#pragma once

#include "Props.h"
#include "ofxParam.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ofxhost {

enum class ParamKind {
    Integer, Double, Boolean, Choice, StrChoice, RGBA, RGB, Double2D, Integer2D, Double3D, Integer3D,
    String, Custom, Bytes, Group, Page, PushButton, Parametric, Unknown
};

ParamKind paramKindFromOfx(const std::string& t);
const char* paramKindToOfx(ParamKind k);
const char* paramKindShort(ParamKind k);   // "Double", "Choice", ...

struct ParamValue {
    std::vector<double> num;   // ints/bools/choice indices are stored exactly as doubles
    std::string str;
    bool operator==(const ParamValue& o) const { return num == o.num && str == o.str; }
};

class ParamSet;

class Param {
public:
    static constexpr unsigned kMagic = 0x0F1A9A3Au;
    Param(ParamKind k, std::string name, ParamSet* owner);
    ~Param() { magic_ = 0; }
    static Param* from(OfxParamHandle h);
    OfxParamHandle handle() { return reinterpret_cast<OfxParamHandle>(this); }

    ParamKind kind() const { return kind_; }
    const std::string& name() const { return name_; }
    PropertySet& props() { return props_; }
    const PropertySet& props() const { return props_; }
    ParamSet* owner() const { return owner_; }

    int dim() const;               // number of numeric components (0 for string/no-value types)
    bool hasValue() const;         // false for group/page/button
    bool isStringValued() const;   // String, StrChoice, Custom
    bool isIntValued() const;      // Integer*, Boolean, Choice
    bool animates() const;

    ParamValue defaultValue() const;     // from kOfxParamPropDefault
    ParamValue valueAt(double t) const;  // interpolated
    const ParamValue& staticValue() const { return value_; }
    void setStatic(const ParamValue& v) { value_ = v; }
    void setKey(double t, const ParamValue& v) { keys_[t] = v; updateAnimating(); }
    bool hasKeys() const { return !keys_.empty(); }
    std::map<double, ParamValue>& keys() { return keys_; }
    void updateAnimating();
    void resetToDefault() { value_ = defaultValue(); keys_.clear(); updateAnimating(); }

private:
    unsigned magic_ = kMagic;
    ParamKind kind_;
    std::string name_;
    ParamSet* owner_;
    PropertySet props_;
    ParamValue value_;
    std::map<double, ParamValue> keys_;
};

class ParamSet {
public:
    static constexpr unsigned kMagic = 0x0F1A5E7Au;
    explicit ParamSet(bool descriptor) : descriptor_(descriptor), props_("ParameterSet") {
        props_.defInt(kOfxPropParamSetNeedsSyncing, 0);
        props_.defString(kOfxPluginPropParamPageOrder, std::vector<std::string>{});
    }
    ~ParamSet() { magic_ = 0; }
    static ParamSet* from(OfxParamSetHandle h);
    OfxParamSetHandle handle() { return reinterpret_cast<OfxParamSetHandle>(this); }

    bool isDescriptor() const { return descriptor_; }
    PropertySet& props() { return props_; }
    Param* define(ParamKind k, const std::string& name);
    Param* get(const std::string& name) const;
    const std::vector<std::unique_ptr<Param>>& params() const { return params_; }

    // Instance creation: deep copy descriptor params, value = default.
    void instantiateFrom(const ParamSet& desc);

    // Hooks set by the effect instance.
    std::function<double()> currentTime = [] { return 0.0; };
    // called after a plug-in initiated value change (paramSetValue / AtTime / key ops)
    std::function<void(Param*)> onPluginChanged;
    // true while the host is inside a render action (plug-in may not set values then)
    std::function<bool()> inRender = [] { return false; };

private:
    unsigned magic_ = kMagic;
    bool descriptor_;
    PropertySet props_;
    std::vector<std::unique_ptr<Param>> params_;
    std::map<std::string, Param*> byName_;
};

OfxParameterSuiteV1* parameterSuite();

// Helpers used by the CLI ----------------------------------------------------
// Parse a user string into a value for the parameter. Returns false with an error message.
bool parseParamValue(const Param& p, const std::string& text, ParamValue& out, std::string& err);
std::string formatParamValue(const Param& p, const ParamValue& v);
// Clamp numeric values to [min,max]; returns true if anything was clamped.
bool clampParamValue(const Param& p, ParamValue& v);

} // namespace ofxhost
