#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <cmath>
#include <new>

namespace {

constexpr const char *kSourceId = "simple_burst_lines_source";

constexpr const char *kUseCanvasSize = "use_canvas_size";
constexpr const char *kWidth = "width";
constexpr const char *kHeight = "height";
constexpr const char *kCenterX = "center_x";
constexpr const char *kCenterY = "center_y";
constexpr const char *kLineCount = "line_count";
constexpr const char *kSpacingJitter = "spacing_jitter";
constexpr const char *kThickness = "thickness";
constexpr const char *kTaper = "taper";
constexpr const char *kInnerRadius = "inner_radius";
constexpr const char *kInnerAspect = "inner_aspect";
constexpr const char *kInnerSoftness = "inner_softness";
constexpr const char *kLengthJitter = "length_jitter";
constexpr const char *kColor = "color";
constexpr const char *kUpdateRate = "update_rate";
constexpr const char *kSeed = "seed";

constexpr uint32_t kMinSize = 16;
constexpr uint32_t kMaxSize = 8192;

// 経過時間を巡回させ、段階の値 (最大で経過秒数 × 60) を float で正確に保てる範囲に収める
constexpr double kStepWrap = 100000.0;

float percent(obs_data_t *settings, const char *name)
{
	return static_cast<float>(obs_data_get_double(settings, name)) / 100.0f;
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
			param_spacing_jitter_ = gs_effect_get_param_by_name(effect_, "spacing_jitter");
			param_thickness_ = gs_effect_get_param_by_name(effect_, "thickness");
			param_taper_length_ = gs_effect_get_param_by_name(effect_, "taper_length");
			param_inner_radius_ = gs_effect_get_param_by_name(effect_, "inner_radius");
			param_inner_scale_ = gs_effect_get_param_by_name(effect_, "inner_scale");
			param_inner_softness_ = gs_effect_get_param_by_name(effect_, "inner_softness");
			param_length_jitter_ = gs_effect_get_param_by_name(effect_, "length_jitter");
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
		use_canvas_size_ = obs_data_get_bool(settings, kUseCanvasSize);
		width_ = static_cast<uint32_t>(obs_data_get_int(settings, kWidth));
		height_ = static_cast<uint32_t>(obs_data_get_int(settings, kHeight));
		center_x_ = percent(settings, kCenterX);
		center_y_ = percent(settings, kCenterY);
		line_count_ = static_cast<float>(obs_data_get_int(settings, kLineCount));
		spacing_jitter_ = percent(settings, kSpacingJitter);
		thickness_ = percent(settings, kThickness);
		taper_ = percent(settings, kTaper);
		inner_radius_ = percent(settings, kInnerRadius);
		inner_aspect_ = static_cast<float>(obs_data_get_double(settings, kInnerAspect));
		inner_softness_ = percent(settings, kInnerSoftness);
		length_jitter_ = percent(settings, kLengthJitter);
		update_rate_ = obs_data_get_double(settings, kUpdateRate);
		seed_ = static_cast<float>(obs_data_get_int(settings, kSeed));
		const uint32_t color = static_cast<uint32_t>(obs_data_get_int(settings, kColor));
		vec4_from_rgba(&line_color_, color);
		vec4_from_rgba_srgb(&line_color_linear_, color);
	}

	uint32_t Width() const
	{
		if (!use_canvas_size_) {
			return width_;
		}
		obs_video_info ovi;
		return obs_get_video_info(&ovi) ? ovi.base_width : 1920;
	}

	uint32_t Height() const
	{
		if (!use_canvas_size_) {
			return height_;
		}
		obs_video_info ovi;
		return obs_get_video_info(&ovi) ? ovi.base_height : 1080;
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

		const uint32_t width_px = Width();
		const uint32_t height_px = Height();
		const float width = static_cast<float>(width_px);
		const float height = static_cast<float>(height_px);
		// 長さの設定は、画面の対角線の半分に対する割合で持つ。中心を動かしても線の長さの見た目を保つため
		const float half_diagonal = 0.5f * std::sqrt(width * width + height * height);
		// 楕円の横と縦の倍率。積を 1 に保ち、縦横比を変えても面積の見た目を保つ
		const float aspect_root = std::sqrt(inner_aspect_);

		vec2 size;
		vec2_set(&size, width, height);
		vec2 center;
		vec2_set(&center, center_x_ * width, center_y_ * height);
		vec2 inner_scale;
		vec2_set(&inner_scale, aspect_root, 1.0f / aspect_root);

		gs_effect_set_vec2(param_size_, &size);
		gs_effect_set_vec2(param_center_, &center);
		gs_effect_set_float(param_line_count_, line_count_);
		gs_effect_set_float(param_spacing_jitter_, spacing_jitter_);
		gs_effect_set_float(param_thickness_, thickness_);
		gs_effect_set_float(param_taper_length_, taper_ * half_diagonal);
		gs_effect_set_float(param_inner_radius_, inner_radius_ * half_diagonal);
		gs_effect_set_vec2(param_inner_scale_, &inner_scale);
		gs_effect_set_float(param_inner_softness_, inner_softness_ * half_diagonal);
		gs_effect_set_float(param_length_jitter_, length_jitter_ * half_diagonal);
		gs_effect_set_float(param_time_step_, time_step_);
		gs_effect_set_float(param_seed_, seed_);
		// OBS の色ソースと同じく、半透明か線形の色空間で合成するときは sRGB を線形に直した色で描き、
		// フレームバッファの sRGB 変換を有効にする。合成の方法は呼び出し側の設定に従う
		const bool linear_srgb = gs_get_linear_srgb() || line_color_.w < 1.0f;
		gs_effect_set_vec4(param_line_color_, linear_srgb ? &line_color_linear_ : &line_color_);
		const bool previous = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(linear_srgb);
		while (gs_effect_loop(effect_, "Draw")) {
			gs_draw_sprite(nullptr, 0, width_px, height_px);
		}
		gs_enable_framebuffer_srgb(previous);
	}

private:
	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *param_size_ = nullptr;
	gs_eparam_t *param_center_ = nullptr;
	gs_eparam_t *param_line_count_ = nullptr;
	gs_eparam_t *param_spacing_jitter_ = nullptr;
	gs_eparam_t *param_thickness_ = nullptr;
	gs_eparam_t *param_taper_length_ = nullptr;
	gs_eparam_t *param_inner_radius_ = nullptr;
	gs_eparam_t *param_inner_scale_ = nullptr;
	gs_eparam_t *param_inner_softness_ = nullptr;
	gs_eparam_t *param_length_jitter_ = nullptr;
	gs_eparam_t *param_time_step_ = nullptr;
	gs_eparam_t *param_seed_ = nullptr;
	gs_eparam_t *param_line_color_ = nullptr;

