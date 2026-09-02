#include "decoration_message.hpp"
#include <core.hpp>
#include <surface/view.hpp>

void sparrow_send_decoration_update(SparrowView *view)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel || !view)
    {
        return;
    }

    const bool uses_csd = !view->uses_ssd;
    wlr_log(WLR_INFO,
        "Sending decoration update: handle=%d, uses_ssd=%d, uses_csd=%d",
        view->handle, view->uses_ssd, uses_csd);

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("handle"), flutter::EncodableValue((int64_t)view->handle)},
        {flutter::EncodableValue("uses_csd"), flutter::EncodableValue((int64_t)(uses_csd ? 1 : 0))},
    };

    instance->wlroots_channel->InvokeMethod("surface_decoration",
        std::make_unique<flutter::EncodableValue>(map));
}
