#include <libdragon.h>
#include "../../minigame.h"
#include "../../core.h"
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/tpx.h>

#include "common.h"

extern T3DViewport viewport;
extern struct character players[];
extern rspq_block_t *empty_hud_block;
extern struct particle_source particle_sources[];

static int next_sfx_channel = FIRST_SFX_CHANNEL;

float get_ground_height(float z, struct ground *ground) {
  float height = 0;
  for (size_t i = 0; i < ground->num_changes; i++) {
    if (ground->changes[i].start_z > z) {
      break;
    }
    height = ground->changes[i].height;
  }
  return height;
}

int get_next_sfx_channel() {
  int r = next_sfx_channel++;
  if (next_sfx_channel >= FIRST_SFX_CHANNEL + NUM_SFX_CHANNELS) {
    next_sfx_channel = FIRST_SFX_CHANNEL;
  }
  return r;
}

void skeleton_init(struct skeleton *s,
    const T3DModel *model,
    size_t num_anims) {
  s->skeleton = t3d_skeleton_create(model);
  s->num_anims = num_anims;
  s->anims = malloc(sizeof(T3DAnim) * num_anims);
}

void skeleton_free(struct skeleton *s) {
  for (size_t i = 0; i < s->num_anims; i++) {
    t3d_anim_destroy(&s->anims[i]);
  }
  free(s->anims);
  t3d_skeleton_destroy(&s->skeleton);
}

void entity_init(struct entity *e,
    const T3DModel *model,
    const T3DVec3 *scale,
    const T3DVec3 *rotation,
    const T3DVec3 *pos,
    T3DSkeleton *skeleton,
    T3DModelDrawConf draw_conf) {

  e->model = model;
  e->transform = malloc_uncached(sizeof(T3DMat4FP));
  t3d_mat4fp_from_srt_euler(e->transform, scale->v, rotation->v, pos->v);
  e->skeleton = skeleton;

  rspq_block_begin();
  t3d_matrix_push(e->transform);

  if (e->skeleton) {
    draw_conf.matrices = skeleton->bufferCount == 1? skeleton->boneMatricesFP
      : (const T3DMat4FP*) t3d_segment_placeholder(T3D_SEGMENT_SKELETON);
  }
  t3d_model_draw_custom(e->model, draw_conf);

  t3d_matrix_pop(1);
  e->display_block = rspq_block_end();
}

void entity_free(struct entity *e) {
  free_uncached(e->transform);
  rspq_block_free(e->display_block);
}

