#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/Account/Account.h"
#include "config/CemuConfig.h"
#include "config/ActiveSettings.h"
#include "WindowSystem.h"

#include <imgui.h>
#include "resource/IconsFontAwesome5.h"
#include "imgui/imgui_extension.h"

#include "input/InputManager.h"
#include "util/SystemInfo/SystemInfo.h"

#include <cinttypes>
#if BOOST_OS_IOS
#include <cstdarg>
#include <cstdio>
#endif

static bool OverlayHasGpuTime()
{
	return g_renderer && g_renderer->GetType() == RendererAPI::Metal;
}

struct OverlayStats
{
	OverlayStats() {};

	int processor_count = 1;
	ProcessorTime processor_time_cemu;
	std::vector<ProcessorTime> processor_times;

	double fps{};
	uint32 draw_calls_per_frame{};
	uint32 fast_draw_calls_per_frame{};
	float cpu_usage{};
	std::vector<float> cpu_per_core;
	uint32 ram_usage{};

	int vramUsage{}, vramTotal{};
} g_state{};

#if BOOST_OS_IOS
std::atomic_bool g_overlayNativeOnIOS{ false };
static std::mutex g_overlayStateMutex;
#endif

extern std::atomic_int g_compiled_shaders_total;
extern std::atomic_int g_compiled_shaders_async;

std::atomic_int g_compiling_pipelines;
std::atomic_int g_compiling_pipelines_async;
std::atomic_uint64_t g_compiling_pipelines_syncTimeSum;

extern std::mutex g_friend_notification_mutex;
extern std::vector< std::pair<std::string, int> > g_friend_notifications;

std::mutex g_notification_mutex;
std::vector< std::pair<std::string, int> > g_notifications;

void LatteOverlay_pushNotification(const std::string& text, sint32 duration)
{
	std::unique_lock lock(g_notification_mutex);
	g_notifications.emplace_back(text, duration);
}

struct OverlayList
{
	std::wstring text;
	float pos_x = 0;
	float pos_y = 0;
	float width;

	OverlayList(std::wstring text, float width)
		: text(std::move(text)), width(width) {}
};

const auto kPopupFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

const float kBackgroundAlpha = 0.65f;
void LatteOverlay_renderOverlay(ImVec2& position, ImVec2& pivot, sint32 direction, float fontSize, bool pad)
{
	auto& config = GetConfig();

	const auto font = ImGui_GetFont(fontSize);
	ImGui::PushFont(font);

	const ImVec4 color = ImGui::ColorConvertU32ToFloat4(config.overlay.text_color);
	ImGui::PushStyleColor(ImGuiCol_Text, color);
	if (config.overlay.fps || config.overlay.drawcalls || config.overlay.cpu_usage || config.overlay.cpu_per_core_usage || config.overlay.ram_usage || config.overlay.vram_usage || config.overlay.debug)
	{
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
		ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
		if (ImGui::Begin("Stats overlay", nullptr, kPopupFlags))
		{
			if (config.overlay.fps)
			{
				ImGui::Text("FPS: %.2lf", g_state.fps);
				const auto& perf = PerfTelemetry::GetSummary();
				if (perf.valid.load(std::memory_order_relaxed))
				{
					ImGui::Text("Host %.1f / game %.1f fps", perf.hostFps.load(std::memory_order_relaxed), perf.guestFps.load(std::memory_order_relaxed));
					ImGui::Text("PPC %.0f%%  GPU thread %.0f%%", perf.ppcExecPct.load(std::memory_order_relaxed), perf.gpuThreadBusyPct.load(std::memory_order_relaxed));
					if (OverlayHasGpuTime())
						ImGui::Text("GPU %.1f ms/f (%.0f%%)  Limit: %s", perf.mtlGpuMsPerFrame.load(std::memory_order_relaxed), perf.mtlGpuBusyPct.load(std::memory_order_relaxed),
							PerfTelemetry::BottleneckName(perf.bottleneck.load(std::memory_order_relaxed)));
					else
						ImGui::Text("Limit: %s", PerfTelemetry::BottleneckName(perf.bottleneck.load(std::memory_order_relaxed)));
				}
			}

			if (config.overlay.drawcalls)
				ImGui::Text("Draws/f: %d (fast: %d)", g_state.draw_calls_per_frame, g_state.fast_draw_calls_per_frame);

			if (config.overlay.cpu_usage)
				ImGui::Text("CPU: %.2lf%%", g_state.cpu_usage);

			if (config.overlay.cpu_per_core_usage)
			{
				const size_t cpuCoreCount = std::min<size_t>(g_state.processor_count, g_state.cpu_per_core.size());
				for (size_t i = 0; i < cpuCoreCount; ++i)
				{
					ImGui::Text("CPU #%zu: %.2lf%%", i + 1, g_state.cpu_per_core[i]);
				}
			}

			if (config.overlay.ram_usage)
				ImGui::Text("RAM: %dMB", g_state.ram_usage);

			if(config.overlay.vram_usage && g_state.vramUsage != -1 && g_state.vramTotal != -1)
				ImGui::Text("VRAM: %dMB / %dMB", g_state.vramUsage, g_state.vramTotal);

			if (config.overlay.debug)
			{
				ImGui::Text("--- Debug info ---");
				ImGui::Text("IndexUploadPerFrame: %dKB", (performanceMonitor.stats.indexDataUploadPerFrame+1023)/1024);
				g_renderer->AppendOverlayDebugInfo();
			}

			position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
		}
		ImGui::End();
	}

	ImGui::PopStyleColor();
	ImGui::PopFont();
}

