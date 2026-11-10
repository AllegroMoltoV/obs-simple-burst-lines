// 集中線のソースの出力を画素で検査する統合テスト。
// 使い方: integration-test <プラグイン> <プラグインの data> <描画モジュール> <libobs の data> <シナリオ>
// 検査に外れると終了コード 1、準備に失敗すると 2 を返す。

#include "burst-fixture.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace {

using burst_test::Image;

constexpr uint32_t kCanvasWidth = 320;
constexpr uint32_t kCanvasHeight = 180;

struct Checker {
	int failures = 0;
	void expect(bool condition, const std::string &message)
	{
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			++failures;
		}
	}
};

// 設定を作り、ソースを作って 1 回描画する
bool render_with(const std::function<void(obs_data_t *)> &configure, Image &image)
{
	obs_data_t *settings = obs_data_create();
	configure(settings);
	obs_source_t *source = burst_test::create_source(settings);
	obs_data_release(settings);
	const bool ok = source && burst_test::render(source, image);
	obs_source_release(source);
	return ok;
}

size_t count_visible(const Image &image)
{
	size_t count = 0;
	for (uint32_t y = 0; y < image.height; ++y) {
		for (uint32_t x = 0; x < image.width; ++x) {
			count += image.alpha(x, y) > 0 ? 1 : 0;
		}
	}
	return count;
}

// シェーダーと同じ式で中心の空白の楕円を求め、その内側 (margin 倍まで) の画素がすべて透明であることを確かめる
void expect_empty_center(Checker &check, const Image &image, double center_x, double center_y, double radius_percent,
			 double aspect, double margin)
{
	const double w = image.width;
	const double h = image.height;
	const double radius = radius_percent / 100.0 * 0.5 * std::sqrt(w * w + h * h) * margin;
	const double sx = std::sqrt(aspect);
	const double sy = 1.0 / sx;
	size_t inside = 0;
	size_t visible_inside = 0;
	for (uint32_t y = 0; y < image.height; ++y) {
		for (uint32_t x = 0; x < image.width; ++x) {
			const double px = (x + 0.5 - center_x / 100.0 * w) / sx;
			const double py = (y + 0.5 - center_y / 100.0 * h) / sy;
			if (std::sqrt(px * px + py * py) < radius) {
				++inside;
				visible_inside += image.alpha(x, y) > 0 ? 1 : 0;
			}
		}
	}
	std::cout << "empty center: pixels inside=" << inside << " visible inside=" << visible_inside << '\n';
	check.expect(inside > 0, "the empty center contains no pixels to check");
	check.expect(visible_inside == 0, "lines are drawn inside the empty center");
}

int scenario_empty_center(Checker &check)
{
	Image image;
	if (!render_with([](obs_data_t *) {}, image)) {
		return 2;
	}
	const size_t visible = count_visible(image);
	std::cout << "visible pixels=" << visible << '\n';
	check.expect(visible > 0, "no lines are drawn with the default settings");
	// 既定値: 中心 50%・50%、空白 45%、縦横比 1.78
	expect_empty_center(check, image, 50.0, 50.0, 45.0, 1.78, 0.99);
	return 0;
}

int scenario_empty_center_shape(Checker &check)
{
	Image image;
	const auto configure = [](obs_data_t *s) {
		obs_data_set_double(s, "center_x", 30.0);
		obs_data_set_double(s, "center_y", 60.0);
		obs_data_set_double(s, "inner_radius", 25.0);
		obs_data_set_double(s, "inner_aspect", 4.0);
		obs_data_set_double(s, "inner_softness", 0.0);
	};
	if (!render_with(configure, image)) {
		return 2;
	}
	check.expect(count_visible(image) > 0, "no lines are drawn with a wide empty center");
	expect_empty_center(check, image, 30.0, 60.0, 25.0, 4.0, 0.99);
	return 0;
}

// 線の画素の色と、最も濃い画素の不透明度を確かめる
void expect_color(Checker &check, const Image &image, int expected_alpha)
{
	int max_alpha = 0;
	int worst_red = 255;
	int worst_other = 0;
	for (uint32_t y = 0; y < image.height; ++y) {
		for (uint32_t x = 0; x < image.width; ++x) {
			const uint8_t *p = image.pixel(x, y);
			if (p[3] == 0) {
				continue;
			}
			max_alpha = std::max(max_alpha, static_cast<int>(p[3]));
			worst_red = std::min(worst_red, static_cast<int>(p[0]));
			worst_other = std::max({worst_other, static_cast<int>(p[1]), static_cast<int>(p[2])});
		}
	}
	std::cout << "max alpha=" << max_alpha << " min red=" << worst_red << " max green/blue=" << worst_other << '\n';
	check.expect(std::abs(max_alpha - expected_alpha) <= 2, "the line opacity does not match the color setting");
	check.expect(worst_red >= 250 && worst_other <= 5, "the line color is not red");
}

int scenario_color(Checker &check)
{
	Image image;
	// OBS の色は 0xAABBGGRR。不透明の赤
	if (!render_with([](obs_data_t *s) { obs_data_set_int(s, "color", 0xFF0000FF); }, image)) {
		return 2;
	}
	expect_color(check, image, 255);
	return 0;
}

int scenario_opacity(Checker &check)
{
	Image image;
	// 不透明度 50% (0x80) の赤
	if (!render_with([](obs_data_t *s) { obs_data_set_int(s, "color", 0x800000FF); }, image)) {
		return 2;
	}
	expect_color(check, image, 0x80);
	return 0;
}