bool script_update(struct script_state *state, float delta_time) {
  while (delta_time > EPS) {
    if (state->action->type == ACTION_END) {
      return true;
    }
    else if (state->action->type == ACTION_WARP_TO) {
      state->character->pos = state->action->pos;
    }
    else if (state->action->type == ACTION_WAIT) {
      if (state->time + delta_time >= state->action->time) {
        delta_time -= state->action->time - state->time;
      }
      else {
        state->time += delta_time;
        break;
      }
    }
    else if (state->action->type == ACTION_SET_VISIBILITY) {
      state->character->visible = state->action->visibility;
    }
    else if (state->action->type == ACTION_START_ANIM
        || state->action->type == ACTION_DO_WHOLE_ANIM) {
      struct character *c = state->character;
      T3DAnim *anim = &c->s.anims[state->action->anim];
      if (!state->time) {
        c->current_anim = state->action->anim;
        t3d_anim_attach(anim, &c->s.skeleton);
        t3d_anim_set_playing(anim, true);
      }

      if (state->action->type == ACTION_DO_WHOLE_ANIM) {
        // Anim not over
        if (state->time - anim->animRef->duration < EPS) {
          state->time += delta_time;
          break;
        }
        else {
          delta_time -= anim->animRef->duration - state->time;
        }
      }
    }
    else if (state->action->type == ACTION_WALK_TO
        || state->action->type == ACTION_CLIMB_TO) {
      struct character *c = state->character;
      if (!state->time) {
        size_t anim = state->action->type == ACTION_WALK_TO? WALK : CLIMB;
        c->current_anim = anim;
        t3d_anim_attach(&c->s.anims[anim], &c->s.skeleton);
        float dz = state->action->pos.v[2] - c->pos.v[2];
        float dx = state->action->pos.v[0] - c->pos.v[0];
        c->rotation = -fm_atan2f(dx, dz);
      }

      T3DVec3 diff;
      t3d_vec3_diff(&diff,
          &(T3DVec3) {{state->action->pos.v[0], 0.f, state->action->pos.v[2]}},
          &(T3DVec3) {{c->pos.v[0], 0.f, c->pos.v[2]}});

      float time_to_end = state->action->type == ACTION_WALK_TO?
        t3d_vec3_len(&diff) / WALK_SPEED :
        c->s.anims[CLIMB].animRef->duration - state->time;

      if (time_to_end - delta_time < EPS) {
        delta_time -= time_to_end;
        c->pos.v[0] = state->action->pos.v[0];
        c->pos.v[2] = state->action->pos.v[2];
      }
      else {
        float ratio = delta_time / time_to_end;
        c->pos.v[0] += ratio * diff.v[0];
        c->pos.v[2] += ratio * diff.v[2];
        state->time += delta_time;
        break;
      }
    }
    else if (state->action->type == ACTION_ROTATE_TO) {
      struct character *c = state->character;
      float target = state->action->rot;

      if (state->action->speed > 0 && target < c->rotation) {
        target += T3D_PI*2.f;
      }
      else if (state->action->speed < 0 && target > c->rotation) {
        target -= T3D_PI*2.f;
      }

      float time_to_end = (target - c->rotation) / state->action->speed;
      if (time_to_end - delta_time < EPS) {
        delta_time -= time_to_end;
        // Original, unchanged target
        c->rotation = state->action->rot;
      }
      else {
        c->rotation += state->action->speed * delta_time;
        break;
      }
    }
    else if (state->action->type == ACTION_PLAY_SFX) {
      wav64_play(state->action->sfx, get_next_sfx_channel());
    }
    else if (state->action->type == ACTION_START_XM64) {
      xm64player_play(state->action->xm64, state->action->first_channel);
    }

    state->action++;
    state->time = 0.f;
  }

  return false;
}

rspq_block_t *build_empty_hud_block() {
  const color_t LINE_COLOR = RGBA32(0x00, 0x00, 0x00, 0xff);
  const color_t BAR_BG_COLOR = RGBA32(0x00, 0xc9, 0xff, 0xff);
  static const char *const TITLES[] = {
    SW_PLAYER1_S "P1",
    SW_PLAYER2_S "P2",
    SW_PLAYER3_S "P3",
    SW_PLAYER4_S "P4",
  };

  rspq_block_begin();
  rdpq_mode_push();
  rdpq_mode_zbuf(false, false);
  for (size_t i = 0; i < 4; i++) {
    int y = HUD_VERTICAL_BORDER;
    int x = HUD_HORIZONTAL_BORDER + i*HUD_INDIVIDUAL_H_SPACE;
    int mid_x = x + HUD_INDIVIDUAL_H_SPACE/2;

    rdpq_text_print(NULL, FONT_NORMAL, mid_x-4, y, TITLES[i]);

    x += HUD_BAR_X_OFFSET;
    y += HUD_BAR_Y_OFFSET;
    int w = HUD_INDIVIDUAL_H_SPACE - HUD_BAR_X_OFFSET*2;
    int h = HUD_BAR_HEIGHT;
    rdpq_set_mode_fill(LINE_COLOR);
    rdpq_fill_rectangle(x, y, x+w, y+1);
    rdpq_fill_rectangle(x, y, x+1, y+h);
    rdpq_fill_rectangle(x, y+h-1, x+w, y+h);
    rdpq_fill_rectangle(x+w-1, y, x+w, y+h);
    rdpq_set_mode_fill(BAR_BG_COLOR);
    rdpq_fill_rectangle(x+1, y+1, x+w-1, y+h-1);
  }
  rdpq_mode_pop();

  return rspq_block_end();
}