void LatteOverlay_RenderNotifications(ImVec2& position, ImVec2& pivot, sint32 direction, float fontSize, bool pad)
{
	auto& config = GetConfig();

	const auto font = ImGui_GetFont(fontSize);
	ImGui::PushFont(font);

	const ImVec4 color = ImGui::ColorConvertU32ToFloat4(config.notification.text_color);
	ImGui::PushStyleColor(ImGuiCol_Text, color);

	if (config.notification.controller_profiles)
	{
		static bool s_init_overlay = false;
		if (!s_init_overlay)
		{
			static std::chrono::steady_clock::time_point s_started = tick_cached();

			const auto now = tick_cached();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_started).count() <= 5000)
			{
				ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
				ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
				if (ImGui::Begin("Active account", nullptr, kPopupFlags))
				{
					ImGui::TextUnformatted((const char*)ICON_FA_USER);
					ImGui::SameLine();

					static std::string s_mii_name;
					if (s_mii_name.empty())
					{
						auto tmp_view = Account::GetAccount(ActiveSettings::GetPersistentId()).GetMiiName();
						std::wstring tmp{ tmp_view };
						s_mii_name = boost::nowide::narrow(tmp);
					}
					ImGui::TextUnformatted(s_mii_name.c_str());

					position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
				}
				ImGui::End();

				std::vector<std::pair<int, std::string>> profiles;
				auto& input_manager = InputManager::instance();
				for (int i = 0; i < InputManager::kMaxController; ++i)
				{
					const auto controller = input_manager.get_controller(i);
					if (!controller)
						continue;

					const auto& profile_name = controller->get_profile_name();
					if (profile_name.empty())
						continue;

					profiles.emplace_back(i, profile_name);
				}

				if (!profiles.empty())
				{
					ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
					ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
					if (ImGui::Begin("Controller profile names", nullptr, kPopupFlags))
					{
						auto it = profiles.cbegin();
						ImGui::TextUnformatted((const char*)ICON_FA_GAMEPAD);
						ImGui::SameLine();
						ImGui::Text("Player %d: %s", it->first + 1, it->second.c_str());

						for (++it; it != profiles.cend(); ++it)
						{
							ImGui::Separator();
							ImGui::TextUnformatted((const char*)ICON_FA_GAMEPAD);
							ImGui::SameLine();
							ImGui::Text("Player %d: %s", it->first + 1, it->second.c_str());
						}

						position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
					}
					ImGui::End();
				}
				else
					s_init_overlay = true;
			}
			else
				s_init_overlay = true;
		}
	}

	if (config.notification.friends)
	{
		static std::vector< std::pair<std::string, std::chrono::steady_clock::time_point> > s_friend_list;

		std::unique_lock lock(g_friend_notification_mutex);
		if (!g_friend_notifications.empty())
		{
			const auto tick = tick_cached();

			for (const auto& entry : g_friend_notifications)
			{
				s_friend_list.emplace_back(entry.first, tick + std::chrono::milliseconds(entry.second));
			}

			g_friend_notifications.clear();
		}

		if (!s_friend_list.empty())
		{
			ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
			ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
			if (ImGui::Begin("Friends overlay", nullptr, kPopupFlags))
			{
				const auto tick = tick_cached();
				for (auto it = s_friend_list.cbegin(); it != s_friend_list.cend();)
				{
					ImGui::TextUnformatted(it->first.c_str(), it->first.c_str() + it->first.size());
					if (tick >= it->second)
						it = s_friend_list.erase(it);
					else
						++it;
				}

				position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
			}
			ImGui::End();
		}
	}

	if (config.notification.controller_battery)
	{
		std::vector<int> batteries;
		auto& input_manager = InputManager::instance();
		for (int i = 0; i < InputManager::kMaxController; ++i)
		{
			const auto controller = input_manager.get_controller(i);
			if (!controller)
				continue;

			if (controller->is_battery_low())
				batteries.emplace_back(i);
		}

		if (!batteries.empty())
		{
			static std::chrono::steady_clock::time_point s_last_tick = tick_cached();
			static bool s_blink_state = false;
			const auto now = tick_cached();

			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_tick).count() >= 750)
			{
				s_blink_state = !s_blink_state;
				s_last_tick = now;
			}

			ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
			ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
			if (ImGui::Begin("Low battery overlay", nullptr, kPopupFlags))
			{
				auto it = batteries.cbegin();
				ImGui::TextUnformatted((const char*)(s_blink_state ? ICON_FA_BATTERY_EMPTY : ICON_FA_BATTERY_QUARTER));
				ImGui::SameLine();
				ImGui::Text("Player %d", *it + 1);

				for (++it; it != batteries.cend(); ++it)
				{
					ImGui::Separator();
					ImGui::TextUnformatted((const char*)(s_blink_state ? ICON_FA_BATTERY_EMPTY : ICON_FA_BATTERY_QUARTER));
					ImGui::SameLine();
					ImGui::Text("Player %d", *it + 1);
				}

				position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
			}
			ImGui::End();
		}
	}

	if (config.notification.shader_compiling)
	{
		static int32_t s_shader_count = 0;
		static int32_t s_shader_count_async = 0;
		if (s_shader_count > 0 || g_compiled_shaders_total > 0)
		{
			const int tmp = g_compiled_shaders_total.exchange(0);
			const int tmpAsync = g_compiled_shaders_async.exchange(0);
			s_shader_count += tmp;
			s_shader_count_async += tmpAsync;

			static std::chrono::steady_clock::time_point s_last_tick = tick_cached();
			const auto now = tick_cached();

			if (tmp > 0)
				s_last_tick = now;

			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_tick).count() >= 2500)
			{
				s_shader_count = 0;
				s_shader_count_async = 0;
			}

			if (s_shader_count > 0)
			{
				ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
				ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
				if (ImGui::Begin("Compiling shaders overlay", nullptr, kPopupFlags))
				{
					ImRotateStart();
					ImGui::TextUnformatted((const char*)ICON_FA_SPINNER);

					const auto ticks = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
					ImRotateEnd(0.001f * ticks.time_since_epoch().count());
					ImGui::SameLine();

					if (s_shader_count_async > 0 && GetConfig().async_compile)
					{
						if(s_shader_count > 1)
							ImGui::Text("Compiled %d new shaders... (%d async)", s_shader_count, s_shader_count_async);
						else
							ImGui::Text("Compiled %d new shader... (%d async)", s_shader_count, s_shader_count_async);
					}
					else
					{
						if (s_shader_count > 1)
							ImGui::Text("Compiled %d new shaders...", s_shader_count);
						else
							ImGui::Text("Compiled %d new shader...", s_shader_count);
					}

					position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
				}
				ImGui::End();
			}
		}

		static int32_t s_pipeline_count = 0;
		static int32_t s_pipeline_count_async = 0;
		if (s_pipeline_count > 0 || g_compiling_pipelines > 0)
		{
			const int tmp = g_compiling_pipelines.exchange(0);
			const int tmpAsync = g_compiling_pipelines_async.exchange(0);
			s_pipeline_count += tmp;
			s_pipeline_count_async += tmpAsync;

			static std::chrono::steady_clock::time_point s_last_tick = tick_cached();
			const auto now = tick_cached();

			if (tmp > 0)
				s_last_tick = now;

			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_tick).count() >= 2500)
			{
				s_pipeline_count = 0;
				s_pipeline_count_async = 0;
			}

			if (s_pipeline_count > 0)
			{
				ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
				ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
				if (ImGui::Begin("Compiling pipeline overlay", nullptr, kPopupFlags))
				{
					ImRotateStart();
					ImGui::TextUnformatted((const char*)ICON_FA_SPINNER);

					const auto ticks = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
					ImRotateEnd(0.001f * ticks.time_since_epoch().count());
					ImGui::SameLine();

#ifdef CEMU_DEBUG_ASSERT
					uint64 totalTime = g_compiling_pipelines_syncTimeSum / 1000000ull;
					if (s_pipeline_count_async > 0)
					{
						if (s_pipeline_count > 1)
							ImGui::Text("Compiled %d new pipelines... (%d async) TotalSync: %" PRIu64 "ms", s_pipeline_count, s_pipeline_count_async, totalTime);
						else
							ImGui::Text("Compiled %d new pipeline... (%d async) TotalSync: %" PRIu64 "ms", s_pipeline_count, s_pipeline_count_async, totalTime);
					}
					else
					{
						if (s_pipeline_count > 1)
							ImGui::Text("Compiled %d new pipelines... TotalSync: %" PRIu64 "ms", s_pipeline_count, totalTime);
						else
							ImGui::Text("Compiled %d new pipeline... TotalSync: %" PRIu64 "ms", s_pipeline_count, totalTime);
					}
#else
					if (s_pipeline_count_async > 0)
					{
						if (s_pipeline_count > 1)
							ImGui::Text("Compiled %d new pipelines... (%d async)", s_pipeline_count, s_pipeline_count_async);
						else
							ImGui::Text("Compiled %d new pipeline... (%d async)", s_pipeline_count, s_pipeline_count_async);
					}
					else
					{
						if (s_pipeline_count > 1)
							ImGui::Text("Compiled %d new pipelines...", s_pipeline_count);
						else
							ImGui::Text("Compiled %d new pipeline...", s_pipeline_count);
					}
#endif
					position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
				}
				ImGui::End();
			}
		}
	}

	static std::vector< std::pair<std::string, std::chrono::steady_clock::time_point> > s_misc_notifications;

	std::unique_lock misc_lock(g_notification_mutex);
	if (!g_notifications.empty())
	{
		const auto tick = tick_cached();

		for (const auto& entry : g_notifications)
		{
			s_misc_notifications.emplace_back(entry.first, tick + std::chrono::milliseconds(entry.second));
		}

		g_notifications.clear();
	}
	misc_lock.unlock();

	if (!s_misc_notifications.empty())
	{
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
		ImGui::SetNextWindowBgAlpha(kBackgroundAlpha);
		if (ImGui::Begin("Misc notifications", nullptr, kPopupFlags))
		{
			const auto tick = tick_cached();
			for (auto it = s_misc_notifications.cbegin(); it != s_misc_notifications.cend();)
			{
				ImGui::TextUnformatted(it->first.c_str(), it->first.c_str() + it->first.size());
				if (tick >= it->second)
					it = s_misc_notifications.erase(it);
				else
					++it;
			}

			position.y += (ImGui::GetWindowSize().y + 10.0f) * direction;
		}
		ImGui::End();
	}
	ImGui::PopStyleColor();
	ImGui::PopFont();
}

