// Celluloid Pro - OFX plug-in (factory declaration). Implementation: CelluloidPlugin.cpp.
#pragma once
#include "ofxsImageEffect.h"

namespace cel { namespace ofxp {

class CelluloidFactory : public OFX::PluginFactoryHelper<CelluloidFactory> {
public:
    CelluloidFactory();
    void load() override;
    void unload() override;
    void describe(OFX::ImageEffectDescriptor& desc) override;
    void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context) override;
    OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context) override;
};

}} // namespace cel::ofxp
