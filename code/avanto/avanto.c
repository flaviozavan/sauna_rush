#include <libdragon.h>
#include "../../minigame.h"
#include "../../core.h"
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/tpx.h>

#include "common.h"
#include "sauna.h"

const MinigameDef minigame_def = {
    .gamename = "Avanto",
    .developername = "Flavio Zavan",
    .description = "Whatever description",
    .instructions = "Sauna: Hold Z to crouch\nWater: Blabla"
};

surface_t *z_buffer;
T3DViewport viewport;
struct camera cam;
T3DModel *player_model;
struct character players[4];
struct scene *current_scene;
rdpq_font_t *normal_font;
rdpq_font_t *timer_font;
rdpq_font_t *banner_font;
color_t player_colors[4];
struct rdpq_textparms_s banner_params;
struct rdpq_textparms_s timer_params;
rspq_block_t *empty_hud_block;
bool paused;
struct subgame subgames[] = {
  {
    .fixed_loop = sauna_fixed_loop,
    .dynamic_loop_pre = sauna_dynamic_loop_pre,
    .dynamic_loop_render = sauna_dynamic_loop_render,
    .dynamic_loop_post = sauna_dynamic_loop_post,
    .cleanup = sauna_cleanup,
    .init = sauna_init,
  },
  {
    .fixed_loop = NULL,
    .dynamic_loop_pre = NULL,
    .dynamic_loop_render = NULL,
    .dynamic_loop_post = NULL,
    .cleanup = NULL,
    .init = NULL,
  },
};
struct subgame *current_subgame;

xm64player_t music;
wav64_t sfx_start;
wav64_t sfx_countdown;
wav64_t sfx_stop;
wav64_t sfx_winner;

struct particle_source particle_sources[MAX_PARTICLE_SOURCES];

static bool filter_player_hair_color(void *user_data, const T3DObject *obj) {
  color_t *color = (color_t *) user_data;
  if (!strcmp(obj->name, "hair")) {
    rdpq_set_prim_color(*color);
  }

  return true;
}

static void update_all_particles(float delta_time) {
  for (size_t i = 0; i < MAX_PARTICLE_SOURCES; i++) {
    particle_source_iterate(&particle_sources[i], delta_time);
  }
}