void LatteOverlay_translateScreenPosition(ScreenPosition pos, const Vector2f& window_size, ImVec2& position, ImVec2& pivot, sint32& direction)
{
	switch (pos)
	{
	case ScreenPosition::kTopLeft:
		position = { 10, 10 };
		pivot = { 0, 0 };
		direction = 1;
		break;
	case ScreenPosition::kTopCenter:
		position = { window_size.x / 2.0f, 10 };
		pivot = { 0.5f, 0 };
		direction = 1;
		break;
	case ScreenPosition::kTopRight:
		position = { window_size.x - 10, 10 };
		pivot = { 1, 0 };
		direction = 1;
		break;
	case ScreenPosition::kBottomLeft:
		position = { 10, window_size.y - 10 };
		pivot = { 0, 1 };
		direction = -1;
		break;
	case ScreenPosition::kBottomCenter:
		position = { window_size.x / 2.0f, window_size.y - 10 };
		pivot = { 0.5f, 1 };
		direction = -1;
		break;
	case ScreenPosition::kBottomRight:
		position = { window_size.x - 10, window_size.y - 10 };
		pivot = { 1, 1 };
		direction = -1;
		break;
	default:
		UNREACHABLE;
	}
}

void LatteOverlay_render(bool pad_view)
{
#if BOOST_OS_IOS
	if (g_overlayNativeOnIOS.load(std::memory_order_relaxed))
		return;
#endif
	const auto& config = GetConfig();
	if(config.overlay.position == ScreenPosition::kDisabled && config.notification.position == ScreenPosition::kDisabled)
		return;

	sint32 w = 0, h = 0;
	if (pad_view && WindowSystem::IsPadWindowOpen())
		WindowSystem::GetPadWindowPhysSize(w, h);
	else
		WindowSystem::GetWindowPhysSize(w, h);

	if (w == 0 || h == 0)
		return;

	const Vector2f window_size{ (float)w,(float)h };

	float fontDPIScale = !pad_view ? WindowSystem::GetWindowDPIScale() : WindowSystem::GetPadDPIScale();

	float overlayFontSize = 14.0f * (float)config.overlay.text_scale / 100.0f * fontDPIScale;

	if (!ImGui_GetFont(overlayFontSize))
		return;

	float notificationsFontSize = 14.0f * (float)config.notification.text_scale / 100.0f * fontDPIScale;

	if (!ImGui_GetFont(notificationsFontSize))
		return;

	ImVec2 position{}, pivot{};
	sint32 direction{};

	if (config.overlay.position != ScreenPosition::kDisabled)
	{
		LatteOverlay_translateScreenPosition(config.overlay.position, window_size, position, pivot, direction);
		LatteOverlay_renderOverlay(position, pivot, direction, overlayFontSize, pad_view);
	}

	if (config.notification.position != ScreenPosition::kDisabled)
	{
		if(config.overlay.position != config.notification.position)
			LatteOverlay_translateScreenPosition(config.notification.position, window_size, position, pivot, direction);

		LatteOverlay_RenderNotifications(position, pivot, direction, notificationsFontSize, pad_view);
	}
}