int scenario_settings_update(Checker &check)
{
	// 同じソースの設定を変え、出力が設定に従って変わることを確かめる
	obs_source_t *source = burst_test::create_source(nullptr);
	if (!source) {
		return 2;
	}
	const auto render_after = [source](const std::function<void(obs_data_t *)> &configure, Image &image) {
		obs_data_t *settings = obs_data_create();
		configure(settings);
		obs_source_update(source, settings);
		obs_data_release(settings);
		return burst_test::wait_frames(2) && burst_test::render(source, image);
	};
	// obs_source_update は設定を積み重ねるため、3 回目は線 400 本のまま空白だけを広げる
	Image few, many, wide_gap;
	const bool ok = render_after([](obs_data_t *s) { obs_data_set_int(s, "line_count", 8); }, few) &&
			render_after([](obs_data_t *s) { obs_data_set_int(s, "line_count", 400); }, many) &&
			render_after([](obs_data_t *s) { obs_data_set_double(s, "inner_radius", 80.0); }, wide_gap);
	obs_source_release(source);
	if (!ok) {
		return 2;
	}
	const size_t n_few = count_visible(few);
	const size_t n_many = count_visible(many);
	const size_t n_wide_gap = count_visible(wide_gap);
	std::cout << "visible pixels: 8 lines=" << n_few << " 400 lines=" << n_many
		  << " 400 lines with 80% gap=" << n_wide_gap << '\n';
	check.expect(n_many > n_few, "more lines do not cover more pixels");
	check.expect(n_wide_gap < n_many, "a larger empty center does not reduce the lines");
	expect_empty_center(check, wide_gap, 50.0, 50.0, 80.0, 1.78, 0.99);
	return 0;
}

int scenario_animation(Checker &check)
{
	// 描き替えの頻度が 0 なら時間が経っても出力は変わらず、正なら変わる
	const auto observe = [](double rate, Image &before, Image &after) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_double(settings, "update_rate", rate);
		obs_source_t *source = burst_test::create_source(settings);
		obs_data_release(settings);
		bool ok = source && burst_test::render(source, before);
		// OBS の描画スレッドが video_tick で時間を進める
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		ok = ok && burst_test::render(source, after);
		obs_source_release(source);
		return ok;
	};
	Image still_before, still_after, moving_before, moving_after;
	if (!observe(0.0, still_before, still_after) || !observe(12.0, moving_before, moving_after)) {
		return 2;
	}
	const auto count_differences = [](const Image &a, const Image &b) {
		size_t count = 0;
		for (size_t i = 0; i + 3 < a.rgba.size() && i + 3 < b.rgba.size(); i += 4) {
			count += std::memcmp(&a.rgba[i], &b.rgba[i], 4) != 0 ? 1 : 0;
		}
		return count;
	};
	const bool still_same = still_before.rgba == still_after.rgba;
	const bool moving_same = moving_before.rgba == moving_after.rgba;
	std::cout << "unchanged after 500 ms: rate 0=" << still_same << " rate 12=" << moving_same << '\n';
	std::cout << "rate 0: visible before=" << count_visible(still_before) << " after=" << count_visible(still_after)
		  << " differing pixels=" << count_differences(still_before, still_after) << '\n';
	std::cout << "rate 12: visible before=" << count_visible(moving_before)
		  << " after=" << count_visible(moving_after)
		  << " differing pixels=" << count_differences(moving_before, moving_after) << '\n';
	check.expect(still_same, "the lines change although redraws are disabled");
	check.expect(!moving_same, "the lines do not change at 12 redraws per second");
	return 0;
}

int scenario_custom_size(Checker &check)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "use_canvas_size", false);
	obs_data_set_int(settings, "width", 200);
	obs_data_set_int(settings, "height", 100);
	obs_source_t *source = burst_test::create_source(settings);
	obs_data_release(settings);
	Image image;
	const bool ok = source && burst_test::render(source, image);
	obs_source_release(source);
	if (!ok) {
		return 2;
	}
	std::cout << "size=" << image.width << "x" << image.height << '\n';
	check.expect(image.width == 200 && image.height == 100, "the custom size is not used");
	check.expect(count_visible(image) > 0, "no lines are drawn at the custom size");
	expect_empty_center(check, image, 50.0, 50.0, 45.0, 1.78, 0.99);
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	const std::map<std::string, int (*)(Checker &)> scenarios = {
		{"empty-center", scenario_empty_center},
		{"empty-center-shape", scenario_empty_center_shape},
		{"color", scenario_color},
		{"opacity", scenario_opacity},
		{"settings-update", scenario_settings_update},
		{"animation", scenario_animation},
		{"custom-size", scenario_custom_size},
	};
	if (argc != 6 || scenarios.find(argv[5]) == scenarios.end()) {
		std::cerr
			<< "usage: integration-test <plugin> <plugin data> <graphics module> <libobs data> <scenario>\n";
		return 2;
	}
	if (!obs_test::start(argv[3], argv[4], kCanvasWidth, kCanvasHeight)) {
		return 2;
	}
	int result = 2;
	Checker check;
	if (obs_test::load_plugin(argv[1], argv[2])) {
		result = scenarios.at(argv[5])(check);
	}
	obs_shutdown();
	if (result != 0) {
		return result;
	}
	std::cout << (check.failures == 0 ? "PASS " : "FAIL ") << argv[5] << '\n';
	return check.failures == 0 ? 0 : 1;
}
