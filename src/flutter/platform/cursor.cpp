#include "cursor.hpp"
#include "client_wrapper/encodable_value.h"
#include "client_wrapper/method_channel.h"
#include "client_wrapper/standard_method_codec.h"
#include "core.hpp"
#include "input/seat.hpp"
#include "output.hpp"
#include <sparrow/nonstd/wlroots-full.hpp>

static const char *flutter_cursor_to_xcursor(const char *flutter_kind)
{
    if (flutter_kind == nullptr)
    {
        return "left_ptr";
    }

    if (strcmp(flutter_kind, "basic") == 0)
    {
        return "left_ptr";
    }

    if (strcmp(flutter_kind, "none") == 0)
    {
        return nullptr;
    }

    if (strcmp(flutter_kind, "click") == 0)
    {
        return "pointer";
    }

    if (strcmp(flutter_kind, "pointer") == 0)
    {
        return "pointer";
    }

    if (strcmp(flutter_kind, "text") == 0)
    {
        return "xterm";
    }

    if (strcmp(flutter_kind, "verticalText") == 0)
    {
        return "vertical-text";
    }

    if (strcmp(flutter_kind, "resizeLeft") == 0)
    {
        return "left_side";
    }

    if (strcmp(flutter_kind, "resizeRight") == 0)
    {
        return "right_side";
    }

    if (strcmp(flutter_kind, "resizeUp") == 0)
    {
        return "top_side";
    }

    if (strcmp(flutter_kind, "resizeDown") == 0)
    {
        return "bottom_side";
    }

    if (strcmp(flutter_kind, "resizeUpLeft") == 0)
    {
        return "top_left_corner";
    }

    if (strcmp(flutter_kind, "resizeUpRight") == 0)
    {
        return "top_right_corner";
    }

    if (strcmp(flutter_kind, "resizeDownLeft") == 0)
    {
        return "bottom_left_corner";
    }

    if (strcmp(flutter_kind, "resizeDownRight") == 0)
    {
        return "bottom_right_corner";
    }

    if (strcmp(flutter_kind, "resizeColumn") == 0)
    {
        return "col-resize";
    }

    if (strcmp(flutter_kind, "resizeRow") == 0)
    {
        return "row-resize";
    }

    if (strcmp(flutter_kind, "resizeUpDown") == 0)
    {
        return "ns-resize";
    }

    if (strcmp(flutter_kind, "resizeLeftRight") == 0)
    {
        return "ew-resize";
    }

    if (strcmp(flutter_kind, "resizeUpLeftDownRight") == 0)
    {
        return "nwse-resize";
    }

    if (strcmp(flutter_kind, "resizeUpRightDownLeft") == 0)
    {
        return "nesw-resize";
    }

    if (strcmp(flutter_kind, "grab") == 0)
    {
        return "grab";
    }

    if (strcmp(flutter_kind, "grabbing") == 0)
    {
        return "grabbing";
    }

    if (strcmp(flutter_kind, "move") == 0)
    {
        return "fleur";
    }

    if (strcmp(flutter_kind, "allScroll") == 0)
    {
        return "all-scroll";
    }

    if (strcmp(flutter_kind, "forbidden") == 0)
    {
        return "not-allowed";
    }

    if (strcmp(flutter_kind, "noDrop") == 0)
    {
        return "no-drop";
    }

    if (strcmp(flutter_kind, "wait") == 0)
    {
        return "wait";
    }

    if (strcmp(flutter_kind, "progress") == 0)
    {
        return "progress";
    }

    if (strcmp(flutter_kind, "contextMenu") == 0)
    {
        return "context-menu";
    }

    if (strcmp(flutter_kind, "help") == 0)
    {
        return "help";
    }

    if (strcmp(flutter_kind, "cell") == 0)
    {
        return "cell";
    }

    if (strcmp(flutter_kind, "precise") == 0)
    {
        return "crosshair";
    }

    if (strcmp(flutter_kind, "crosshair") == 0)
    {
        return "crosshair";
    }

    if (strcmp(flutter_kind, "copy") == 0)
    {
        return "copy";
    }

    if (strcmp(flutter_kind, "alias") == 0)
    {
        return "alias";
    }

    if (strcmp(flutter_kind, "zoomIn") == 0)
    {
        return "zoom-in";
    }

    if (strcmp(flutter_kind, "zoomOut") == 0)
    {
        return "zoom-out";
    }

    wlr_log(WLR_DEBUG, "Unknown cursor kind: %s, using default", flutter_kind);
    return "left_ptr";
}

