#ifndef DEVKITY_SANDBOX_H
#define DEVKITY_SANDBOX_H

#include "dk_types.h"

#define DK_SANDBOX_MAX_OBJECTS 64u
#define DK_SANDBOX_BUTTON_UP 0x0001u
#define DK_SANDBOX_BUTTON_DOWN 0x0002u
#define DK_SANDBOX_BUTTON_LEFT 0x0004u
#define DK_SANDBOX_BUTTON_RIGHT 0x0008u
#define DK_SANDBOX_BUTTON_A 0x0010u
#define DK_SANDBOX_BUTTON_B 0x0020u
#define DK_SANDBOX_BUTTON_C 0x0040u
#define DK_SANDBOX_BUTTON_START 0x0080u
#define DK_SANDBOX_COLLIDER_BOX 0u
#define DK_SANDBOX_COLLIDER_CIRCLE 1u
#define DK_SANDBOX_OBJECT_TANGIBLE 0x01u
#define DK_SANDBOX_COLLISION_GROUND 0x01u
#define DK_SANDBOX_COLLISION_OBJECT 0x08u

typedef struct dk_sandbox_object_s {
    dk_u8 active;
    dk_u8 object_id;
    dk_u8 subtype;
    dk_u8 state;
    dk_u16 x;
    dk_u16 y;
    dk_u16 half_width;
    dk_u16 half_height;
    dk_u8 collider_type;
    dk_u8 flags;
    dk_u8 reserved0;
    dk_u8 reserved1;
} dk_sandbox_object_t;

typedef struct dk_sandbox_state_s {
    dk_u32 frame_count;
    dk_u16 buttons;
    dk_u8 debug_enabled;
    dk_u8 object_count;
    int x;
    int y;
    int vx;
    int vy;
    int x_subpixel;
    int y_subpixel;
    dk_u16 previous_buttons;
    dk_u16 ground_y;
    dk_u8 grounded;
    dk_u8 facing;
    dk_u8 rolling;
    dk_u8 jump_pressed;
    dk_u8 collision_flags;
    dk_u8 reserved;
    dk_sandbox_object_t objects[DK_SANDBOX_MAX_OBJECTS];
} dk_sandbox_state_t;

void dk_sandbox_init(dk_sandbox_state_t *state, dk_u16 x, dk_u16 y);
void dk_sandbox_set_buttons(dk_sandbox_state_t *state, dk_u16 buttons);
void dk_sandbox_set_debug(dk_sandbox_state_t *state, dk_u8 enabled);
void dk_sandbox_clear_objects(dk_sandbox_state_t *state);
int dk_sandbox_place_object(
    dk_sandbox_state_t *state,
    dk_u8 object_id,
    dk_u8 subtype,
    dk_u16 x,
    dk_u16 y
);
void dk_sandbox_frame(dk_sandbox_state_t *state);

#endif
