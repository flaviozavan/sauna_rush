#define MAX_GROUND_CHANGES 4
#define NUM_SFX_CHANNELS 4
#define FIRST_SFX_CHANNEL (31-NUM_SFX_CHANNELS)
#define WALK_SPEED 100.f
#define EPS 1e-6
#define TIMER_Y 220
#define HUD_HORIZONTAL_BORDER 26
#define HUD_VERTICAL_BORDER 26
#define HUD_INDIVIDUAL_H_SPACE ((320 - HUD_HORIZONTAL_BORDER*2)/4)
#define HUD_BAR_HEIGHT 16
#define HUD_BAR_Y_OFFSET 4
#define HUD_BAR_X_OFFSET 1
#define GRAVITY 200.f

struct entity {
  const T3DModel *model;
  T3DMat4FP *transform;
  T3DSkeleton *skeleton;
  rspq_block_t *display_block;
};

struct skeleton {
  T3DSkeleton skeleton;
  T3DAnim *anims;
  size_t num_anims;
};

struct camera {
  T3DVec3 pos;
  T3DVec3 target;
};

struct character {
  struct entity e;
  T3DVec3 pos;
  float rotation;
  float scale;
  struct skeleton s;
  size_t current_anim;
  bool visible;
  float temperature;
  bool out;
};

struct ground_height_change {
  float start_z;
  float height;
};

struct ground {
  size_t num_changes;
  struct ground_height_change changes[MAX_GROUND_CHANGES];
};

struct scene {
  const char *bg_path;
  const char *z_path;
  sprite_t *bg;
  sprite_t *z;
  float fov;
  struct camera starting_cam;
  void (*do_light)();
  struct ground ground;
};

struct script_action {
  int type;
  union {
    struct {
      float rot;
      float speed;
    };
    T3DVec3 pos;
    size_t anim;
    bool visibility;
    float time;
    wav64_t *sfx;
    struct {
      xm64player_t *xm64;
      size_t first_channel;
    };
  };
};

struct script_state {
  struct character *character;
  const struct script_action *action;
  float time;
};

struct subgame {
  void (*dynamic_loop_pre)(float, bool);
  void (*dynamic_loop_render)(float, bool);
  void (*dynamic_loop_post)(float, bool);
  bool (*fixed_loop)(float, bool);
  void (*cleanup)();
  void (*init)();
};

enum script_actions {
  ACTION_WAIT,
  ACTION_WALK_TO,
  ACTION_WARP_TO,
  ACTION_CLIMB_TO,
  ACTION_ROTATE_TO,
  ACTION_START_ANIM,
  ACTION_SET_VISIBILITY,
  ACTION_DO_WHOLE_ANIM,
  ACTION_PLAY_SFX,
  ACTION_START_XM64,
  ACTION_END,
};

enum fonts {
  FONT_NORMAL = 1,
  FONT_TIMER,
  FONT_BANNER,
};

enum sw_styles {
  SW_NORMAL,
  SW_BANNER,
  SW_TIMER,
  SW_PLAYER1,
  SW_PLAYER2,
  SW_PLAYER3,
  SW_PLAYER4,
  SW_OUT,
  SW_SELECTED,
};

#define SW_NORMAL_S "^00"
#define SW_BANNER_S "^01"
#define SW_TIMER_S "^02"
#define SW_PLAYER1_S "^03"
#define SW_PLAYER2_S "^04"
#define SW_PLAYER3_S "^05"
#define SW_PLAYER4_S "^06"
#define SW_OUT_S "^07"
#define SW_SELECTED_S "^08"

enum player_anims {
  WALK,
  CLIMB,
  SIT,
  BEND,
  UNBEND,
  STAND_UP,
  NUM_PLAYER_ANIMS,
};

float get_ground_height(float z, struct ground *ground);
int get_next_sfx_channel();
void init_sfx();
void skeleton_init(struct skeleton *s,
    const T3DModel *model,
    size_t num_anims);
void skeleton_free(struct skeleton *s);
void entity_init(struct entity *e,
    const T3DModel *model,
    const T3DVec3 *scale,
    const T3DVec3 *rotation,
    const T3DVec3 *pos,
    T3DSkeleton *skeleton,
    T3DModelDrawConf draw_conf);
void entity_free(struct entity *e);
bool script_update(struct script_state *state, float delta_time);
void draw_hud();
rspq_block_t *build_empty_hud_block();
