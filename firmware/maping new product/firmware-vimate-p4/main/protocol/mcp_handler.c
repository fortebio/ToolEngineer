#include "mcp_handler.h"
#include "vimate.h"
#include "envelope.h"
#include "ui/display.h"
#include "ui/ui_image.h"
#include "ui/ui_emotion.h"
#include "ui/ui_reward.h"
#include "media/video_control.h"
#include "esp_err.h"
#include <string.h>

void mcp_handler_note_listen_after_tts(void) {
}

void mcp_handler_dispatch(const cJSON *payload) {
    if (!cJSON_IsObject(payload)) return;
    const cJSON *method = cJSON_GetObjectItem(payload, "method");
    const cJSON *id = cJSON_GetObjectItem(payload, "id");
    const cJSON *params = cJSON_GetObjectItem(payload, "params");
    if (!cJSON_IsString(method)) return;
    int rpc_id = cJSON_IsNumber(id) ? id->valueint : 0;

    if (strcmp(method->valuestring, "tools/call") != 0) {
        ESP_LOGW(TAG_MCP, "unsupported method %s", method->valuestring);
        return;
    }
    const cJSON *name = cJSON_GetObjectItem(params, "name");
    const cJSON *args = cJSON_GetObjectItem(params, "arguments");
    if (!cJSON_IsString(name)) return;

    ESP_LOGI(TAG_MCP, "tool call: %s", name->valuestring);
    if (strcmp(name->valuestring, "self.edu.show_image") == 0) {
        const cJSON *url = cJSON_GetObjectItem(args, "url");
        if (cJSON_IsString(url)) {
            /* Enqueue vào worker ảnh tạo sẵn lúc boot để không block WS receive. */
            ESP_LOGI(TAG_MCP, "MCP show_image enqueue: %s", url->valuestring);
            ui_emotion_hide();
            if (!ui_image_show_async(url->valuestring)) {
                ESP_LOGE(TAG_MCP, "MCP show_image enqueue failed");
            }
            envelope_send_mcp_result(rpc_id, "{\"shown\":true}");
            return;
        }
    } else if (strcmp(name->valuestring, "self.edu.show_card") == 0) {
        const cJSON *img = cJSON_GetObjectItem(args, "image");
        if (!cJSON_IsString(img)) img = cJSON_GetObjectItem(args, "url");
        ui_emotion_hide();
        const cJSON *title = cJSON_GetObjectItem(args, "title");
        const cJSON *sub = cJSON_GetObjectItem(args, "subtitle");
        display_set_message(cJSON_IsString(title) ? title->valuestring : "",
                            cJSON_IsString(sub) ? sub->valuestring : "");
        if (cJSON_IsString(img) && img->valuestring[0]) {
            ui_image_show_async(img->valuestring);
        }
        envelope_send_mcp_result(rpc_id, "{\"shown\":true}");
        return;
    } else if (strcmp(name->valuestring, "self.core.play_video") == 0 ||
               strcmp(name->valuestring, "self.edu.play_video") == 0) {
        const cJSON *url = cJSON_GetObjectItem(args, "url");
        const cJSON *path = cJSON_GetObjectItem(args, "path");
        const cJSON *title = cJSON_GetObjectItem(args, "title");
        const cJSON *loop = cJSON_GetObjectItem(args, "loop");
        const char *source = cJSON_IsString(path) && path->valuestring[0]
            ? path->valuestring
            : (cJSON_IsString(url) ? url->valuestring : "");
        esp_err_t err = video_control_play(
            source,
            cJSON_IsString(title) ? title->valuestring : "",
            cJSON_IsTrue(loop));
        if (err == ESP_OK) {
            envelope_send_mcp_result(rpc_id, "{\"playing\":true}");
        } else {
            char res[128];
            snprintf(res, sizeof(res),
                     "{\"playing\":false,\"error\":\"%s\"}",
                     esp_err_to_name(err));
            envelope_send_mcp_result(rpc_id, res);
        }
        return;
    } else if (strcmp(name->valuestring, "self.core.stop_video") == 0 ||
               strcmp(name->valuestring, "self.edu.stop_video") == 0) {
        video_control_stop();
        envelope_send_mcp_result(rpc_id, "{\"stopped\":true}");
        return;
    }
    else if (strcmp(name->valuestring, "self.edu.show_reward") == 0) {
        const cJSON *stars = cJSON_GetObjectItem(args, "stars");
        int n = cJSON_IsNumber(stars) ? stars->valueint : 0;
        if (n < 0) n = 0;
        if (n > 5) n = 5;
        ui_reward_show(n);
        envelope_send_mcp_result(rpc_id, "{\"shown\":true}");
        return;
    }

    /* Unknown tool — báo error JSON-RPC */
    char err[160];
    snprintf(err, sizeof(err),
        "{\"shown\":false,\"error\":\"unsupported tool %s\"}",
        name->valuestring);
    envelope_send_mcp_result(rpc_id, err);
}
