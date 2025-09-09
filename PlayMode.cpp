#include "PlayMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>

#include <random>
#include <iostream>

GLuint bugbat_meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > bugbat_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("BugBat.pnct"));
	bugbat_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > bugbat_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("BugBat.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = bugbat_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = bugbat_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

PlayMode::PlayMode() : scene(*bugbat_scene) {
	//get pointers to leg for convenience:
	for (auto &transform : scene.transforms) {
		//std::cout << "name of transform: " << transform.name << std::endl;
		if (transform.name == "Bug") bug = &transform;
		else if (transform.name == "Bat") bat = &transform;
	}
	if (bug == nullptr) throw std::runtime_error("Bug not found.");
	if (bat == nullptr) throw std::runtime_error("Bat not found.");

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	//set initial bat base rotation
	bat_base_rotation = bat->rotation;

	bugs.emplace_back();
	bugs.back().tf = bug;
	pick_new_dir(bugs.back());

	std::uniform_real_distribution<float> rx(arena_min.x, arena_max.x);
	std::uniform_real_distribution<float> rz(arena_min.y, arena_max.y);
	bug->position = glm::vec3(rx(rng), ground_y, rz(rng));
	face_velocity_y(bugs.back());

}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_ESCAPE) {
			SDL_SetWindowRelativeMouseMode(Mode::window, false);
			return true;
		} 
		
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
		tilt_time = tilt_duration;
		return true;
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		float x = ((evt.motion.x + 0.5f) / float(window_size.x)) * 2.0f - 1.0f;
		float y = ((float(window_size.y) - (evt.motion.y + 0.5f)) / float(window_size.y)) * 2.0f - 1.0f;
		mouse_ndc = glm::vec2(x, y);
		
			return true;
		}

	return false;
}

void PlayMode::update(float elapsed) {

		// dist in front of camera
		static constexpr float FRONT_D = 6.0f;

		//code adapted from using the NDC idea, mouse position from pixel coordinate to NDC
		//(noramalized device coordinates): https://www.scratchapixel.com/lessons/3d-basic-rendering/ray-tracing-generating-camera-rays/generating-camera-rays.html
		float half_fovy = std::tan(0.5f * camera->fovy);
		float half_fovx = camera->aspect * half_fovy;

		glm::mat4x3 frame = camera->transform->make_parent_from_local();
		glm::vec3 cam_pos = camera->transform->position;
		glm::vec3 cam_right = frame[0];
		glm::vec3 cam_up = frame[1];
		glm::vec3 cam_forward = -frame[2];

		glm::vec3 base = cam_pos + cam_forward * FRONT_D;

		float x_span_at_d = FRONT_D * half_fovx;
		float y_span_at_d = FRONT_D * half_fovy;

		// left right and up down at fixed depth
		glm::vec3 target = base + cam_right * (mouse_ndc.x * x_span_at_d)
			+ cam_up * (mouse_ndc.y * y_span_at_d);

		// bat follow mouse
		float follow = 1.0f - std::exp(-elapsed * 20.0f);
		bat->position = glm::mix(bat->position, target, follow);

		// bat tilt 
		if (tilt_time > 0.0f){
			tilt_time = std::max(0.0f, tilt_time - elapsed);
			float t = 1.0f - (tilt_time / tilt_duration);
			float angle = glm::radians(15.0f) * std::sin(t * glm::pi<float>());
			bat->rotation = bat_base_rotation * glm::angleAxis(angle, glm::vec3(-1, 0, 0));
		}
		else {
			bat->rotation = bat_base_rotation;
		}

		spawn_timer -= elapsed;
		if (spawn_timer <= 0.0f && (int)bugs.size() < max_bugs) {
			spawn_bug_clone();
			spawn_timer = spawn_interval;
		}
		update_bugs(elapsed);
}

