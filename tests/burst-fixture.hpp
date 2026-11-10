#pragma once

// 統合テストとベンチマークで共通に使う、集中線のソースの作成と描画の読み出し。

#include "obs-test-env.hpp"

#include <obs.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

namespace burst_test {

constexpr const char *kSourceId = "simple_burst_lines_source";

struct Image {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgba;

	const uint8_t *pixel(uint32_t x, uint32_t y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
	uint8_t alpha(uint32_t x, uint32_t y) const { return pixel(x, y)[3]; }
};

// 設定を渡してソースを作る。settings は呼び出し側で解放する
inline obs_source_t *create_source(obs_data_t *settings)
{
	return obs_source_create(kSourceId, "burst", settings, nullptr);
}

// OBS の描画スレッドがフレームを count 個進めるまで待つ。映像のソースへの obs_source_update は、
// 次のフレームの video_tick まで反映されないため、設定を変えたあとはこれで待ってから描画する
inline bool wait_frames(uint64_t count)
{
	const uint64_t target = obs_get_total_frames() + count;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (obs_get_total_frames() < target) {
		if (std::chrono::steady_clock::now() > deadline) {
			std::cerr << "OBS graphics thread did not advance within five seconds\n";
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return true;
}

// ソースの出力を、ソースの大きさのまま読み出す。合成せずに出力をそのまま書き込む
inline bool render(obs_source_t *source, Image &image)
{
	bool ok = false;
	obs_enter_graphics();
	image.width = obs_source_get_width(source);
	image.height = obs_source_get_height(source);
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(image.width, image.height, GS_RGBA);
	if (output && staging && gs_texrender_begin(output, image.width, image.height)) {
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(image.width), 0.0f, static_cast<float>(image.height), -100.0f,
			 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(output);
		gs_stage_texture(staging, gs_texrender_get_texture(output));
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (gs_stagesurface_map(staging, &mapped, &stride)) {
			image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
			for (uint32_t y = 0; y < image.height; ++y) {
				std::copy(mapped + static_cast<size_t>(y) * stride,
					  mapped + static_cast<size_t>(y) * stride + image.width * 4,
					  image.rgba.begin() + static_cast<size_t>(y) * image.width * 4);
			}
			gs_stagesurface_unmap(staging);
			ok = true;
		}
	}
	if (!ok) {
		std::cerr << "failed to render the source\n";
	}
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(output);
	obs_leave_graphics();
	return ok;
}

} // namespace burst_test
