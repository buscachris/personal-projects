#include "sandbox.h"

#include <string.h>

static int dk_sandbox_clamp(int value, int low, int high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

static int dk_sandbox_abs(int value)
{
    return value < 0 ? -value : value;
}

static void dk_sandbox_collide_objects(dk_sandbox_state_t *state)
{
    dk_u32 index;
    state->collision_flags &= (dk_u8)~DK_SANDBOX_COLLISION_OBJECT;
    for (index = 0u; index < DK_SANDBOX_MAX_OBJECTS; ++index) {
        dk_sandbox_object_t *object = &state->objects[index];
        int dx;
        int dy;
        int radius;
        if (!object->active) {
            continue;
        }
        dx = state->x - (int)object->x;
        dy = state->y - (int)object->y;
        if (object->collider_type == DK_SANDBOX_COLLIDER_CIRCLE) {
            radius = (int)object->half_width + 8;
            if ((dx * dx) + (dy * dy) > radius * radius) {
                continue;
            }
            /* The sphere stores energy while blue. Contact arms it; the
             * next frame makes it tangible, matching the editor contract. */
            if (object->object_id == 0x19u && object->state == 0u) {
                object->state = 1u;
                continue;
            }
            if (!(object->flags & DK_SANDBOX_OBJECT_TANGIBLE) &&
                object->object_id != 0x19u) {
                continue;
            }
            if (dx == 0 && dy == 0) {
                dx = state->facing ? 1 : -1;
            }
            if (dk_sandbox_abs(dx) >= dk_sandbox_abs(dy)) {
                state->x = (int)object->x + (dx < 0 ? -radius : radius);
                state->x_subpixel = state->x << 4;
                state->vx = -state->vx / 2;
            } else {
                state->y = (int)object->y + (dy < 0 ? -radius : radius);
                state->y_subpixel = state->y << 4;
                state->vy = -state->vy / 2;
            }
            state->collision_flags |= DK_SANDBOX_COLLISION_OBJECT;
            continue;
        }
        if (dk_sandbox_abs(dx) > (int)object->half_width + 8 ||
            dk_sandbox_abs(dy) > (int)object->half_height + 16) {
            continue;
        }
        if (!(object->flags & DK_SANDBOX_OBJECT_TANGIBLE)) {
            continue;
        }
        if (dk_sandbox_abs(dx) >= dk_sandbox_abs(dy)) {
            state->x = (int)object->x + (dx < 0 ? -(int)object->half_width - 8 : (int)object->half_width + 8);
            state->x_subpixel = state->x << 4;
            state->vx = -state->vx / 2;
        } else {
            state->y = (int)object->y + (dy < 0 ? -(int)object->half_height - 16 : (int)object->half_height + 16);
            state->y_subpixel = state->y << 4;
            state->vy = -state->vy / 2;
        }
        state->collision_flags |= DK_SANDBOX_COLLISION_OBJECT;
    }
}

void dk_sandbox_init(dk_sandbox_state_t *state, dk_u16 x, dk_u16 y)
{
    if (state == 0) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->x = (int)x;
    state->y = (int)y;
    state->x_subpixel = state->x << 4;
    state->y_subpixel = state->y << 4;
    state->ground_y = 0x180u;
    state->grounded = state->y >= (int)state->ground_y ? 1u : 0u;
    state->facing = 1u;
    state->collision_flags = state->grounded ? 0x01u : 0u;
}

void dk_sandbox_set_buttons(dk_sandbox_state_t *state, dk_u16 buttons)
{
    if (state != 0) {
        state->buttons = buttons;
    }
}

void dk_sandbox_set_debug(dk_sandbox_state_t *state, dk_u8 enabled)
{
    if (state != 0) {
        state->debug_enabled = enabled ? 1u : 0u;
    }
}

void dk_sandbox_clear_objects(dk_sandbox_state_t *state)
{
    if (state == 0) {
        return;
    }
    memset(state->objects, 0, sizeof(state->objects));
    state->object_count = 0u;
}

int dk_sandbox_place_object(
    dk_sandbox_state_t *state,
    dk_u8 object_id,
    dk_u8 subtype,
    dk_u16 x,
    dk_u16 y
)
{
    dk_u32 index;
    if (state == 0) {
        return -1;
    }
    for (index = 0u; index < DK_SANDBOX_MAX_OBJECTS; ++index) {
        if (!state->objects[index].active) {
            state->objects[index].active = 1u;
            state->objects[index].object_id = object_id;
            state->objects[index].subtype = subtype;
            state->objects[index].state = 0u;
            state->objects[index].x = x;
            state->objects[index].y = y;
            state->objects[index].half_width = object_id == 0x19u ? 16u : 8u;
            state->objects[index].half_height = object_id == 0x19u ? 16u : 8u;
            state->objects[index].collider_type = object_id == 0x19u
                ? DK_SANDBOX_COLLIDER_CIRCLE : DK_SANDBOX_COLLIDER_BOX;
            state->objects[index].flags = object_id == 0x19u
                ? 0u : DK_SANDBOX_OBJECT_TANGIBLE;
            state->object_count += 1u;
            return (int)index;
        }
    }
    return -1;
}

void dk_sandbox_frame(dk_sandbox_state_t *state)
{
    int grounded;
    dk_u16 pressed;
    int speed;
    if (state == 0) {
        return;
    }
    pressed = (dk_u16)(state->buttons & (dk_u16)~state->previous_buttons);
    grounded = state->grounded != 0u;
    if (state->buttons & DK_SANDBOX_BUTTON_RIGHT) {
        state->vx = dk_sandbox_clamp(state->vx + 0x20, -0x400, 0x400);
        state->facing = 1u;
    } else if (state->buttons & DK_SANDBOX_BUTTON_LEFT) {
        state->vx = dk_sandbox_clamp(state->vx - 0x20, -0x400, 0x400);
        state->facing = 0u;
    } else {
        state->vx = (state->vx * 85) / 100;
    }
    speed = state->vx < 0 ? -state->vx : state->vx;
    if ((state->buttons & DK_SANDBOX_BUTTON_DOWN) && grounded && speed >= 0x80) {
        state->rolling = 1u;
    } else if (!(state->buttons & DK_SANDBOX_BUTTON_DOWN) && speed < 0x40) {
        state->rolling = 0u;
    }
    /* Genesis jump buttons are A/B/C.  Up remains a keyboard convenience,
     * but action buttons are ignored while the object cursor is active so
     * A/C/B retain the Sonic 1 debug object controls. */
    if ((pressed & (DK_SANDBOX_BUTTON_UP | DK_SANDBOX_BUTTON_A |
                   DK_SANDBOX_BUTTON_B | DK_SANDBOX_BUTTON_C)) &&
        (!state->debug_enabled || (pressed & DK_SANDBOX_BUTTON_UP)) && grounded) {
        state->vy = -0x300;
        state->grounded = 0u;
        state->collision_flags = 0u;
        state->jump_pressed = 1u;
    } else {
        state->jump_pressed = 0u;
    }
    if (!grounded || state->vy < 0) {
        state->vy = dk_sandbox_clamp(state->vy + 0x20, -0x400, 0x300);
    }
    state->x_subpixel = dk_sandbox_clamp(state->x_subpixel + state->vx, 0, 0xFFFF0);
    state->y_subpixel = dk_sandbox_clamp(state->y_subpixel + state->vy, 0, 0x0FFF0);
    state->x = state->x_subpixel >> 4;
    state->y = state->y_subpixel >> 4;
    if (state->y >= (int)state->ground_y && state->vy >= 0) {
        state->y = (int)state->ground_y;
        state->y_subpixel = state->y << 4;
        state->vy = 0;
        state->grounded = 1u;
        state->collision_flags = 0x01u;
    } else if (state->y < (int)state->ground_y) {
        state->grounded = 0u;
        state->collision_flags &= (dk_u8)~0x01u;
    }
    dk_sandbox_collide_objects(state);
    state->previous_buttons = state->buttons;
    state->frame_count += 1u;
}
