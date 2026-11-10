// 集中線のソースの 1 フレームあたりの GPU 時間を、1920x1080 で測るベンチマーク。
// 計測は gs_timer (タイムスタンプのクエリ) で行う。描画先を消去するだけの時間を基準とし、その差を集中線の時間とする。
// 使い方: benchmark <プラグイン> <プラグインの data> <描画モジュール> <libobs の data> <線の本数>...
// 環境で値が変わるため CTest には登録しない。

#include "burst-fixture.hpp"

#include <cstdlib>
#include <iomanip>

namespace {

constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr int kWarmupFrames = 10;
constexpr int kMeasuredFrames = 60;
constexpr int kRepeats = 5;

// 1 フレームあたりの GPU 時間 (ミリ秒) を返す。source が nullptr なら消去だけを測る。失敗時は負の値
double measure(obs_source_t *source)
{
	double result = -1.0;
	obs_enter_graphics();
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_timer_range_t *range = gs_timer_range_create();
	gs_timer_t *timer = gs_timer_create();

	auto render_frame = [&]() {
		gs_texrender_reset(target);
		if (!gs_texrender_begin(target, kWidth, kHeight)) {
			return false;
		}
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(kWidth), 0.0f, static_cast<float>(kHeight), -100.0f, 100.0f);
		if (source) {
			obs_source_video_render(source);
		}
		gs_texrender_end(target);
		return true;
	};

	// OpenGL の libobs は範囲を作らず NULL を返す。範囲のデータの取得は引数を使わずに周波数を返すため、
	// 範囲が無いときは開始と終了だけを省く
	bool ok = target && timer;
	for (int i = 0; ok && i < kWarmupFrames; ++i) {
		ok = render_frame();
	}
	if (ok) {
		if (range) {
			gs_timer_range_begin(range);
		}
		gs_timer_begin(timer);
		for (int i = 0; ok && i < kMeasuredFrames; ++i) {
			ok = render_frame();
		}
		gs_timer_end(timer);
		if (range) {
			gs_timer_range_end(range);
		}
		gs_flush();
	}
	if (ok) {
		bool disjoint = true;
		uint64_t frequency = 0;
		uint64_t ticks = 0;
		if (gs_timer_range_get_data(range, &disjoint, &frequency) && gs_timer_get_data(timer, &ticks) &&
		    !disjoint && frequency > 0) {
			result = static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency) / kMeasuredFrames;
		}
	}
	gs_timer_destroy(timer);
	if (range) {
		gs_timer_range_destroy(range);
	}
	gs_texrender_destroy(target);
	obs_leave_graphics();
	return result;
}

double median_of_repeats(obs_source_t *source)
{
	std::vector<double> samples;
	for (int i = 0; i < kRepeats; ++i) {
		const double ms = measure(source);
		if (ms < 0.0) {
			return -1.0;
		}
		samples.push_back(ms);
	}
	std::sort(samples.begin(), samples.end());
	return samples[samples.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 6) {
		std::cerr
			<< "usage: benchmark <plugin> <plugin data> <graphics module> <libobs data> <line count>...\n";
		return 2;
	}
	if (!obs_test::start(argv[3], argv[4], kWidth, kHeight)) {
		return 2;
	}
	if (!obs_test::load_plugin(argv[1], argv[2])) {
		obs_shutdown();
		return 2;
	}

	int exit_code = 0;
	const double baseline = median_of_repeats(nullptr);
	if (baseline < 0.0) {
		std::cerr << "failed to measure the baseline\n";
		obs_shutdown();
		return 2;
	}

	std::cout << std::fixed << std::setprecision(3);
	std::cout << "line_count,width,height,frame_ms,baseline_ms,burst_ms\n";
	for (int i = 5; i < argc; ++i) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, "line_count", std::atoi(argv[i]));
		obs_source_t *source = burst_test::create_source(settings);
		obs_data_release(settings);
		const double frame = source ? median_of_repeats(source) : -1.0;
		std::cout << argv[i] << ',' << obs_source_get_width(source) << ',' << obs_source_get_height(source)
			  << ',' << frame << ',' << baseline << ',' << (frame - baseline) << '\n';
		if (frame < 0.0) {
			exit_code = 1;
		}
		obs_source_release(source);
	}

	obs_shutdown();
	return exit_code;
}