	bool use_canvas_size_ = true;
	uint32_t width_ = 1920;
	uint32_t height_ = 1080;
	float center_x_ = 0.5f;
	float center_y_ = 0.5f;
	float line_count_ = 120.0f;
	float spacing_jitter_ = 1.0f;
	float thickness_ = 0.3f;
	float taper_ = 0.2f;
	float inner_radius_ = 0.45f;
	float inner_aspect_ = 1.78f;
	float inner_softness_ = 0.05f;
	float length_jitter_ = 0.15f;
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
	obs_data_set_default_bool(settings, kUseCanvasSize, true);
	obs_data_set_default_int(settings, kWidth, 1920);
	obs_data_set_default_int(settings, kHeight, 1080);
	obs_data_set_default_double(settings, kCenterX, 50.0);
	obs_data_set_default_double(settings, kCenterY, 50.0);
	obs_data_set_default_int(settings, kLineCount, 120);
	obs_data_set_default_double(settings, kSpacingJitter, 100.0);
	obs_data_set_default_double(settings, kThickness, 30.0);
	obs_data_set_default_double(settings, kTaper, 20.0);
	obs_data_set_default_double(settings, kInnerRadius, 45.0);
	obs_data_set_default_double(settings, kInnerAspect, 1.78);
	obs_data_set_default_double(settings, kInnerSoftness, 5.0);
	obs_data_set_default_double(settings, kLengthJitter, 15.0);
	obs_data_set_default_int(settings, kColor, 0xFF000000);
	obs_data_set_default_double(settings, kUpdateRate, 12.0);
	obs_data_set_default_int(settings, kSeed, 0);
}

bool use_canvas_size_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	const bool custom = !obs_data_get_bool(settings, kUseCanvasSize);
	obs_property_set_visible(obs_properties_get(props, kWidth), custom);
	obs_property_set_visible(obs_properties_get(props, kHeight), custom);
	return true;
}

obs_property_t *add_percent(obs_properties_t *props, const char *name, const char *text, double min, double max,
			    double step)
{
	obs_property_t *p = obs_properties_add_float_slider(props, name, obs_module_text(text), min, max, step);
	obs_property_float_set_suffix(p, " %");
	return p;
}

obs_properties_t *source_get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p = obs_properties_add_bool(props, kUseCanvasSize, obs_module_text("UseCanvasSize"));
	obs_property_set_modified_callback(p, use_canvas_size_modified);
	obs_properties_add_int(props, kWidth, obs_module_text("Width"), kMinSize, kMaxSize, 1);
	obs_properties_add_int(props, kHeight, obs_module_text("Height"), kMinSize, kMaxSize, 1);

	add_percent(props, kCenterX, "CenterX", 0.0, 100.0, 0.1);
	add_percent(props, kCenterY, "CenterY", 0.0, 100.0, 0.1);

	obs_properties_add_int_slider(props, kLineCount, obs_module_text("LineCount"), 8, 400, 1);
	add_percent(props, kSpacingJitter, "SpacingJitter", 0.0, 100.0, 1.0);
	add_percent(props, kThickness, "Thickness", 1.0, 100.0, 1.0);
	add_percent(props, kTaper, "Taper", 0.0, 100.0, 0.5);

	add_percent(props, kInnerRadius, "InnerRadius", 0.0, 100.0, 0.5);
	obs_properties_add_float_slider(props, kInnerAspect, obs_module_text("InnerAspect"), 0.25, 4.0, 0.01);
	add_percent(props, kInnerSoftness, "InnerSoftness", 0.0, 50.0, 0.5);
	add_percent(props, kLengthJitter, "LengthJitter", 0.0, 100.0, 0.5);

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

uint32_t source_get_width(void *data)
{
	return static_cast<BurstLinesSource *>(data)->Width();
}

uint32_t source_get_height(void *data)
{
	return static_cast<BurstLinesSource *>(data)->Height();
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
