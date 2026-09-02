#include "output_message.hpp"
#include <core.hpp>
#include <output.hpp>

static flutter::EncodableMap create_output_map(Output *output, Core *instance)
{
    struct wlr_output *wlr_out = output->wlr_output;
    struct wlr_box box;
    wlr_output_layout_get_box(instance->output_layout, wlr_out, &box);

    int eff_width = 0, eff_height = 0;
    wlr_output_effective_resolution(wlr_out, &eff_width, &eff_height);

    flutter::EncodableList modes_list;
    struct wlr_output_mode *mode;
    wl_list_for_each(mode, &wlr_out->modes, link)
    {
        modes_list.push_back(flutter::EncodableValue(flutter::EncodableMap{
            {flutter::EncodableValue("width"), flutter::EncodableValue((int64_t)mode->width)},
            {flutter::EncodableValue("height"), flutter::EncodableValue((int64_t)mode->height)},
            {flutter::EncodableValue("refresh"), flutter::EncodableValue((int64_t)mode->refresh)},
        }));
    }

    return flutter::EncodableMap{
        {flutter::EncodableValue("id"), flutter::EncodableValue((int64_t)output->id)},
        {flutter::EncodableValue("name"), flutter::EncodableValue(wlr_out->name ? wlr_out->name : "")},
        {flutter::EncodableValue("make"), flutter::EncodableValue(wlr_out->make ? wlr_out->make : "")},
        {flutter::EncodableValue("model"), flutter::EncodableValue(wlr_out->model ? wlr_out->model : "")},
        {flutter::EncodableValue("x"), flutter::EncodableValue((int64_t)box.x)},
        {flutter::EncodableValue("y"), flutter::EncodableValue((int64_t)box.y)},
        {flutter::EncodableValue("width"), flutter::EncodableValue((int64_t)eff_width)},
        {flutter::EncodableValue("height"), flutter::EncodableValue((int64_t)eff_height)},
        {flutter::EncodableValue("refresh"), flutter::EncodableValue((int64_t)get_output_refresh(wlr_out))},
        {flutter::EncodableValue("scale"), flutter::EncodableValue(wlr_out->scale)},
        {flutter::EncodableValue("transform"), flutter::EncodableValue((int64_t)wlr_out->transform)},
        {flutter::EncodableValue("modes"), flutter::EncodableValue(modes_list)},
    };
}

void send_output_added(Output *output)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel || !output || !output->wlr_output)
    {
        return;
    }

    auto map = create_output_map(output, instance);
    instance->wlroots_channel->InvokeMethod("output_added",
        std::make_unique<flutter::EncodableValue>(map));

    struct wlr_output *wlr_out = output->wlr_output;
    wlr_log(WLR_INFO, "Sent output_added for %s (id=%d, %dx%d @ %d mHz)",
        wlr_out->name, output->id, wlr_out->width, wlr_out->height,
        get_output_refresh(wlr_out));
}

void sparrow_send_all_outputs()
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel)
    {
        return;
    }

    int count = 0;
    Output *output;
    wl_list_for_each(output, &instance->outputs, link)
    {
        send_output_added(output);
        count++;
    }
    wlr_log(WLR_INFO, "Sent %d existing outputs to Flutter", count);
}

void send_output_removed(uint32_t output_id)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel)
    {
        return;
    }

    auto map = flutter::EncodableMap{
        {flutter::EncodableValue("id"), flutter::EncodableValue((int64_t)output_id)},
    };

    instance->wlroots_channel->InvokeMethod("output_removed",
        std::make_unique<flutter::EncodableValue>(map));

    wlr_log(WLR_INFO, "Sent output_removed for id=%d", output_id);
}

void send_output_changed(Output *output)
{
    Core *instance = Core::instance();
    if (!instance || !instance->wlroots_channel || !output || !output->wlr_output)
    {
        return;
    }

    auto map = create_output_map(output, instance);
    instance->wlroots_channel->InvokeMethod("output_changed",
        std::make_unique<flutter::EncodableValue>(map));

#ifdef DEBUG
    struct wlr_output *wlr_out = output->wlr_output;
    wlr_log(WLR_INFO, "Sent output_changed for %s (id=%d, %dx%d @ %d mHz)",
        wlr_out->name, output->id, wlr_out->width, wlr_out->height,
        get_output_refresh(wlr_out));
#endif
}