void minigame_init() {
  player_colors[0] = PLAYERCOLOR_1;
  player_colors[1] = PLAYERCOLOR_2;
  player_colors[2] = PLAYERCOLOR_3;
  player_colors[3] = PLAYERCOLOR_4;

  display_init(RESOLUTION_320x240,
      DEPTH_16_BPP,
      3,
      GAMMA_NONE,
      FILTERS_RESAMPLE_ANTIALIAS);
  z_buffer = display_get_zbuf();

  t3d_init((T3DInitParams){});
  tpx_init((TPXInitParams){});
  viewport = t3d_viewport_create();

  player_model = t3d_model_load("rom:/avanto/guy.t3dm");
  T3DModelDrawConf player_draw_conf = {
    .userData = NULL,
    .tileCb = NULL,
    .filterCb = filter_player_hair_color,
    .dynTextureCb = NULL,
    .matrices = NULL,
  };
  for (size_t i = 0; i < 4; i++) {
    players[i].rotation = 0;
    skeleton_init(&players[i].s, player_model, NUM_PLAYER_ANIMS);
    players[i].s.anims[WALK] = t3d_anim_create(player_model, "walking");
    players[i].s.anims[CLIMB] = t3d_anim_create(player_model, "climbing");
    t3d_anim_set_looping(&players[i].s.anims[CLIMB], false);
    players[i].s.anims[SIT] = t3d_anim_create(player_model, "sitting");
    t3d_anim_set_looping(&players[i].s.anims[SIT], false);
    players[i].s.anims[BEND] = t3d_anim_create(player_model, "bending");
    t3d_anim_set_looping(&players[i].s.anims[BEND], false);
    players[i].s.anims[UNBEND] = t3d_anim_create(player_model, "unbending");
    t3d_anim_set_looping(&players[i].s.anims[UNBEND], false);
    players[i].s.anims[STAND_UP] =
      t3d_anim_create(player_model, "standing_up");
    t3d_anim_set_looping(&players[i].s.anims[STAND_UP], false);
    players[i].s.anims[PASS_OUT] =
      t3d_anim_create(player_model, "passing_out");
    t3d_anim_set_looping(&players[i].s.anims[PASS_OUT], false);
    players[i].pos = (T3DVec3) {{0, 0, 0}};
    players[i].scale = 2.5f;
    players[i].current_anim = -1;
    players[i].visible = false;
    players[i].temperature = 0.f;
    players[i].out = false;
    player_draw_conf.userData = &player_colors[i];
    entity_init(&players[i].e,
        player_model,
        &(T3DVec3) {{players[i].scale, players[i].scale, players[i].scale}},
        &(T3DVec3) {{0, players[i].rotation, 0}},
        &players[i].pos,
        &players[i].s.skeleton,
        player_draw_conf);
  }

  const color_t BLACK = RGBA32(0x00, 0x00, 0x00, 0xff);
  const color_t YELLOW = RGBA32(0xff, 0xff, 0x00, 0xff);
  const color_t WHITE = RGBA32(0xff, 0xff, 0xff, 0xff);
  const color_t LIGHT_BLUE = RGBA32(0x00, 0xc9, 0xff, 0xff);
  normal_font = rdpq_font_load("rom:/squarewave.font64");
  rdpq_text_register_font(FONT_NORMAL, normal_font);
  timer_font = rdpq_font_load("rom:/avanto/timer.font64");
  rdpq_text_register_font(FONT_TIMER, timer_font);
  banner_font = rdpq_font_load("rom:/avanto/banner.font64");
  rdpq_text_register_font(FONT_BANNER, banner_font);

  rdpq_font_t *fonts[] = {normal_font, timer_font, banner_font};
  for (size_t i = 0; i < 3; i++) {
    rdpq_font_t *font = fonts[i];

    rdpq_font_style(font,
        SW_NORMAL,
        &(rdpq_fontstyle_t) {.color = WHITE, .outline_color = BLACK});
    rdpq_font_style(font,
        SW_BANNER,
        &(rdpq_fontstyle_t) {.color = YELLOW, .outline_color = BLACK});
    rdpq_font_style(font,
        SW_TIMER,
        &(rdpq_fontstyle_t) {.color = YELLOW, .outline_color = BLACK});
    rdpq_font_style(font,
        SW_OUT,
        &(rdpq_fontstyle_t) {.color = LIGHT_BLUE, .outline_color = BLACK});
    rdpq_font_style(font,
        SW_OUT,
        &(rdpq_fontstyle_t) {.color = LIGHT_BLUE, .outline_color = BLACK});
    rdpq_font_style(font,
        SW_SELECTED,
        &(rdpq_fontstyle_t) {.color = YELLOW, .outline_color = BLACK});
    for (size_t j = 0; j < 4; j++) {
      rdpq_font_style(font,
          SW_PLAYER1 + j,
          &(rdpq_fontstyle_t) {
            .color = player_colors[j],
            .outline_color = BLACK,
          });
    }
  }
  memset(&banner_params, 0, sizeof(banner_params));
  banner_params.style_id = SW_BANNER;
  banner_params.align = 1;
  banner_params.valign = 1;
  banner_params.width = 320;

  memset(&timer_params, 0, sizeof(timer_params));
  timer_params.style_id = SW_TIMER;
  timer_params.align = 1;
  timer_params.width = 320;

  xm64player_open(&music, "rom:/avanto/sj-polkka.xm64");

  wav64_open(&sfx_start, "rom:/core/Start.wav64");
  wav64_open(&sfx_countdown, "rom:/core/Countdown.wav64");
  wav64_open(&sfx_stop, "rom:/core/Stop.wav64");
  wav64_open(&sfx_winner, "rom:/core/Winner.wav64");

  mixer_set_vol(1.f);
  for (int i = xm64player_num_channels(&music); i < 32; i++) {
    mixer_ch_set_vol(i, 0.5f, 0.5f);
    mixer_ch_set_limits(i, 0, 48000, 0);
  }

  empty_hud_block = build_empty_hud_block();

  paused = false;

  for (size_t i = 0; i < MAX_PARTICLE_SOURCES; i++) {
    particle_source_pre_init(&particle_sources[i]);
  }

  current_subgame = &subgames[0];
  current_subgame->init();
}

void minigame_fixedloop(float delta_time) {
  if (current_subgame->fixed_loop) {
    if (current_subgame->fixed_loop(delta_time, paused)) {
      current_subgame->cleanup();
      current_subgame++;
      if (current_subgame->init) {
        current_subgame->init();
      }
      else {
        minigame_end();
      }
    }
  }

  update_all_particles(delta_time);
}