void LatteOverlay_init()
{
	g_state.processor_count = std::max<int>(1, GetProcessorCount());

	g_state.processor_times.resize(g_state.processor_count);
	g_state.cpu_per_core.resize(g_state.processor_count);
}

static void UpdateStats_CemuCpu()
{
	ProcessorTime now;
	QueryProcTime(now);

	double cpu = ProcessorTime::Compare(g_state.processor_time_cemu, now);
	cpu /= std::max(1, g_state.processor_count);

	g_state.cpu_usage = cpu * 100;
	g_state.processor_time_cemu = now;
}

static void UpdateStats_CpuPerCore()
{
	std::vector<ProcessorTime> now(g_state.processor_times.size());
	QueryCoreTimes(now);
	if (now.empty())
	{
		g_state.processor_count = 0;
		g_state.processor_times.clear();
		g_state.cpu_per_core.clear();
		return;
	}

	if (g_state.processor_times.size() != now.size())
	{
		g_state.processor_count = (int)now.size();
		g_state.processor_times.resize(now.size());
		g_state.cpu_per_core.resize(now.size());
	}

	for (int32_t i = 0; i < g_state.processor_count; ++i)
	{
		double cpu = ProcessorTime::Compare(g_state.processor_times[i], now[i]);

		g_state.cpu_per_core[i] = cpu * 100;
		g_state.processor_times[i] = now[i];
	}
}

