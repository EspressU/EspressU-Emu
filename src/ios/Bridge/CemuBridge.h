#ifndef CEMU_BRIDGE_H
#define CEMU_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CEMU_BRIDGE_OK              = 0,
    CEMU_BRIDGE_INVALID_RPX     = 1,
    CEMU_BRIDGE_UNABLE_TO_MOUNT = 2,
    CEMU_BRIDGE_NO_DISC_KEY     = 3,
    CEMU_BRIDGE_NO_TITLE_TIK    = 4,
    CEMU_BRIDGE_UNSUPPORTED     = 5,
    CEMU_BRIDGE_BASE_NOT_FOUND  = 6,
    CEMU_BRIDGE_BAD_TITLE_TMD   = 7,
    CEMU_BRIDGE_BAD_TITLE_TIK   = 8,
    CEMU_BRIDGE_TITLE_KEY_INVALID = 9,
    CEMU_BRIDGE_MISSING_CONTENT = 10,
    CEMU_BRIDGE_CORE_NOT_BUILT  = 100,
    CEMU_BRIDGE_BAD_ARG         = 101,
} CemuBridgeStatus;

bool cemu_bridge_core_available(void);

void cemu_bridge_initialize(const char* mlcPath);

CemuBridgeStatus cemu_bridge_boot_title(const char* path);

CemuBridgeStatus cemu_bridge_boot_title_id(uint64_t titleId);

CemuBridgeStatus cemu_bridge_boot_rpx(const char* rpxPath);

int cemu_bridge_reload_and_count_keys(void);

void cemu_bridge_prepare_keys_before_init(const char* mlcPath);

void cemu_bridge_register_render_surface(void* uiView, int width, int height, double dpiScale);

void cemu_bridge_register_pad_render_surface(void* uiView, int width, int height, double dpiScale);

void cemu_bridge_release_pad_render_surface(void);

bool cemu_bridge_has_pad_render_surface(void);

void cemu_bridge_set_visible_outputs(bool tv, bool pad);

void cemu_bridge_set_output_sources(bool mainShowsGamePad, bool padShowsTV);

bool cemu_bridge_pad_layer_active(void);

void cemu_bridge_ensure_pad_layer(void);

uint32_t cemu_bridge_pad_present_count(void);

void cemu_bridge_set_pad_touch(double x, double y, bool down);

void cemu_bridge_resize_render_surface(int width, int height, double dpiScale, bool mainWindow);

void cemu_bridge_log_line(const char* message);

double cemu_bridge_get_fps(void);

bool cemu_bridge_video_stalled(void);

int cemu_bridge_video_stall_kind(void);

typedef struct {
    bool gx2_init_reached;
    unsigned long long gx2_frame_count;
    double gx2_frames_per_second;
    unsigned long long os_screen_scanouts;
    unsigned int guest_flip_requests;
} CemuBridgeProgress;

void cemu_bridge_get_progress(CemuBridgeProgress* out);

bool cemu_bridge_start_decrypt(const char* srcPath, const char* destPath, bool toWua);

bool cemu_bridge_start_wua_build(const char* sourcePathsNewlineSeparated, const char* destPath);

int cemu_bridge_wua_list_titles(const char* wuaPath, char* outLines, size_t outSize);

typedef struct {
    bool is_running;
    bool completed;
    int result_status;
    unsigned long long bytes_written;
    unsigned int files_written;
} CemuBridgeDecryptProgress;

void cemu_bridge_get_decrypt_progress(CemuBridgeDecryptProgress* out);

void cemu_bridge_cancel_decrypt(void);

bool cemu_bridge_derive_gametdb_id(const char* romPath, char* outGameID, size_t outGameIDSize);

bool cemu_bridge_get_title_name(const char* romPath, char* outName, size_t outNameSize);

bool cemu_bridge_derive_title_id(const char* romPath, uint64_t* outTitleId);

bool cemu_bridge_read_tmd_title_id(const char* tmdPath, uint64_t* outTitleId);

uint64_t cemu_bridge_derive_base_title_id(uint64_t titleId);

