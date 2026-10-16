#include "decoration_message.hpp"
#include <core.hpp>
#include <surface/view.hpp>

void sparrow_send_decoration_update(SparrowView *view)
{
    Core *instance = Core::instance();
    if (!instance || !instance->pigeon_flutter_api || !view)
    {
        return;
    }

    const bool uses_csd = !view->uses_ssd;
    wlr_log(WLR_INFO,
        "Sending decoration update: handle=%d, uses_ssd=%d, uses_csd=%d",
        view->handle, view->uses_ssd, uses_csd);

    instance->pigeon_flutter_api->SurfaceDecoration(
        view->handle,
        view->uses_ssd,
        uses_csd,
        [] () {},
        [] (const sparrow::FlutterError & err)
    {
        wlr_log(WLR_ERROR, "surface_decoration error: %s", err.message().c_str());
    });
}