void PlayMode::draw(glm::uvec2 const& drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f, -1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	GL_ERRORS(); //print any errors produced by this setup code

	scene.draw(*camera);

	{ //use DrawLines to overlay some text:
		glDisable(GL_DEPTH_TEST);
		float aspect = float(drawable_size.x) / float(drawable_size.y);
		DrawLines lines(glm::mat4(
			1.0f / aspect, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		));

		constexpr float H = 0.09f;
		lines.draw_text("Move mouse to steer bat; click to tilt 30 degrees",
			glm::vec3(-aspect + 0.1f * H, -1.0 + 0.1f * H, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0x00, 0x00, 0x00, 0x00));
		float ofs = 2.0f / drawable_size.y;
		lines.draw_text("Move mouse to steer bat; click to tilt 30 degrees",
			glm::vec3(-aspect + 0.1f * H + ofs, -1.0 + 0.1f * H + ofs, 0.0),
			glm::vec3(H, 0.0f, 0.0f), glm::vec3(0.0f, H, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, 0x00));
	}
}

	Scene::Drawable* PlayMode::find_drawable_for(Scene::Transform * tf) {
		for (auto& d : scene.drawables) {
			if (d.transform == tf) return &d;
		}
		return nullptr;
	}

	void PlayMode::pick_new_dir(Buggy& b) {
		
		std::uniform_real_distribution<float> angle(0.0f, 2.0f * glm::pi<float>());
		std::uniform_real_distribution<float> speed(0.6f, 1.1f);
		std::uniform_real_distribution<float> hold(1.0f, 2.0f);

		float a = angle(rng);
		float s = speed(rng);
		b.velocity = glm::vec3(std::sin(a) * s, 0.0f, std::cos(a) * s);
		b.dir_timer = hold(rng);
	}

	void PlayMode::face_velocity_y(Buggy& b) {
		if (!b.tf) return;
		glm::vec2 v = glm::vec2(b.velocity.x, b.velocity.z);
		if (glm::length(v) < 1e-4f) return;
		float yaw = std::atan2(b.velocity.x, -b.velocity.z);
		b.tf->rotation = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
	}

	PlayMode::Buggy& PlayMode::spawn_bug_clone() {
		Scene::Transform* proto = bug;// use original bug as template
		Scene::Transform* t = &scene.transforms.emplace_back();
		t->name = "Bug " + std::to_string(bugs.size());
		t->parent = proto->parent;
		t->position = proto->position;
		t->rotation = proto->rotation;
		t->scale = proto->scale;

		Scene::Drawable* pd = find_drawable_for(proto);
		if (!pd) throw std::runtime_error("prototype bug not drawable");
		scene.drawables.emplace_back(t);
		Scene::Drawable& nd = scene.drawables.back();
		nd.pipeline = pd->pipeline;

		std::uniform_real_distribution<float> rx(arena_min.x, arena_max.x);
		std::uniform_real_distribution<float> rz(arena_min.y, arena_max.y);
		t->position = glm::vec3(rx(rng), ground_y, rz(rng));

		bugs.emplace_back();
		Buggy& b = bugs.back();
		b.tf = t;
		pick_new_dir(b);
		face_velocity_y(b);
		return b;
	}

	void PlayMode::update_bugs(float dt) {
		std::normal_distribution<float> jitter(0.0f, 0.2f);

		for (auto& b : bugs) {
			if (!b.tf) continue;

			// countdown to pick a new wandering direction
			b.dir_timer -= dt;
			if (b.dir_timer <= 0.0f) {
				pick_new_dir(b);
			}

			// make bugs walk with jitter on XZ (not Y)
			b.velocity.x += jitter(rng) * dt;
			b.velocity.z += jitter(rng) * dt;

			// clamp horizontal speed
			float vlen = glm::length(glm::vec2(b.velocity.x, b.velocity.z));
			float vmax = 1.2f;
			if (vlen > vmax) {
				glm::vec2 hv = glm::vec2(b.velocity.x, b.velocity.z) * (vmax / vlen);
				b.velocity.x = hv.x;
				b.velocity.z = hv.y;
			}
			b.velocity.y = 0.0f; // keep on the ground

			// integrate and stick to ground plane
			glm::vec3 p = b.tf->position + b.velocity * dt;
			p.y = ground_y;

			bool bounced = false;
			if (p.x < arena_min.x) { p.x = arena_min.x; b.velocity.x = std::abs(b.velocity.x); bounced = true; }
			if (p.x > arena_max.x) { p.x = arena_max.x; b.velocity.x = -std::abs(b.velocity.x); bounced = true; }
			if (p.z < arena_min.y) { p.z = arena_min.y; b.velocity.z = std::abs(b.velocity.z); bounced = true; }
			if (p.z > arena_max.y) { p.z = arena_max.y; b.velocity.z = -std::abs(b.velocity.z); bounced = true; }

			b.tf->position = p;

			// face the current velocity when moving or after a bounce
			if (bounced || glm::length(glm::vec2(b.velocity.x, b.velocity.z)) > 1e-4f) {
				face_velocity_y(b);
			}
		}
	}