void draw_hud() {
  const color_t BAR_COLOR = RGBA32(0xff, 0x45, 0x00, 0xff);

  rspq_block_run(empty_hud_block);

  rdpq_mode_push();
  rdpq_mode_zbuf(false, false);
  for (size_t i = 0; i < 4; i++) {
    int y = HUD_VERTICAL_BORDER;
    int x = HUD_HORIZONTAL_BORDER + i*HUD_INDIVIDUAL_H_SPACE;
    int mid_x = x + HUD_INDIVIDUAL_H_SPACE/2;

    x += HUD_BAR_X_OFFSET + 1;
    y += HUD_BAR_Y_OFFSET + 1;
    int max_w = HUD_INDIVIDUAL_H_SPACE - HUD_BAR_X_OFFSET*2 - 2;
    int h = HUD_BAR_HEIGHT - 2;;
    int w = (int) roundf((float) max_w * players[i].temperature);
    if (w > max_w) {
      w = max_w;
    }
    rdpq_set_mode_fill(BAR_COLOR);
    rdpq_fill_rectangle(x, y, x+w, y+h);

    if (players[i].out) {
      rdpq_text_print(NULL, FONT_NORMAL, mid_x-8, y+10, SW_OUT_S "OUT");
    }
  }
  rdpq_mode_pop();
}

void particle_source_pre_init(struct particle_source *source) {
  source->_particles = NULL;
  source->_transform = NULL;
  source->_in_use = false;
  source->_meta = NULL;
  source->_type = UNDEFINED;
}

struct particle_source *particle_source_get_unused(size_t num_particles) {
  for (size_t i = 0; i < MAX_PARTICLE_SOURCES; i++) {
    if (!particle_sources[i]._in_use) {
      particle_sources[i]._in_use = true;
      particle_sources[i]._num_allocated_particles = num_particles;
      particle_sources[i]._particles = malloc_uncached(
          sizeof(TPXParticle) * num_particles/2);
      particle_sources[i]._meta = malloc(
          sizeof(struct particle_meta) * num_particles);
      particle_sources[i]._transform = malloc_uncached(sizeof(T3DMat4FP));
      return &particle_sources[i];
    }
  }

  return NULL;
}

void particle_source_reset_steam(struct particle_source *source) {
  for (size_t i = 0; i < source->_num_allocated_particles/2; i++) {
      source->_particles[i].sizeA = 0;
      source->_particles[i].sizeB = 0;
  }
  source->_y_move_error = 0.f;
  source->_to_spawn = 0;
}

void particle_source_init_steam(struct particle_source *source) {
  particle_source_reset_steam(source);
  for (size_t i = 0; i < source->_num_allocated_particles/2; i++) {
      source->_particles[i].colorA[0] = 0xff;
      source->_particles[i].colorA[1] = 0xff;
      source->_particles[i].colorA[2] = 0xff;
      source->_particles[i].colorA[3] = 0xff;

      source->_particles[i].colorB[0] = 0xff;
      source->_particles[i].colorB[1] = 0xff;
      source->_particles[i].colorB[2] = 0xff;
      source->_particles[i].colorB[3] = 0xff;
  }
  source->max_particles = source->_num_allocated_particles;
  source->_type = STEAM;
}

void particle_source_free(struct particle_source *source) {
  source->_in_use = false;
  source->_num_allocated_particles = 0;
  source->_type = UNDEFINED;
  if (source->_particles) {
    free_uncached(source->_particles);
    source->_particles = NULL;
  }
  if (source->_transform) {
    free_uncached(source->_transform);
    source->_transform = NULL;
  }
  if (source->_meta) {
    free(source->_meta);
    source->_meta = NULL;
  }
}