void LatteOverlay_updateStats(double fps, sint32 drawcalls, sint32 fastDrawcalls)
{
	if (GetConfig().overlay.position == ScreenPosition::kDisabled)
		return;

#if BOOST_OS_IOS
	std::lock_guard<std::mutex> stateLock(g_overlayStateMutex);
#endif
	g_state.fps = fps;
	g_state.draw_calls_per_frame = drawcalls;
	g_state.fast_draw_calls_per_frame = fastDrawcalls;

	const auto& overlay = GetConfig().overlay;
	if (overlay.cpu_usage)
		UpdateStats_CemuCpu();
	if (overlay.cpu_per_core_usage)
		UpdateStats_CpuPerCore();

	if (overlay.ram_usage)
		g_state.ram_usage = (QueryRamUsage() / 1000) / 1000;

	if (overlay.vram_usage && g_renderer)
		g_renderer->GetVRAMInfo(g_state.vramUsage, g_state.vramTotal);
}

#if BOOST_OS_IOS

static std::string OverlayNativeFormat(const char* fmt, ...)
{
	char buffer[320];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	return std::string(buffer);
}

static void OverlayNative_CollectStats(std::vector<std::string>& stats)
{
	const auto& config = GetConfig();
	if (config.overlay.position == ScreenPosition::kDisabled)
		return;
	if (!(config.overlay.fps || config.overlay.drawcalls || config.overlay.cpu_usage || config.overlay.cpu_per_core_usage || config.overlay.ram_usage || config.overlay.vram_usage || config.overlay.debug))
		return;

	OverlayStats state;
	{
		std::lock_guard<std::mutex> stateLock(g_overlayStateMutex);
		state = g_state;
	}

	if (config.overlay.fps)
	{
		stats.emplace_back(OverlayNativeFormat("FPS: %.2lf", state.fps));
		const auto& perf = PerfTelemetry::GetSummary();
		if (perf.valid.load(std::memory_order_relaxed))
		{
			stats.emplace_back(OverlayNativeFormat("Host %.1f / game %.1f fps", perf.hostFps.load(std::memory_order_relaxed), perf.guestFps.load(std::memory_order_relaxed)));
			stats.emplace_back(OverlayNativeFormat("PPC %.0f%%  GPU thread %.0f%%", perf.ppcExecPct.load(std::memory_order_relaxed), perf.gpuThreadBusyPct.load(std::memory_order_relaxed)));
			if (OverlayHasGpuTime())
				stats.emplace_back(OverlayNativeFormat("GPU %.1f ms/f (%.0f%%)  Limit: %s", perf.mtlGpuMsPerFrame.load(std::memory_order_relaxed), perf.mtlGpuBusyPct.load(std::memory_order_relaxed),
					PerfTelemetry::BottleneckName(perf.bottleneck.load(std::memory_order_relaxed))));
			else
				stats.emplace_back(OverlayNativeFormat("Limit: %s", PerfTelemetry::BottleneckName(perf.bottleneck.load(std::memory_order_relaxed))));
		}
	}

	if (config.overlay.drawcalls)
		stats.emplace_back(OverlayNativeFormat("Draws/f: %d (fast: %d)", state.draw_calls_per_frame, state.fast_draw_calls_per_frame));

	if (config.overlay.cpu_usage)
		stats.emplace_back(OverlayNativeFormat("CPU: %.2lf%%", state.cpu_usage));

	if (config.overlay.cpu_per_core_usage)
	{
		const size_t cpuCoreCount = std::min<size_t>(state.processor_count, state.cpu_per_core.size());
		for (size_t i = 0; i < cpuCoreCount; ++i)
			stats.emplace_back(OverlayNativeFormat("CPU #%zu: %.2lf%%", i + 1, state.cpu_per_core[i]));
	}

	if (config.overlay.ram_usage)
		stats.emplace_back(OverlayNativeFormat("RAM: %dMB", state.ram_usage));

	if (config.overlay.vram_usage && state.vramUsage != -1 && state.vramTotal != -1)
		stats.emplace_back(OverlayNativeFormat("VRAM: %dMB / %dMB", state.vramUsage, state.vramTotal));

	if (config.overlay.debug)
	{
		stats.emplace_back("--- Debug info ---");
		stats.emplace_back(OverlayNativeFormat("IndexUploadPerFrame: %dKB", (performanceMonitor.stats.indexDataUploadPerFrame + 1023) / 1024));
	}
}