int cemu_bridge_get_title_type(uint64_t titleId);

void cemu_bridge_get_mlc_title_path_components(uint64_t titleId, char* outUpperHex, char* outLowerHex);

typedef enum {
    CemuTitleValid = 0,
    CemuTitleBadPathOrInaccessible = 1,
    CemuTitleUnknownFormat = 2,
    CemuTitleNoDiscKey = 3,
    CemuTitleNoTicket = 4,
    CemuTitleMissingXmlFiles = 5,
    CemuTitleBadTitleTmd = 6,
    CemuTitleBadTitleTik = 7,
    CemuTitleKeyInvalid = 8,
    CemuTitleMissingContentFile = 9,
} CemuTitleInvalidReason;

bool cemu_bridge_inspect_title(const char* romPath, uint64_t* outTitleId, uint16_t* outVersion,
    int* outRegion, int* outInvalidReason);

uint64_t cemu_bridge_derive_content_title_id(uint64_t baseTitleId, bool isUpdate);

void cemu_bridge_graphic_packs_refresh(void);

const char* cemu_bridge_graphic_packs_list(void);

void cemu_bridge_graphic_pack_set_enabled(int index, bool enabled);

typedef enum {
    CEMU_BRIDGE_USB_DEVICE_SKYLANDERS = 0,
    CEMU_BRIDGE_USB_DEVICE_INFINITY   = 1,
    CEMU_BRIDGE_USB_DEVICE_DIMENSIONS = 2,
} CemuBridgeUSBDevice;

void cemu_bridge_set_emulate_skylander_portal(bool enabled);
bool cemu_bridge_emulate_skylander_portal(void);
void cemu_bridge_set_emulate_infinity_base(bool enabled);
bool cemu_bridge_emulate_infinity_base(void);
void cemu_bridge_set_emulate_dimensions_toypad(bool enabled);
bool cemu_bridge_emulate_dimensions_toypad(void);

int cemu_bridge_usb_device_slot_count(CemuBridgeUSBDevice device);

const char* cemu_bridge_usb_device_slot_names(CemuBridgeUSBDevice device);

const char* cemu_bridge_usb_device_figure_list(CemuBridgeUSBDevice device, int slot);

const char* cemu_bridge_usb_device_load(CemuBridgeUSBDevice device, int slot, const char* path);

const char* cemu_bridge_usb_device_clear(CemuBridgeUSBDevice device, int slot);

const char* cemu_bridge_usb_device_create(CemuBridgeUSBDevice device, uint32_t figureId, uint16_t variant, const char* path);

const char* cemu_bridge_usb_device_move_dimensions(int fromSlot, int toSlot);

const char* cemu_bridge_touch_amiibo(const char* path);

void cemu_bridge_set_timebase_shift(int shift);

int cemu_bridge_get_timebase_shift(void);

void cemu_bridge_set_timebase_auto_enabled(bool enabled);

bool cemu_bridge_timebase_auto_enabled(void);

bool cemu_bridge_has_txm(void);

int cemu_bridge_cpu_mode(void);
bool cemu_bridge_jit_probe(void);

void cemu_bridge_set_recompiler_enabled(bool enabled);
bool cemu_bridge_recompiler_enabled(void);

void cemu_bridge_set_favour_accuracy(bool enabled);

void cemu_bridge_set_favour_performance(bool enabled);
bool cemu_bridge_favour_performance(void);

void cemu_bridge_set_full_speed_renders(bool enabled, int shaderMode);
bool cemu_bridge_full_speed_renders(void);

typedef enum {
    CEMU_BRIDGE_TUNING_FREE_MEMORY_ON_WARNING = 0,
    CEMU_BRIDGE_TUNING_GPU_FAULT_HISTORY = 1,
    CEMU_BRIDGE_TUNING_METALFX_UPSCALE = 2,
    CEMU_BRIDGE_TUNING_METAL4_OUTPUT = 3,
    // 4 was frame generation, removed
    CEMU_BRIDGE_TUNING_PAD_PRESENT_PACING = 5,
    CEMU_BRIDGE_TUNING_INTERPRETER_BLOCK_LINKING = 6,
    CEMU_BRIDGE_TUNING_PAD_LOW_RESOLUTION = 7,
    CEMU_BRIDGE_TUNING_DIAGNOSTICS_LOG = 8,
    CEMU_BRIDGE_TUNING_JIT_SKIP_OVERLAPS = 9,
} CemuBridgeTuning;

