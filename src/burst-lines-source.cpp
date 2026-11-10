#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <cmath>
#include <new>

namespace {

constexpr const char *kSourceId = "simple_burst_lines_source";

constexpr const char *kCenterX = "center_x";
constexpr const char *kCenterY = "center_y";
constexpr const char *kLineCount = "line_count";
constexpr const char *kThickness = "thickness";
constexpr const char *kInnerRadius = "inner_radius";
constexpr const char *kInnerJitter = "inner_jitter";
constexpr const char *kTaper = "taper";
constexpr const char *kColor = "color";
constexpr const char *kUpdateRate = "update_rate";
constexpr const char *kSeed = "seed";

// 経過時間を巡回させ、段階の値 (最大で経過秒数 × 60) を float で正確に保てる範囲に収める
constexpr double kStepWrap = 100000.0;

struct CanvasSize {
	uint32_t width;
	uint32_t height;
};

CanvasSize canvas_size()
{
	obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		return {ovi.base_width, ovi.base_height};
	}
	return {1920, 1080};
}

class BurstLinesSource final {
public:
	explicit BurstLinesSource(obs_data_t *settings)
	{
		obs_enter_graphics();
		char *path = obs_module_file("effects/burst-lines.effect");
		if (path) {
			effect_ = gs_effect_create_from_file(path, nullptr);
			bfree(path);
		}
		if (effect_) {
			param_size_ = gs_effect_get_param_by_name(effect_, "size");
			param_center_ = gs_effect_get_param_by_name(effect_, "center");
			param_line_count_ = gs_effect_get_param_by_name(effect_, "line_count");
			param_thickness_ = gs_effect_get_param_by_name(effect_, "thickness");
			param_inner_radius_ = gs_effect_get_param_by_name(effect_, "inner_radius");
			param_inner_jitter_ = gs_effect_get_param_by_name(effect_, "inner_jitter");
			param_taper_length_ = gs_effect_get_param_by_name(effect_, "taper_length");
			param_time_step_ = gs_effect_get_param_by_name(effect_, "time_step");
			param_seed_ = gs_effect_get_param_by_name(effect_, "seed");
			param_line_color_ = gs_effect_get_param_by_name(effect_, "line_color");
		} else {
			obs_log(LOG_WARNING, "[burst_lines] Failed to load effect: effects/burst-lines.effect");
		}
		obs_leave_graphics();

		Update(settings);
	}

	~BurstLinesSource()
	{
		obs_enter_graphics();
		gs_effect_destroy(effect_);
		obs_leave_graphics();
	}

	BurstLinesSource(const BurstLinesSource &) = delete;
	BurstLinesSource &operator=(const BurstLinesSource &) = delete;

	void Update(obs_data_t *settings)
	{
		center_x_ = static_cast<float>(obs_data_get_double(settings, kCenterX)) / 100.0f;
		center_y_ = static_cast<float>(obs_data_get_double(settings, kCenterY)) / 100.0f;
		line_count_ = static_cast<float>(obs_data_get_int(settings, kLineCount));
		thickness_ = static_cast<float>(obs_data_get_double(settings, kThickness)) / 100.0f;
		inner_radius_ = static_cast<float>(obs_data_get_double(settings, kInnerRadius)) / 100.0f;
		inner_jitter_ = static_cast<float>(obs_data_get_double(settings, kInnerJitter)) / 100.0f;
		taper_ = static_cast<float>(obs_data_get_double(settings, kTaper)) / 100.0f;
		update_rate_ = obs_data_get_double(settings, kUpdateRate);
		seed_ = static_cast<float>(obs_data_get_int(settings, kSeed));
		const uint32_t color = static_cast<uint32_t>(obs_data_get_int(settings, kColor));
		vec4_from_rgba(&line_color_, color);
		vec4_from_rgba_srgb(&line_color_linear_, color);
	}

	void Tick(float seconds)
	{
		elapsed_ = std::fmod(elapsed_ + static_cast<double>(seconds), kStepWrap);
		time_step_ = update_rate_ > 0.0 ? static_cast<float>(std::floor(elapsed_ * update_rate_)) : 0.0f;
	}