void sparrow_cursor_reset_to_flutter()
{
    Core *instance = Core::instance();

    if (instance == nullptr)
    {
        return;
    }

    // Clear client cursor surface when returning to Flutter/desktop
    if ((instance->client_cursor_destroy.link.next != nullptr) &&
        (instance->client_cursor_destroy.link.prev != nullptr))
    {
        wl_list_remove(&instance->client_cursor_destroy.link);
        wl_list_init(&instance->client_cursor_destroy.link);
    }

    instance->client_cursor_surface = nullptr;

    if (!instance->cursor_visible)
    {
        if (instance->cursor)
        {
            wlr_cursor_unset_image(instance->cursor);
        }
    } else
    {
        const char *target = instance->flutter_cursor_name.empty() ?
            "left_ptr" :
            instance->flutter_cursor_name.c_str();
        instance->current_xcursor_name = target;
        if (instance->cursor && instance->cursor_mgr)
        {
            wlr_cursor_set_xcursor(instance->cursor, instance->cursor_mgr, target);
        }
    }

    // Schedule a frame to update the cursor on the screen immediately
    Output *output = nullptr;
    wl_list_for_each(output, &instance->outputs, link)
    {
        if (output && output->wlr_output && output->wlr_output->enabled &&
            output->wlr_output->needs_frame)
        {
            wlr_output_schedule_frame(output->wlr_output);
        }
    }
}

static std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> g_cursor_channel;

void sparrow_cursor_init()
{
    Core *instance = Core::instance();

    g_cursor_channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        &instance->messenger, "flutter/mousecursor",
        &flutter::StandardMethodCodec::GetInstance());

    g_cursor_channel->SetMethodCallHandler(
        [] (const flutter::MethodCall<flutter::EncodableValue>& call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result)
    {
        Core *instance = Core::instance();
        const std::string& method = call.method_name();

        if (method == "activateSystemCursor")
        {
            const auto *args = std::get_if<flutter::EncodableMap>(call.arguments());
            if (args)
            {
                auto kind_it = args->find(flutter::EncodableValue("kind"));
                if ((kind_it != args->end()) &&
                    std::holds_alternative<std::string>(kind_it->second))
                {
                    const std::string& flutter_kind =
                        std::get<std::string>(kind_it->second);

                    if (flutter_kind == "none")
                    {
                        instance->cursor_visible = false;
                        if (instance->cursor)
                        {
                            wlr_cursor_unset_image(instance->cursor);
                        }

                        Output *output = nullptr;
                        wl_list_for_each(output, &instance->outputs, link)
                        {
                            if (output && output->wlr_output &&
                                output->wlr_output->enabled &&
                                output->wlr_output->needs_frame)
                            {
                                wlr_output_schedule_frame(output->wlr_output);
                            }
                        }
                    } else
                    {
                        instance->cursor_visible = true;
                        const char *xcursor_name =
                            flutter_cursor_to_xcursor(flutter_kind.c_str());

                        if (xcursor_name != nullptr)
                        {
                            wlr_log(WLR_DEBUG,
                                "Setting cursor: flutter=%s xcursor=%s",
                                flutter_kind.c_str(), xcursor_name);

                            instance->flutter_cursor_name = std::string(xcursor_name);

                            // If no Wayland client currently holds pointer focus, update to
                            // Flutter cursor
                            if ((instance->seat == nullptr) ||
                                (instance->seat->pointer_state.focused_client == nullptr))
                            {
                                sparrow_cursor_reset_to_flutter();
                            } else
                            {
                                // A Wayland client (e.g. terminal) has pointer focus: restore
                                // client cursor!
                                if ((instance->client_cursor_surface != nullptr) &&
                                    instance->client_cursor_surface->mapped)
                                {
                                    wlr_cursor_set_surface(instance->cursor,
                                        instance->client_cursor_surface,
                                        instance->client_cursor_hotspot_x,
                                        instance->client_cursor_hotspot_y);
                                } else if (!instance->current_xcursor_name.empty())
                                {
                                    wlr_cursor_set_xcursor(instance->cursor,
                                        instance->cursor_mgr,
                                        instance->current_xcursor_name.c_str());
                                }

                                Output *output = nullptr;
                                wl_list_for_each(output, &instance->outputs, link)
                                {
                                    if (output && output->wlr_output &&
                                        output->wlr_output->enabled &&
                                        output->wlr_output->needs_frame)
                                    {
                                        wlr_output_schedule_frame(output->wlr_output);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            result->Success();
        } else if ((method == "createSystemCursor") ||
                   (method == "deleteSystemCursor"))
        {
            result->Success();
        } else
        {
            wlr_log(WLR_INFO, "Unhandled mousecursor method: %s",
                method.c_str());
            result->NotImplemented();
        }
    });

    wlr_log(WLR_INFO, "Mouse cursor plugin initialized (modern flutter::MethodChannel)");
}
