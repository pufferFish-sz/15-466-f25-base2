#include "Mode.hpp"

#include "Scene.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>
#include <random>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	//struct Button {
	//	uint8_t downs = 0;
	//	uint8_t pressed = 0;
	//} left, right, down, up;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	//objects in the scene:
	Scene::Transform *bug = nullptr;
	Scene::Transform *bat = nullptr;
	glm::quat bat_base_rotation;
	float tilt_time = 0.0f;
	static constexpr float tilt_duration = 0.25f;
	
	//camera:
	Scene::Camera *camera = nullptr;

	//mouse
	glm::vec2 mouse_ndc = glm::vec2(0.0f);

	struct Buggy {
		Scene::Transform* tf = nullptr;
		glm::vec3 velocity = glm::vec3(0.0f);
		float dir_timer = 0.0f;
	};

	std::vector<Buggy> bugs;
	// x-z plane
	glm::vec2 arena_min = glm::vec2(-6.0f, -4.0f); // minx, minz
	glm::vec2 arena_max = glm::vec2(6.0f, 4.0f); // maxx, maxz
	float ground_y = 0.0f; //floor

	//spawn
	float spawn_timer = 0.0f;
	float spawn_interval = 2.0f;
	int max_bugs = 12;

	std::mt19937 rng{ std::random_device{}() };

	void update_bugs(float dt);
	Buggy& spawn_bug_clone();
	void pick_new_dir(Buggy& b);
	void face_velocity_y(Buggy& b);
	Scene::Drawable* find_drawable_for(Scene::Transform* tf);

};