void minigame_loop(float delta_time) {
  static int paused_controller = 0;
  static int paused_selection = 0;

  if (!paused) {
    for (size_t i = 0; i < core_get_playercount(); i++) {
      joypad_buttons_t pressed = joypad_get_buttons_pressed(
          core_get_playercontroller(i));
      {
      }
      if (pressed.start) {
        paused_controller = core_get_playercontroller(i);
        paused_selection = 0;
        paused = true;
        mixer_set_vol(.2f);
        break;
      }
    }
  }
  else {
    joypad_buttons_t pressed = joypad_get_buttons_pressed(paused_controller);
    int axis = joypad_get_axis_pressed(paused_controller, JOYPAD_AXIS_STICK_X);
    if (pressed.start || pressed.b || (pressed.a && !paused_selection)) {
      paused = false;
      mixer_set_vol(1.f);
    } else if (pressed.a) {
      minigame_end();
    } else if (pressed.d_left || axis || pressed.d_right) {
      paused_selection ^= 1;
    }
  }

  if (current_subgame->dynamic_loop_pre) {
    current_subgame->dynamic_loop_pre(delta_time, paused);
  }

  rdpq_attach(display_get(), z_buffer);
  t3d_frame_start();
  t3d_viewport_attach(&viewport);

  if (current_subgame->dynamic_loop_render) {
    current_subgame->dynamic_loop_render(delta_time, paused);
  }

  rdpq_sync_pipe();
  rdpq_sync_tile();
  rdpq_mode_push();
  rdpq_mode_zbuf(false, false);

  if (paused) {


    rdpq_mode_push();
    rdpq_set_fog_color(RGBA32(0xff, 0xff, 0xff, 0xc0));
    rdpq_set_prim_color(RGBA32(0x00, 0x00, 0x00, 0xff));
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
    rdpq_fill_rectangle(0, 0, 320, 240);
    rdpq_mode_pop();

    rdpq_text_print(&banner_params, FONT_BANNER, 0, 60, "PAUSED");

    rdpq_textparms_t params = {
      .width = 300,
      .height = 130,
      .wrap = WRAP_WORD,
    };

    rdpq_text_printf(&params,
        FONT_NORMAL,
        10,
        85,
        "%s by %s\n\n%s\n\n%s",
        minigame_def.gamename,
        minigame_def.developername,
        minigame_def.description,
        minigame_def.instructions);

    rdpq_text_printf(NULL,
        FONT_NORMAL,
        40,
        215,
        "%sRESUME",
        paused_selection? SW_NORMAL_S : SW_SELECTED_S);
    rdpq_text_printf(NULL,
        FONT_NORMAL,
        40+160,
        215,
        "%sQUIT",
        !paused_selection? SW_NORMAL_S : SW_SELECTED_S);
  }

  rdpq_text_printf(NULL,
    FONT_NORMAL,
    10,
    235,
    SW_NORMAL_S "FPS: %.2f",
    display_get_fps());
  rdpq_mode_pop();

  rdpq_detach_show();

  if (current_subgame->dynamic_loop_post) {
    current_subgame->dynamic_loop_post(delta_time, paused);
  }
}

void minigame_cleanup() {
  rspq_wait();

  rspq_block_free(empty_hud_block);

  if (current_subgame->cleanup) {
    current_subgame->cleanup();
  }
  wav64_close(&sfx_start);
  wav64_close(&sfx_countdown);
  wav64_close(&sfx_stop);
  wav64_close(&sfx_winner);

  xm64player_stop(&music);
  xm64player_close(&music);

  rdpq_text_unregister_font(FONT_NORMAL);
  rdpq_font_free(normal_font);
  rdpq_text_unregister_font(FONT_TIMER);
  rdpq_font_free(timer_font);
  rdpq_text_unregister_font(FONT_BANNER);
  rdpq_font_free(banner_font);

  for (size_t i = 0; i < 4; i++) {
    entity_free(&players[i].e);
    skeleton_free(&players[i].s);
  }
  t3d_model_free(player_model);

  for (size_t i = 0; i < MAX_PARTICLE_SOURCES; i++) {
    particle_source_free(&particle_sources[i]);
  }

  tpx_destroy();
  t3d_destroy();
  display_close();
}
