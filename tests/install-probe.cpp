// 通常の導入を検証するための、OBS の中で動くモジュール。
// 環境変数 BURST_LINES_INSTALL_RESULT_DIR と BURST_LINES_INSTALL_PHASE があるときだけ読み込まれる。
// - setup: 集中線のソースをシーンに追加し、画素を検査して保存する
// - restore: 再起動後に、保存したソースと設定が残っていることを確かめ、画素を検査する
// 結果を result.json に書き、OBS を閉じる。

#include "burst-fixture.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>

#include <cmath>
#include <cstring>

OBS_DECLARE_MODULE()

namespace {
QString result_dir;
QString phase;
QJsonObject evidence;
constexpr const char *kModuleName = "simple-burst-lines";
constexpr const char *kSourceName = "Burst lines install check";
constexpr const char *kExpectedName = "Simple Burst Lines";
constexpr long long kLineCount = 64;
// OBS の色は 0xAABBGGRR。不透明の赤
constexpr long long kColor = 0xFF0000FF;

// 中心の空白 (既定値: 45%、縦横比 1.78) の内側が透明で、外側に赤い線があることを確かめる。結果は 0 で合格
int check_pixels(obs_source_t *source)
{
	burst_test::Image image;
	if (!burst_test::render(source, image) || image.width == 0 || image.height == 0) {
		return 2;
	}
	const double w = image.width;
	const double h = image.height;
	const double radius = 0.45 * 0.5 * std::sqrt(w * w + h * h) * 0.99;
	const double sx = std::sqrt(1.78);
	size_t visible_inside = 0;
	size_t red_outside = 0;
	for (uint32_t y = 0; y < image.height; ++y) {
		for (uint32_t x = 0; x < image.width; ++x) {
			const uint8_t *p = image.pixel(x, y);
			const double px = (x + 0.5 - w / 2) / sx;
			const double py = (y + 0.5 - h / 2) * sx;
			if (std::sqrt(px * px + py * py) < radius) {
				visible_inside += p[3] > 0 ? 1 : 0;
			} else if (p[3] == 255 && p[0] >= 250 && p[1] <= 5 && p[2] <= 5) {
				++red_outside;
			}
		}
	}
	evidence.insert("output_size", QString("%1x%2").arg(image.width).arg(image.height));
	evidence.insert("visible_inside_empty_center", static_cast<qint64>(visible_inside));
	evidence.insert("red_line_pixels", static_cast<qint64>(red_outside));
	return visible_inside == 0 && red_outside > 0 ? 0 : 1;
}

void finish(bool passed, const QString &reason)
{
	QJsonObject result{{"passed", passed},
			   {"reason", reason},
			   {"obs", obs_get_version_string()},
			   {"phase", phase},
			   {"evidence", evidence}};
	if (auto *module = obs_get_module(kModuleName))
		result.insert("product_binary_path", QString::fromUtf8(obs_get_module_binary_path(module)));
	QFile file(result_dir + "/result.json");
	const auto json = QJsonDocument(result).toJson();
	if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.flush())
		passed = false;
	file.close();
	blog(passed ? LOG_INFO : LOG_ERROR, "BURST_LINES_INSTALL %s phase=%s %s", passed ? "PASS" : "FAIL",
	     phase.toUtf8().constData(), reason.toUtf8().constData());
	auto *window = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	QTimer::singleShot(0, window, [window] { window->close(); });
}

void inspect()
{
	auto *module = obs_get_module(kModuleName);
	if (!module || QString(obs_get_version_string()) != "33.0.1") {
		finish(false, "product module missing or OBS version mismatch");
		return;
	}
	int registrations = 0;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_input_types(i, &id); ++i)
		registrations += std::strcmp(id, burst_test::kSourceId) == 0;
	evidence.insert("source_registrations", registrations);
	if (registrations != 1) {
		finish(false, "source registration count mismatch");
		return;
	}
	char *effect = obs_find_module_file(module, "effects/burst-lines.effect");
	char *locale = obs_find_module_file(module, "locale/en-US.ini");
	const QString translated = QString::fromUtf8(obs_module_get_locale_text(module, "BurstLines"));
	evidence.insert("effect_path", effect ? QString::fromUtf8(effect) : QString());
	evidence.insert("locale_path", locale ? QString::fromUtf8(locale) : QString());
	evidence.insert("source_name", translated);
	const bool resources = effect && locale && translated == kExpectedName;
	bfree(effect);
	bfree(locale);
	if (!resources) {
		finish(false, "installed effect or translated locale unavailable");
		return;
	}
	obs_source_t *source = nullptr;
	if (phase == "setup") {
		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, "line_count", kLineCount);
		obs_data_set_int(settings, "color", kColor);
		source = obs_source_create(burst_test::kSourceId, kSourceName, settings, nullptr);
		obs_data_release(settings);
		auto *scene_source = obs_frontend_get_current_scene();
		auto *scene = scene_source ? obs_scene_from_source(scene_source) : nullptr;
		const bool attached = source && scene && obs_scene_add(scene, source);
		obs_source_release(scene_source);
		if (!attached) {
			obs_source_release(source);
			finish(false, "source or scene attachment failed");
			return;
		}
	} else {
		source = obs_get_source_by_name(kSourceName);
	}
	if (!source || std::strcmp(obs_source_get_id(source), burst_test::kSourceId) != 0) {
		obs_source_release(source);
		finish(false, "persisted source missing");
		return;
	}
	auto *settings = obs_source_get_settings(source);
	const bool retained = obs_data_get_int(settings, "line_count") == kLineCount &&
			      obs_data_get_int(settings, "color") == kColor;
	evidence.insert("source_settings", QJsonDocument::fromJson(obs_data_get_json(settings)).object());
	obs_data_release(settings);
	const int pixels = retained ? check_pixels(source) : -1;
	evidence.insert("pixel_result", pixels);
	obs_source_release(source);
	if (retained && pixels == 0)
		obs_frontend_save();
	finish(retained && pixels == 0, "installed resources, persisted source settings and line pixels checked");
}

void frontend_event(obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		QTimer::singleShot(1000, static_cast<QMainWindow *>(obs_frontend_get_main_window()), inspect);
}
} // namespace

bool obs_module_load()
{
	result_dir = qEnvironmentVariable("BURST_LINES_INSTALL_RESULT_DIR");
	phase = qEnvironmentVariable("BURST_LINES_INSTALL_PHASE");
	if (result_dir.isEmpty() || (phase != "setup" && phase != "restore"))
		return false;
	obs_frontend_add_event_callback(frontend_event, nullptr);
	return true;
}

void obs_module_unload()
{
	obs_frontend_remove_event_callback(frontend_event, nullptr);
}