void cemu_bridge_set_tuning(CemuBridgeTuning option, int value);
int cemu_bridge_tuning(CemuBridgeTuning option);

bool cemu_bridge_metal4_output_supported(void);

// The session report (EspressU-Report.txt): settings text from the front end, and the file paths.
void cemu_bridge_report_settings(const char* text);
const char* cemu_bridge_report_path(void);
const char* cemu_bridge_report_previous_path(void);
void cemu_bridge_report_clear(void);

const char* cemu_bridge_gpu_fault_history(void);
void cemu_bridge_clear_gpu_fault_history(void);

double cemu_bridge_device_temperature_celsius(void);

void cemu_bridge_set_thermal_throttle_micros(uint32_t micros);

void cemu_bridge_set_multicore_enabled(bool enabled);

void cemu_bridge_set_cpu_core_mode(int mode);

void cemu_bridge_set_cpu_auto_demoted(bool demoted);

int cemu_bridge_cpu_cores_running(void);
bool cemu_bridge_cpu_auto_picked_multicore(void);

void cemu_bridge_set_draw_breadcrumbs(bool enabled);

const char* cemu_bridge_perf_line(void);

const char* cemu_bridge_device_summary(void);

void cemu_bridge_set_low_power_mode(bool enabled);
bool cemu_bridge_low_power_mode(void);
bool cemu_bridge_favour_accuracy(void);

void cemu_bridge_set_async_shader_compile(bool enabled);
bool cemu_bridge_async_shader_compile(void);

void cemu_bridge_set_vsync_enabled(bool enabled);

bool cemu_bridge_vsync_enabled(void);

void cemu_bridge_set_stretch_to_fill(bool enabled);

void cemu_bridge_set_graphics_api(int api);
int cemu_bridge_graphics_api(void);

const char* cemu_bridge_take_launch_notice(void);

void cemu_bridge_set_upscale_filter(int filter);
void cemu_bridge_set_downscale_filter(int filter);

void cemu_bridge_set_framebuffer_fetch(bool enabled);
bool cemu_bridge_framebuffer_fetch(void);

void cemu_bridge_set_render_upside_down(bool enabled);
bool cemu_bridge_render_upside_down(void);

void cemu_bridge_set_display_gamma(float gamma);
float cemu_bridge_display_gamma(void);

void cemu_bridge_set_override_app_gamma(bool enabled);
bool cemu_bridge_override_app_gamma(void);
void cemu_bridge_set_override_gamma_value(float gamma);
float cemu_bridge_override_gamma_value(void);

void cemu_bridge_set_overlay_position(int position);
int cemu_bridge_overlay_position(void);

void cemu_bridge_set_overlay_fps(bool enabled);
bool cemu_bridge_overlay_fps(void);
void cemu_bridge_set_overlay_cpu_usage(bool enabled);
bool cemu_bridge_overlay_cpu_usage(void);
void cemu_bridge_set_overlay_ram_usage(bool enabled);
bool cemu_bridge_overlay_ram_usage(void);
void cemu_bridge_set_overlay_text_color(uint32_t color);
uint32_t cemu_bridge_overlay_text_color(void);
void cemu_bridge_set_overlay_text_scale(int scale);
int cemu_bridge_overlay_text_scale(void);
void cemu_bridge_set_overlay_cpu_mode(bool enabled);
bool cemu_bridge_overlay_cpu_mode(void);
void cemu_bridge_set_overlay_drawcalls(bool enabled);
bool cemu_bridge_overlay_drawcalls(void);
void cemu_bridge_set_overlay_cpu_per_core_usage(bool enabled);
bool cemu_bridge_overlay_cpu_per_core_usage(void);
void cemu_bridge_set_overlay_vram_usage(bool enabled);
bool cemu_bridge_overlay_vram_usage(void);
void cemu_bridge_set_overlay_debug(bool enabled);
bool cemu_bridge_overlay_debug(void);

