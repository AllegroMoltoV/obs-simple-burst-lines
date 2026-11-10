// 集中線のソースを描画し、エフェクトが読み込めて線が描かれることを確かめる。
// 使い方: render-smoke-test <プラグインの DLL> <プラグインの data> <描画モジュール> <libobs の data>

#include "obs-test-env.hpp"

#include <obs.h>

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kWidth = 320;
constexpr uint32_t kHeight = 180;

struct Image {
	std::vector<uint8_t> rgba;
	uint8_t alpha(uint32_t x, uint32_t y) const { return rgba[(static_cast<size_t>(y) * kWidth + x) * 4 + 3]; }
};

bool render(obs_source_t *source, Image &image)
{
	bool ok = false;
	obs_enter_graphics();
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(kWidth, kHeight, GS_RGBA);
	if (output && staging && gs_texrender_begin(output, kWidth, kHeight)) {
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(kWidth), 0.0f, static_cast<float>(kHeight), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(output);
		gs_stage_texture(staging, gs_texrender_get_texture(output));
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (gs_stagesurface_map(staging, &mapped, &stride)) {
			image.rgba.resize(static_cast<size_t>(kWidth) * kHeight * 4);
			for (uint32_t y = 0; y < kHeight; ++y) {
				std::copy(mapped + static_cast<size_t>(y) * stride,
					  mapped + static_cast<size_t>(y) * stride + kWidth * 4,
					  image.rgba.begin() + static_cast<size_t>(y) * kWidth * 4);
			}
			gs_stagesurface_unmap(staging);
			ok = true;
		}
	}
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(output);
	obs_leave_graphics();
	return ok;
}

size_t count_opaque(const Image &image)
{
	size_t count = 0;
	for (uint32_t y = 0; y < kHeight; ++y) {
		for (uint32_t x = 0; x < kWidth; ++x) {
			count += image.alpha(x, y) == 255 ? 1 : 0;
		}
	}
	return count;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 5) {
		std::cerr << "usage: render-smoke-test <plugin> <plugin data> <graphics module> <libobs data>\n";
		return 2;
	}
	if (!obs_test::start(argv[3], argv[4], kWidth, kHeight)) {
		return 1;
	}

	int failures = 0;
	if (obs_test::load_plugin(argv[1], argv[2])) {
		obs_source_t *source = obs_source_create("simple_burst_lines_source", "burst", nullptr, nullptr);
		Image image;
		if (!source || !render(source, image)) {
			std::cerr << "FAIL: could not create or render the source\n";
			++failures;
		} else {
			// 中心の空白の中は透明で、外側には不透明な線がある
			const uint8_t center_alpha = image.alpha(kWidth / 2, kHeight / 2);
			const size_t opaque = count_opaque(image);
			std::cout << "center alpha=" << static_cast<int>(center_alpha) << " opaque pixels=" << opaque
				  << "\n";
			if (center_alpha != 0) {
				std::cerr << "FAIL: the empty center is not transparent\n";
				++failures;
			}
			if (opaque == 0) {
				std::cerr << "FAIL: no lines were drawn\n";
				++failures;
			}
		}
		obs_source_release(source);
	} else {
		++failures;
	}

	obs_shutdown();
	std::cout << (failures == 0 ? "PASS" : "FAIL") << "\n";
	return failures == 0 ? 0 : 1;
}