	void Render()
	{
		if (!effect_) {
			return;
		}

		const CanvasSize canvas = canvas_size();
		const float width = static_cast<float>(canvas.width);
		const float height = static_cast<float>(canvas.height);
		// 半径は中心から画面の角までの距離ではなく、画面の対角線の半分を基準にする。中心を動かしても線の長さの見た目を保つため
		const float half_diagonal = 0.5f * std::sqrt(width * width + height * height);

		vec2 size;
		vec2_set(&size, width, height);
		vec2 center;
		vec2_set(&center, center_x_ * width, center_y_ * height);

		gs_effect_set_vec2(param_size_, &size);
		gs_effect_set_vec2(param_center_, &center);
		gs_effect_set_float(param_line_count_, line_count_);
		gs_effect_set_float(param_thickness_, thickness_);
		gs_effect_set_float(param_inner_radius_, inner_radius_ * half_diagonal);
		gs_effect_set_float(param_inner_jitter_, inner_jitter_ * half_diagonal);
		gs_effect_set_float(param_taper_length_, taper_ * half_diagonal);
		gs_effect_set_float(param_time_step_, time_step_);
		gs_effect_set_float(param_seed_, seed_);
		// OBS の色ソースと同じく、半透明か線形の色空間で合成するときは sRGB を線形に直した色で描き、
		// フレームバッファの sRGB 変換を有効にする。合成の方法は呼び出し側の設定に従う
		const bool linear_srgb = gs_get_linear_srgb() || line_color_.w < 1.0f;
		gs_effect_set_vec4(param_line_color_, linear_srgb ? &line_color_linear_ : &line_color_);
		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(linear_srgb);
		while (gs_effect_loop(effect_, "Draw")) {
			gs_draw_sprite(nullptr, 0, canvas.width, canvas.height);
		}
		gs_enable_framebuffer_srgb(previous);
	}

private:
	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *param_size_ = nullptr;
	gs_eparam_t *param_center_ = nullptr;
	gs_eparam_t *param_line_count_ = nullptr;
	gs_eparam_t *param_thickness_ = nullptr;
	gs_eparam_t *param_inner_radius_ = nullptr;
	gs_eparam_t *param_inner_jitter_ = nullptr;
	gs_eparam_t *param_taper_length_ = nullptr;
	gs_eparam_t *param_time_step_ = nullptr;
	gs_eparam_t *param_seed_ = nullptr;
	gs_eparam_t *param_line_color_ = nullptr;

	float center_x_ = 0.5f;
	float center_y_ = 0.5f;
	float line_count_ = 120.0f;
	float thickness_ = 0.3f;
	float inner_radius_ = 0.45f;
	float inner_jitter_ = 0.15f;
	float taper_ = 0.2f;
	double update_rate_ = 12.0;
	float seed_ = 0.0f;
	vec4 line_color_ = {};
	vec4 line_color_linear_ = {};

	double elapsed_ = 0.0;
	float time_step_ = 0.0f;
};

const char *source_get_name(void *)
{
	return obs_module_text("BurstLines");
}

void *source_create(obs_data_t *settings, obs_source_t *)
{
	return new (std::nothrow) BurstLinesSource(settings);
}

void source_destroy(void *data)
{
	delete static_cast<BurstLinesSource *>(data);
}

void source_update(void *data, obs_data_t *settings)
{
	static_cast<BurstLinesSource *>(data)->Update(settings);
}

void source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, kCenterX, 50.0);
	obs_data_set_default_double(settings, kCenterY, 50.0);
	obs_data_set_default_int(settings, kLineCount, 120);
	obs_data_set_default_double(settings, kThickness, 30.0);
	obs_data_set_default_double(settings, kInnerRadius, 45.0);
	obs_data_set_default_double(settings, kInnerJitter, 15.0);
	obs_data_set_default_double(settings, kTaper, 20.0);
	obs_data_set_default_int(settings, kColor, 0xFF000000);
	obs_data_set_default_double(settings, kUpdateRate, 12.0);
	obs_data_set_default_int(settings, kSeed, 0);
}

obs_properties_t *source_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p =
		obs_properties_add_float_slider(props, kCenterX, obs_module_text("CenterX"), 0.0, 100.0, 0.1);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, kCenterY, obs_module_text("CenterY"), 0.0, 100.0, 0.1);
	obs_property_float_set_suffix(p, " %");
	obs_properties_add_int_slider(props, kLineCount, obs_module_text("LineCount"), 8, 400, 1);
	p = obs_properties_add_float_slider(props, kThickness, obs_module_text("Thickness"), 1.0, 100.0, 1.0);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, kInnerRadius, obs_module_text("InnerRadius"), 0.0, 100.0, 0.5);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, kInnerJitter, obs_module_text("InnerJitter"), 0.0, 100.0, 0.5);
	obs_property_float_set_suffix(p, " %");
	p = obs_properties_add_float_slider(props, kTaper, obs_module_text("Taper"), 0.0, 100.0, 0.5);
	obs_property_float_set_suffix(p, " %");
	obs_properties_add_color_alpha(props, kColor, obs_module_text("Color"));
	p = obs_properties_add_float_slider(props, kUpdateRate, obs_module_text("UpdateRate"), 0.0, 60.0, 0.5);
	obs_property_float_set_suffix(p, " /s");
	obs_properties_add_int(props, kSeed, obs_module_text("Seed"), 0, 9999, 1);

	return props;
}

void source_video_tick(void *data, float seconds)
{
	static_cast<BurstLinesSource *>(data)->Tick(seconds);
}

uint32_t source_get_width(void *)
{
	return canvas_size().width;
}

uint32_t source_get_height(void *)
{
	return canvas_size().height;
}

void source_video_render(void *data, gs_effect_t *)
{
	static_cast<BurstLinesSource *>(data)->Render();
}

} // namespace

void register_burst_lines_source(void)
{
	obs_source_info info = {};
	info.id = kSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name = source_get_name;
	info.create = source_create;
	info.destroy = source_destroy;
	info.update = source_update;
	info.get_defaults = source_get_defaults;
	info.get_properties = source_get_properties;
	info.video_tick = source_video_tick;
	info.get_width = source_get_width;
	info.get_height = source_get_height;
	info.video_render = source_video_render;
	info.icon_type = OBS_ICON_TYPE_COLOR;
	obs_register_source(&info);
}
