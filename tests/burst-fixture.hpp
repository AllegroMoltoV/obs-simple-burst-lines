#pragma once

// 統合テストとベンチマークで共通に使う、集中線のソースの作成と描画の読み出し。

#include "obs-test-env.hpp"

#include <obs.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
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

// 24 ビットの BMP を書き出す。pixel(x, y) は上から数えた位置の RGB を返す
template<typename PixelFn>
inline bool write_bmp(const std::string &path, uint32_t width, uint32_t height, PixelFn pixel)
{
	const uint32_t row_size = (width * 3 + 3) & ~3u;
	const uint32_t data_size = row_size * height;
	uint8_t header[54] = {'B', 'M'};
	const auto put32 = [&header](size_t offset, uint32_t value) {
		for (int i = 0; i < 4; ++i) {
			header[offset + i] = static_cast<uint8_t>(value >> (8 * i));
		}
	};
	put32(2, 54 + data_size);
	put32(10, 54);
	put32(14, 40);
	put32(18, width);
	put32(22, height);
	header[26] = 1;
	header[28] = 24;
	put32(34, data_size);
	std::ofstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	file.write(reinterpret_cast<const char *>(header), sizeof(header));
	std::vector<uint8_t> row(row_size, 0);
	// BMP は下の行から並べる。各画素は B・G・R の順
	for (uint32_t y = height; y-- > 0;) {
		for (uint32_t x = 0; x < width; ++x) {
			const auto rgb = pixel(x, y);
			row[x * 3 + 0] = rgb[2];
			row[x * 3 + 1] = rgb[1];
			row[x * 3 + 2] = rgb[0];
		}
		file.write(reinterpret_cast<const char *>(row.data()), row.size());
	}
	return static_cast<bool>(file);
}

// 環境変数 BURST_TEST_OUTPUT_DIR があるときだけ、目視の確認のために画像を書き出す。無ければ何もしない
inline std::string output_dir()
{
	const char *dir = std::getenv("BURST_TEST_OUTPUT_DIR");
	return dir ? std::string(dir) : std::string();
}

// 出力を白い背景に合成して書き出す
inline void save_image(const std::string &name, const Image &image)
{
	const std::string dir = output_dir();
	if (dir.empty()) {
		return;
	}
	const std::string path = dir + "/" + name + ".bmp";
	const bool ok = write_bmp(path, image.width, image.height, [&image](uint32_t x, uint32_t y) {
		const uint8_t *p = image.pixel(x, y);
		std::array<uint8_t, 3> rgb;
		for (int c = 0; c < 3; ++c) {
			rgb[c] = static_cast<uint8_t>((p[c] * p[3] + 255 * (255 - p[3])) / 255);
		}
		return rgb;
	});
	std::cout << (ok ? "saved " : "failed to save ") << path << '\n';
}

// 2 枚の出力を比べ、違う画素を赤、両方で線のある画素を灰、どちらも透明な画素を白で書き出す
inline void save_difference(const std::string &name, const Image &a, const Image &b)
{
	const std::string dir = output_dir();
	if (dir.empty() || a.width != b.width || a.height != b.height) {
		return;
	}
	const std::string path = dir + "/" + name + ".bmp";
	const bool ok = write_bmp(path, a.width, a.height, [&a, &b](uint32_t x, uint32_t y) {
		const uint8_t *p = a.pixel(x, y);
		const uint8_t *q = b.pixel(x, y);
		if (std::memcmp(p, q, 4) != 0) {
			return std::array<uint8_t, 3>{255, 0, 0};
		}
		return p[3] > 0 ? std::array<uint8_t, 3>{160, 160, 160} : std::array<uint8_t, 3>{255, 255, 255};
	});
	std::cout << (ok ? "saved " : "failed to save ") << path << '\n';
}

} // namespace burst_test