void cemu_bridge_set_notification_position(int position);
int cemu_bridge_notification_position(void);
void cemu_bridge_set_notification_text_color(uint32_t color);
uint32_t cemu_bridge_notification_text_color(void);
void cemu_bridge_set_notification_text_scale(int scale);
int cemu_bridge_notification_text_scale(void);
void cemu_bridge_set_notification_controller_profiles(bool enabled);
bool cemu_bridge_notification_controller_profiles(void);
void cemu_bridge_set_notification_controller_battery(bool enabled);
bool cemu_bridge_notification_controller_battery(void);
void cemu_bridge_set_notification_shader_compiling(bool enabled);
bool cemu_bridge_notification_shader_compiling(void);
void cemu_bridge_set_notification_friends(bool enabled);
bool cemu_bridge_notification_friends(void);

void cemu_bridge_set_native_overlay(bool enabled);
bool cemu_bridge_native_overlay(void);

const char* cemu_bridge_native_overlay_text(void);

void cemu_bridge_set_tv_audio_enabled(bool enabled);
bool cemu_bridge_tv_audio_enabled(void);

void cemu_bridge_set_tv_volume(int volume);
int cemu_bridge_tv_volume(void);

void cemu_bridge_set_tv_channels(int channels);
int cemu_bridge_tv_channels(void);

void cemu_bridge_set_pad_audio_enabled(bool enabled);
bool cemu_bridge_pad_audio_enabled(void);

void cemu_bridge_set_pad_volume(int volume);
int cemu_bridge_pad_volume(void);

void cemu_bridge_set_pad_channels(int channels);
int cemu_bridge_pad_channels(void);

void cemu_bridge_set_microphone_enabled(bool enabled);
bool cemu_bridge_microphone_enabled(void);

void cemu_bridge_set_input_volume(int volume);
int cemu_bridge_input_volume(void);

void cemu_bridge_set_mic_blow(bool blowing);
bool cemu_bridge_mic_blow(void);

const char* cemu_bridge_active_moltenvk(void);

long long cemu_bridge_clear_shader_cache(unsigned long long titleId, bool includeLearned);
int cemu_bridge_shader_cache_stats(unsigned long long titleId, long long* outLearnedBytes, long long* outCompiledBytes);

typedef enum {
    CEMU_CACHE_FILE_UNKNOWN  = 0,
    CEMU_CACHE_FILE_SHADERS  = 1,
    CEMU_CACHE_FILE_PIPELINE = 2,
} CemuCacheFileKind;

typedef enum {
    CEMU_CACHE_STATUS_OK            = 0,
    CEMU_CACHE_STATUS_NOT_A_CACHE   = 1,
    CEMU_CACHE_STATUS_OTHER_GAME    = 2,
    CEMU_CACHE_STATUS_OLD_FORMAT    = 3,
    CEMU_CACHE_STATUS_NOTHING_USABLE = 4,
    CEMU_CACHE_STATUS_TITLE_RUNNING = 5,
} CemuCacheStatus;

typedef struct {
    int kind;
    int status;
    unsigned int stamp;
    int entryCount;
    int usableCount;
    int damagedCount;
} CemuCacheFileInfo;

void cemu_bridge_cache_file_inspect(unsigned long long titleId, const char* path, CemuCacheFileInfo* outInfo);

int cemu_bridge_cache_stamp_kind(unsigned long long titleId, unsigned int stamp);

int cemu_bridge_cache_file_import(unsigned long long titleId, const char* path, int kind, int renderer, bool pipelineIsVulkan,
                                  int* outAdded, int* outAlreadyThere, int* outSkipped);

const char* cemu_bridge_cpu_mode_detail(void);