static void particle_source_spawn_steam(struct particle_source *source,
    int8_t *pos, int8_t *size, struct particle_meta *meta) {
  int8_t cx = (rand() % (source->x_range*2+1)) - source->x_range;
  int8_t cz = (rand() % (source->z_range*2+1)) - source->z_range;
  meta->cx = cx;
  meta->cz = cz;

  float v = -128.f / T3D_PI;
  *size = source->particle_size;
  pos[0] = cx+sinf(v);
  pos[1] = -128;
  pos[2] = cz+cosf(v);
}

static void particle_source_iterate_steam(struct particle_source *source,
    float delta_time) {
  source->_y_move_error += ((float) source->height / source->time_to_rise)
    * delta_time;
  int y_move = (int) source->_y_move_error;
  source->_y_move_error -= (float) y_move;

  source->_to_spawn += ((float) source->max_particles / source->time_to_rise)
    * delta_time;
  int actual_to_spawn = (int) source->_to_spawn;
  int spawned = 0;

  TPXParticle *p = source->_particles;
  struct particle_meta *m = source->_meta;
  for (int i = 0; i < source->_num_allocated_particles/2; i++, p++, m++) {
    if (p->sizeA) {
      p->posA[1] += y_move;
      if ((int) p->posA[1] >= source->height - 128) {
        p->sizeA = 0;
      }
      else {
        p->posA[0] = m->cx
          + sinf((float) p->posA[1] / T3D_PI)
          * source->movement_amplitude;
        p->posA[2] = m->cz
          + cosf((float) p->posA[1] / T3D_PI)
          * source->movement_amplitude;
      }
    }
    if (!p->sizeA && actual_to_spawn) {
      actual_to_spawn--;
      spawned++;
      particle_source_spawn_steam(source, p->posA, &p->sizeA, m);
    }

    m++;
    if (p->sizeB) {
      p->posB[1] += y_move;
      if ((int) p->posB[1] >= source->height - 128) {
        p->sizeB = 0;
      }
      else {
        p->posB[0] = (float) m->cx
          + sinf((float) p->posB[1] / T3D_PI)
          * (float) source->movement_amplitude;
        p->posB[2] = (float) m->cz
          + cosf((float) p->posB[1] / T3D_PI)
          * (float) source->movement_amplitude;
      }
    }
    if (!p->sizeB && actual_to_spawn) {
      actual_to_spawn--;
      spawned++;
      particle_source_spawn_steam(source, p->posB, &p->sizeB, m);
    }
  }
  source->_to_spawn -= (float) spawned;

  t3d_mat4fp_from_srt_euler(source->_transform,
      (float[]) {source->scale, source->scale, source->scale},
      (float[]) {0.f, 0.f, 0.f},
      source->pos.v);
}

void particle_source_iterate(struct particle_source *source,
    float delta_time) {
  if (!source->_in_use || source->paused) {
    return;
  }
  switch (source->_type) {
    case STEAM:
      particle_source_iterate_steam(source, delta_time);
      break;
  }
}

void particle_source_draw(const struct particle_source *source) {
  if (source->_in_use && source->render) {
    tpx_matrix_push(source->_transform);
    tpx_particle_draw(source->_particles, source->_num_allocated_particles);
    tpx_matrix_pop(1);
  }
}

void particle_source_draw_all() {
  rdpq_sync_pipe();
  rdpq_sync_tile();

  rdpq_mode_push();
  rdpq_set_mode_standard();
  rdpq_mode_zbuf(true, true);
  rdpq_mode_zoverride(true, 0, 0);
  rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
  tpx_state_from_t3d();

  for (size_t i = 0; i < MAX_PARTICLE_SOURCES; i++) {
    particle_source_draw(&particle_sources[i]);
  }
  rdpq_mode_pop();
}