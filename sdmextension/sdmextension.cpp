/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Wrapper around the Qualcomm SDM extension (libsdmextension_qti.so).
 *
 * In the DCI-P3 color mode the stock strategy sends layers outside the P3
 * gamut to the GPU but stages the GPU target above every device layer. SF only
 * clears the client target under opaque layers, so those layers get drawn over
 * translucent device layers stacked above them, hiding their content.
 *
 * While the blend space is P3, send everything from the lowest layer that
 * needs the GPU up to the top of the stack to the GPU, so staging the GPU
 * target on top is correct.
 */

#define LOG_TAG "SDMExtnWrapper"

#include <dlfcn.h>
#include <log/log.h>

#include <private/extension_interface.h>

namespace sdm {
namespace {

class StrategyWrapper : public StrategyInterface {
  public:
    explicit StrategyWrapper(StrategyInterface* real) : real_(real) {}
    StrategyInterface* real() const { return real_; }

    DisplayError Start(HWLayersInfo* info, uint32_t* max_attempts) override {
        if (p3_) SendTopToGpu(info);
        return real_->Start(info, max_attempts);
    }
    DisplayError SetBlendSpace(const PrimariesTransfer& blend_space) override {
        p3_ = blend_space.primaries == ColorPrimaries_DCIP3;
        return real_->SetBlendSpace(blend_space);
    }

    DisplayError GetNextStrategy(StrategyConstraints* c) override {
        return real_->GetNextStrategy(c);
    }
    DisplayError Stop() override { return real_->Stop(); }
    DisplayError Reconfigure(const HWPanelInfo& panel, const HWResourceInfo& res,
                             const HWMixerAttributes& mixer,
                             const DisplayConfigVariableInfo& fb) override {
        return real_->Reconfigure(panel, res, mixer, fb);
    }
    DisplayError SetCompositionState(LayerComposition type, bool enable) override {
        return real_->SetCompositionState(type, enable);
    }
    DisplayError Purge() override { return real_->Purge(); }
    DisplayError SetIdleTimeoutMs(uint32_t ms) override { return real_->SetIdleTimeoutMs(ms); }
    DisplayError SetColorModesInfo(const std::vector<PrimariesTransfer>& cs) override {
        return real_->SetColorModesInfo(cs);
    }
    bool CanSkipValidate() override { return real_->CanSkipValidate(); }

  private:
    void SendTopToGpu(HWLayersInfo* info) {
        // Layers below app_layer_count never include the GPU target.
        std::vector<Layer*>& layers = info->stack->layers;
        uint32_t count = info->app_layer_count;
        uint32_t i = 0;

        while (i < count && !layers[i]->flags.skip &&
               layers[i]->input_buffer.color_metadata.colorPrimaries == ColorPrimaries_DCIP3) {
            i++;
        }
        if (i != logged_) {
            ALOGD("P3 blend space: GPU composition from layer %u of %u", i, count);
            logged_ = i;
        }
        if (i == count) return;

        info->stack->flags.skip_present = true;
        for (; i < count; i++) layers[i]->flags.skip = true;
    }

    StrategyInterface* real_;
    bool p3_ = false;
    uint32_t logged_ = UINT32_MAX;
};

class ExtensionWrapper : public ExtensionInterface {
  public:
    explicit ExtensionWrapper(ExtensionInterface* real) : real_(real) {}
    ExtensionInterface* real() const { return real_; }

    DisplayError CreateStrategyExtn(int32_t id, DisplayType type, BufferAllocator* alloc,
                                    const HWResourceInfo& res, const HWPanelInfo& panel,
                                    const HWMixerAttributes& mixer,
                                    const DisplayConfigVariableInfo& fb,
                                    StrategyInterface** intf) override {
        StrategyInterface* real = nullptr;
        DisplayError error =
                real_->CreateStrategyExtn(id, type, alloc, res, panel, mixer, fb, &real);
        *intf = (error == kErrorNone && real) ? new StrategyWrapper(real) : nullptr;
        return *intf ? kErrorNone : (error != kErrorNone ? error : kErrorUndefined);
    }
    DisplayError DestroyStrategyExtn(StrategyInterface* intf) override {
        auto* wrapper = static_cast<StrategyWrapper*>(intf);
        DisplayError error = real_->DestroyStrategyExtn(wrapper->real());
        delete wrapper;
        return error;
    }

    DisplayError CreatePartialUpdate(int32_t id, DisplayType type, const HWResourceInfo& res,
                                     const HWPanelInfo& panel, const HWMixerAttributes& mixer,
                                     const HWDisplayAttributes& attrs,
                                     const DisplayConfigVariableInfo& fb,
                                     PartialUpdateInterface** intf) override {
        return real_->CreatePartialUpdate(id, type, res, panel, mixer, attrs, fb, intf);
    }
    DisplayError DestroyPartialUpdate(PartialUpdateInterface* intf) override {
        return real_->DestroyPartialUpdate(intf);
    }
    DisplayError CreateResourceExtn(const HWResourceInfo& res, BufferAllocator* alloc,
                                    BufferSyncHandler* sync, ResourceInterface** intf) override {
        return real_->CreateResourceExtn(res, alloc, sync, intf);
    }
    DisplayError DestroyResourceExtn(ResourceInterface* intf) override {
        return real_->DestroyResourceExtn(intf);
    }
    DisplayError CreateDppsControlExtn(DppsControlInterface** intf,
                                       SocketHandler* socket) override {
        return real_->CreateDppsControlExtn(intf, socket);
    }
    DisplayError DestroyDppsControlExtn(DppsControlInterface* intf) override {
        return real_->DestroyDppsControlExtn(intf);
    }

  private:
    ExtensionInterface* real_;
};

template <typename T>
T RealSymbol(const char* name) {
    static void* lib = dlopen("libsdmextension_qti.so", RTLD_NOW);
    void* sym = lib ? dlsym(lib, name) : nullptr;
    if (!sym) ALOGE("Failed to resolve %s: %s", name, dlerror());
    return reinterpret_cast<T>(sym);
}

}  // namespace
}  // namespace sdm

using sdm::DisplayError;
using sdm::ExtensionInterface;
using sdm::ExtensionWrapper;
using sdm::kErrorNone;
using sdm::kErrorUndefined;

extern "C" DisplayError CreateExtensionInterface(uint16_t version, ExtensionInterface** intf) {
    static auto create =
            sdm::RealSymbol<sdm::CreateExtensionInterface>(CREATE_EXTENSION_INTERFACE_NAME);
    ExtensionInterface* real = nullptr;
    DisplayError error = create ? create(version, &real) : kErrorUndefined;
    *intf = (error == kErrorNone && real) ? new ExtensionWrapper(real) : nullptr;
    return *intf ? kErrorNone : (error != kErrorNone ? error : kErrorUndefined);
}

extern "C" DisplayError DestroyExtensionInterface(ExtensionInterface* intf) {
    static auto destroy =
            sdm::RealSymbol<sdm::DestroyExtensionInterface>(DESTROY_EXTENSION_INTERFACE_NAME);
    auto* wrapper = static_cast<ExtensionWrapper*>(intf);
    DisplayError error = destroy ? destroy(wrapper->real()) : kErrorUndefined;
    delete wrapper;
    return error;
}