bool cemu_bridge_is_title_running(void);
void cemu_bridge_pause(void);
void cemu_bridge_resume(void);
void cemu_bridge_shutdown_title(void);
void cemu_bridge_shutdown(void);

bool cemu_bridge_clean_start_required(void);

typedef void (*CemuTitleSwitchCallback)(uint64_t titleId);
void cemu_bridge_set_title_switch_callback(CemuTitleSwitchCallback callback);

typedef enum {
    CEMU_BRIDGE_MENU_HOME    = 0,
    CEMU_BRIDGE_MENU_UP      = 1,
    CEMU_BRIDGE_MENU_DOWN    = 2,
    CEMU_BRIDGE_MENU_CONFIRM = 3,
    CEMU_BRIDGE_MENU_BACK    = 4,
} CemuBridgeMenuEvent;

typedef void (*CemuMenuInputCallback)(CemuBridgeMenuEvent event);
void cemu_bridge_set_menu_input_callback(CemuMenuInputCallback callback);

void cemu_bridge_set_menu_capture(bool capture);
const char* cemu_bridge_clean_start_reason(void);

const char* cemu_bridge_status_text(void);

void cemu_bridge_log_checkpoint(const char* message);

const char* cemu_bridge_device_report(void);

bool cemu_bridge_memory_status(unsigned long long* availableBytes, unsigned long long* footprintBytes);

void cemu_bridge_memory_note(const char* tag);

void cemu_bridge_start_memory_watchdog(void);

void cemu_bridge_flush_pipeline_cache(void);

const char* cemu_bridge_crash_log_path(void);

void cemu_bridge_refresh_input_devices(void);

typedef enum {
    CEMU_BRIDGE_BUTTON_NONE    = 0,

    CEMU_BRIDGE_BUTTON_A       = 1,
    CEMU_BRIDGE_BUTTON_B       = 2,
    CEMU_BRIDGE_BUTTON_X       = 3,
    CEMU_BRIDGE_BUTTON_Y       = 4,

    CEMU_BRIDGE_BUTTON_L       = 5,
    CEMU_BRIDGE_BUTTON_R       = 6,
    CEMU_BRIDGE_BUTTON_ZL      = 7,
    CEMU_BRIDGE_BUTTON_ZR      = 8,

    CEMU_BRIDGE_BUTTON_PLUS    = 9,
    CEMU_BRIDGE_BUTTON_MINUS   = 10,

    CEMU_BRIDGE_BUTTON_UP      = 11,
    CEMU_BRIDGE_BUTTON_DOWN    = 12,
    CEMU_BRIDGE_BUTTON_LEFT    = 13,
    CEMU_BRIDGE_BUTTON_RIGHT   = 14,

    CEMU_BRIDGE_BUTTON_STICK_L = 15,
    CEMU_BRIDGE_BUTTON_STICK_R = 16,

    CEMU_BRIDGE_BUTTON_HOME    = 17,

    CEMU_BRIDGE_BUTTON_COUNT   = 18,
} CemuBridgeButton;

void cemu_bridge_set_button_state(CemuBridgeButton button, bool pressed);

void cemu_bridge_release_all_buttons(void);

const char* cemu_bridge_accounts_list(void);

void cemu_bridge_accounts_refresh(void);

bool cemu_bridge_accounts_has_free_slot(void);

uint32_t cemu_bridge_accounts_next_persistent_id(void);

uint32_t cemu_bridge_accounts_min_persistent_id(void);

bool cemu_bridge_accounts_locked(void);

bool cemu_bridge_account_create(uint32_t persistentId, const char* miiName, uint16_t birthYear,
    uint8_t birthMonth, uint8_t birthDay, int gender, const char* email, int country);

bool cemu_bridge_account_delete(uint32_t persistentId);

bool cemu_bridge_account_set_mii_name(uint32_t persistentId, const char* miiName);
bool cemu_bridge_account_set_gender(uint32_t persistentId, int gender);
bool cemu_bridge_account_set_email(uint32_t persistentId, const char* email);
bool cemu_bridge_account_set_country(uint32_t persistentId, int country);
bool cemu_bridge_account_set_birthdate(uint32_t persistentId, uint16_t year, uint8_t month, uint8_t day);