static std::string OverlayNative_JoinLines(const std::vector<std::string>& lines)
{
	std::string joined;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (i != 0)
			joined += '\n';
		joined += lines[i];
	}
	return joined;
}

static void OverlayNative_CollectNotifications(std::vector<std::string>& notifications)
{
	const auto& config = GetConfig();
	if (config.notification.position == ScreenPosition::kDisabled)
		return;

	if (config.notification.controller_profiles)
	{
		static bool s_init_overlay = false;
		if (!s_init_overlay)
		{
			static std::chrono::steady_clock::time_point s_started = tick_cached();

			const auto now = tick_cached();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_started).count() <= 5000)
			{
				static std::string s_mii_name;
				if (s_mii_name.empty())
				{
					auto tmp_view = Account::GetAccount(ActiveSettings::GetPersistentId()).GetMiiName();
					std::wstring tmp{ tmp_view };
					s_mii_name = boost::nowide::narrow(tmp);
				}
				notifications.emplace_back("Account: " + s_mii_name);

				std::vector<std::string> profileLines;
				auto& input_manager = InputManager::instance();
				for (int i = 0; i < InputManager::kMaxController; ++i)
				{
					const auto controller = input_manager.get_controller(i);
					if (!controller)
						continue;

					const auto& profile_name = controller->get_profile_name();
					if (profile_name.empty())
						continue;

					profileLines.emplace_back(OverlayNativeFormat("Player %d: %s", i + 1, profile_name.c_str()));
				}

				if (!profileLines.empty())
					notifications.emplace_back(OverlayNative_JoinLines(profileLines));
				else
					s_init_overlay = true;
			}
			else
				s_init_overlay = true;
		}
	}

	if (config.notification.friends)
	{
		static std::vector< std::pair<std::string, std::chrono::steady_clock::time_point> > s_friend_list;

		std::unique_lock lock(g_friend_notification_mutex);
		if (!g_friend_notifications.empty())
		{
			const auto tick = tick_cached();

			for (const auto& entry : g_friend_notifications)
				s_friend_list.emplace_back(entry.first, tick + std::chrono::milliseconds(entry.second));

			g_friend_notifications.clear();
		}

		if (!s_friend_list.empty())
		{
			std::vector<std::string> lines;
			const auto tick = tick_cached();
			for (auto it = s_friend_list.cbegin(); it != s_friend_list.cend();)
			{
				lines.emplace_back(it->first);
				if (tick >= it->second)
					it = s_friend_list.erase(it);
				else
					++it;
			}
			notifications.emplace_back(OverlayNative_JoinLines(lines));
		}
	}

	if (config.notification.controller_battery)
	{
		std::vector<std::string> lines;
		auto& input_manager = InputManager::instance();
		for (int i = 0; i < InputManager::kMaxController; ++i)
		{
			const auto controller = input_manager.get_controller(i);
			if (!controller)
				continue;

			if (controller->is_battery_low())
				lines.emplace_back(OverlayNativeFormat("Low battery: Player %d", i + 1));
		}

		if (!lines.empty())
			notifications.emplace_back(OverlayNative_JoinLines(lines));
	}

	if (config.notification.shader_compiling)
	{
		static int32_t s_shader_count = 0;
		static int32_t s_shader_count_async = 0;
		if (s_shader_count > 0 || g_compiled_shaders_total > 0)
		{
			const int tmp = g_compiled_shaders_total.exchange(0);
			const int tmpAsync = g_compiled_shaders_async.exchange(0);
			s_shader_count += tmp;
			s_shader_count_async += tmpAsync;

			static std::chrono::steady_clock::time_point s_last_tick = tick_cached();
			const auto now = tick_cached();

			if (tmp > 0)
				s_last_tick = now;

			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_tick).count() >= 2500)
			{
				s_shader_count = 0;
				s_shader_count_async = 0;
			}

			if (s_shader_count > 0)
			{
				if (s_shader_count_async > 0 && GetConfig().async_compile)
				{
					if (s_shader_count > 1)
						notifications.emplace_back(OverlayNativeFormat("Compiled %d new shaders... (%d async)", s_shader_count, s_shader_count_async));
					else
						notifications.emplace_back(OverlayNativeFormat("Compiled %d new shader... (%d async)", s_shader_count, s_shader_count_async));
				}
				else
				{
					if (s_shader_count > 1)
						notifications.emplace_back(OverlayNativeFormat("Compiled %d new shaders...", s_shader_count));
					else
						notifications.emplace_back(OverlayNativeFormat("Compiled %d new shader...", s_shader_count));
				}
			}
		}

		static int32_t s_pipeline_count = 0;
		static int32_t s_pipeline_count_async = 0;
		if (s_pipeline_count > 0 || g_compiling_pipelines > 0)
		{
			const int tmp = g_compiling_pipelines.exchange(0);
			const int tmpAsync = g_compiling_pipelines_async.exchange(0);
			s_pipeline_count += tmp;
			s_pipeline_count_async += tmpAsync;

			static std::chrono::steady_clock::time_point s_last_tick = tick_cached();
			const auto now = tick_cached();

			if (tmp > 0)
				s_last_tick = now;

			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last_tick).count() >= 2500)
			{
				s_pipeline_count = 0;
				s_pipeline_count_async = 0;
			}

			if (s_pipeline_count > 0)
			{
#ifdef CEMU_DEBUG_ASSERT
				uint64 totalTime = g_compiling_pipelines_syncTimeSum / 1000000ull;
				if (s_pipeline_count_async > 0)
					notifications.emplace_back(OverlayNativeFormat("Compiled %d new pipeline%s... (%d async) TotalSync: %" PRIu64 "ms", s_pipeline_count, s_pipeline_count > 1 ? "s" : "", s_pipeline_count_async, totalTime));
				else
					notifications.emplace_back(OverlayNativeFormat("Compiled %d new pipeline%s... TotalSync: %" PRIu64 "ms", s_pipeline_count, s_pipeline_count > 1 ? "s" : "", totalTime));
#else
				if (s_pipeline_count_async > 0)
					notifications.emplace_back(OverlayNativeFormat("Compiled %d new pipeline%s... (%d async)", s_pipeline_count, s_pipeline_count > 1 ? "s" : "", s_pipeline_count_async));
				else
					notifications.emplace_back(OverlayNativeFormat("Compiled %d new pipeline%s...", s_pipeline_count, s_pipeline_count > 1 ? "s" : ""));
#endif
			}
		}
	}

	static std::vector< std::pair<std::string, std::chrono::steady_clock::time_point> > s_misc_notifications;

	std::unique_lock misc_lock(g_notification_mutex);
	if (!g_notifications.empty())
	{
		const auto tick = tick_cached();

		for (const auto& entry : g_notifications)
			s_misc_notifications.emplace_back(entry.first, tick + std::chrono::milliseconds(entry.second));

		g_notifications.clear();
	}
	misc_lock.unlock();

	if (!s_misc_notifications.empty())
	{
		std::vector<std::string> lines;
		const auto tick = tick_cached();
		for (auto it = s_misc_notifications.cbegin(); it != s_misc_notifications.cend();)
		{
			lines.emplace_back(it->first);
			if (tick >= it->second)
				it = s_misc_notifications.erase(it);
			else
				++it;
		}
		notifications.emplace_back(OverlayNative_JoinLines(lines));
	}
}

void LatteOverlay_CollectNativeText(std::vector<std::string>& stats, std::vector<std::string>& notifications)
{
	static std::mutex s_collectMutex;
	std::lock_guard<std::mutex> collectLock(s_collectMutex);

	OverlayNative_CollectStats(stats);
	OverlayNative_CollectNotifications(notifications);
}
#endif