uint32_t cemu_bridge_active_account_persistent_id(void);
void cemu_bridge_set_active_account_persistent_id(uint32_t persistentId);

bool cemu_bridge_account_is_online_valid(uint32_t persistentId);

int cemu_bridge_account_online_error(uint32_t persistentId);

const char* cemu_bridge_countries_list(void);

typedef enum {
    CEMU_BRIDGE_NETWORK_OFFLINE  = 0,
    CEMU_BRIDGE_NETWORK_NINTENDO = 1,
    CEMU_BRIDGE_NETWORK_PRETENDO = 2,
    CEMU_BRIDGE_NETWORK_CUSTOM   = 3,
} CemuBridgeNetworkService;

CemuBridgeNetworkService cemu_bridge_network_service(uint32_t persistentId);
void cemu_bridge_set_network_service(uint32_t persistentId, CemuBridgeNetworkService service);

bool cemu_bridge_custom_network_service_available(void);

bool cemu_bridge_reload_custom_network_service(void);

bool cemu_bridge_network_services_xml_is_valid(const char* path);

typedef enum {
    CEMU_BRIDGE_DEVICE_NETWORK_OFFLINE  = 0,
    CEMU_BRIDGE_DEVICE_NETWORK_WIFI     = 1,
    CEMU_BRIDGE_DEVICE_NETWORK_CELLULAR = 2,
    CEMU_BRIDGE_DEVICE_NETWORK_OTHER    = 3,
} CemuBridgeDeviceNetwork;

void cemu_bridge_set_device_network(CemuBridgeDeviceNetwork kind);

bool cemu_bridge_console_appears_connected(void);

bool cemu_bridge_online_play_enabled(void);

typedef enum {
    CEMU_BRIDGE_STICK_LEFT  = 0,
    CEMU_BRIDGE_STICK_RIGHT = 1,
} CemuBridgeStick;

void cemu_bridge_set_stick_axis(CemuBridgeStick stick, float x, float y);

const char* cemu_bridge_memory_headroom_summary(void);

int cemu_bridge_input_button_mapping_count(void);

const char* cemu_bridge_input_profile_name(void);

bool cemu_bridge_reset_controller_bindings(void);

// GamePad buttons that can be remapped onto a physical controller. The values match VPADController::ButtonId.
typedef enum {
    CEMU_BRIDGE_PAD_A = 1, CEMU_BRIDGE_PAD_B = 2, CEMU_BRIDGE_PAD_X = 3, CEMU_BRIDGE_PAD_Y = 4,
    CEMU_BRIDGE_PAD_L = 5, CEMU_BRIDGE_PAD_R = 6, CEMU_BRIDGE_PAD_ZL = 7, CEMU_BRIDGE_PAD_ZR = 8,
    CEMU_BRIDGE_PAD_PLUS = 9, CEMU_BRIDGE_PAD_MINUS = 10,
    CEMU_BRIDGE_PAD_UP = 11, CEMU_BRIDGE_PAD_DOWN = 12, CEMU_BRIDGE_PAD_LEFT = 13, CEMU_BRIDGE_PAD_RIGHT = 14,
    CEMU_BRIDGE_PAD_STICK_L = 15, CEMU_BRIDGE_PAD_STICK_R = 16,
} CemuBridgePadMapButton;

// Physical buttons are numbered 0 A, 1 B, 2 X, 3 Y, 4 left bumper, 5 right bumper, 6 left trigger, 7 right trigger,
// 8 options, 9 menu, 10 and 11 stick clicks, 12 to 15 D-pad up, down, left, right. A negative deviceButton puts the default back.
void cemu_bridge_pad_mapping_set(int padButton, int deviceButton);
void cemu_bridge_pad_mapping_clear_all(void);
int cemu_bridge_pad_mapping_default(int padButton);

#ifdef __cplusplus
}
#endif

#endif
