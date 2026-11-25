#ifndef DEVKITY_NATIVE_H
#define DEVKITY_NATIVE_H

#include <stddef.h>

#include "dk_types.h"

#define DK_NATIVE_ABI_VERSION 5u

#define DK_NATIVE_FEATURE_EXECUTION_SCAFFOLD 0x00000001u
#define DK_NATIVE_FEATURE_ROM_LOADING        0x00000002u
#define DK_NATIVE_FEATURE_FRAME_CLOCK        0x00000004u
#define DK_NATIVE_FEATURE_CONTROLLER_INPUT   0x00000008u
#define DK_NATIVE_FEATURE_GENEC_TARGET       0x00000010u
#define DK_NATIVE_FEATURE_MULTI_EXPORT       0x00000020u
#define DK_NATIVE_FEATURE_HEADER_PARSE       0x00000040u
#define DK_NATIVE_FEATURE_RUNTIME_SYNC       0x00000080u
#define DK_NATIVE_FEATURE_WRAM_MIRROR        0x00000100u
#define DK_NATIVE_FEATURE_ROM_MIRROR         0x00000200u
#define DK_NATIVE_FEATURE_BUS_READS          0x00000400u
#define DK_NATIVE_FEATURE_CPU_STATE_SHADOW   0x00000800u
#define DK_NATIVE_FEATURE_OPCODE_FETCH       0x00001000u
#define DK_NATIVE_FEATURE_EXECUTE_ASSIST     0x00002000u
#define DK_NATIVE_FEATURE_INTERRUPT_RUNNER   0x00004000u
#define DK_NATIVE_FEATURE_YM2612_BUS          0x00008000u
#define DK_NATIVE_FEATURE_Z80_SLICE           0x00010000u
#define DK_NATIVE_FEATURE_FRAME_EXECUTOR     0x00020000u

#define DK_NATIVE_FRAME_RUN_VBLANK           0x00000001u
#define DK_NATIVE_FRAME_RUN_Z80              0x00000002u
#define DK_NATIVE_FRAME_ADVANCE_CLOCK        0x00000004u
#define DK_NATIVE_FRAME_FORCE_VBLANK         0x00000008u
#define DK_NATIVE_FRAME_SKIP_MAIN            0x00000010u

#define DK_NATIVE_FRAME_INVALID              0u
#define DK_NATIVE_FRAME_COMPLETED            1u
#define DK_NATIVE_FRAME_MAIN_STOPPED         2u
#define DK_NATIVE_FRAME_MAIN_UNSUPPORTED     3u
#define DK_NATIVE_FRAME_VBLANK_FAILED        4u
#define DK_NATIVE_FRAME_Z80_STOPPED          5u

#define DK_NATIVE_INTERRUPT_INVALID       0u
#define DK_NATIVE_INTERRUPT_COMPLETED     1u
#define DK_NATIVE_INTERRUPT_MASKED        2u
#define DK_NATIVE_INTERRUPT_UNSUPPORTED   3u
#define DK_NATIVE_INTERRUPT_LIMIT         4u
#define DK_NATIVE_INTERRUPT_STOPPED       5u

#define DK_NATIVE_MAX_ROM_BYTES 0x00400000u
#define DK_NATIVE_WRAM_BYTES    0x00010000u
#define DK_NATIVE_VRAM_BYTES    0x00010000u
#define DK_NATIVE_IO_REG_BYTES  0x00000020u

#define DK_NATIVE_BUTTON_UP     0x0001u
#define DK_NATIVE_BUTTON_DOWN   0x0002u
#define DK_NATIVE_BUTTON_LEFT   0x0004u
#define DK_NATIVE_BUTTON_RIGHT  0x0008u
#define DK_NATIVE_BUTTON_A      0x0010u
#define DK_NATIVE_BUTTON_B      0x0020u
#define DK_NATIVE_BUTTON_C      0x0040u
#define DK_NATIVE_BUTTON_START  0x0080u

typedef struct dk_native_info_s {
    dk_u32 abi_version;
    dk_u32 feature_flags;
    dk_u32 max_rom_bytes;
    dk_u32 wram_bytes;
    dk_u32 vram_bytes;
    char name[32];
    char build_tag[32];
} dk_native_info_t;

typedef struct dk_native_interrupt_result_s {
    dk_u32 status;
    dk_u32 instructions;
    dk_u32 cycles;
    dk_u32 return_pc;
    dk_u32 return_sp;
    dk_u32 stop_pc;
    dk_u16 stop_opcode;
    dk_u16 reserved;
} dk_native_interrupt_result_t;

typedef struct dk_native_frame_result_s {
    dk_u32 status;
    dk_u32 main_cycles;
    dk_u32 vblank_status;
    dk_u32 vblank_instructions;
    dk_u32 vblank_cycles;
    dk_u32 z80_cycles;
    dk_u32 frame_count;
    dk_u32 stop_pc;
    dk_u16 stop_opcode;
    dk_u16 reserved;
} dk_native_frame_result_t;

#include "vdp.h"
#include "ym2612.h"
#include "z80.h"

typedef struct dk_native_system_s {
    dk_u32 abi_version;
    dk_u32 feature_flags;
    dk_u32 rom_len;
    dk_u32 master_clock;
    dk_u32 frame_count;
    dk_u32 button_mask;
    dk_u32 reset_sp;
    dk_u32 reset_pc;
    dk_u32 current_pc;
    dk_u32 last_error;
    dk_u32 last_illegal_pc;
    dk_u16 rom_checksum;
    dk_u16 current_sr;
    dk_u16 current_opcode;
    dk_u8 sega_header_valid;
    dk_u8 game_mode;
    dk_u8 display_enabled;
    dk_u8 stopped;
    dk_u8 current_opcode_valid;
    dk_u8 pending_irq;
    char last_error_text[96];
    dk_u32 usp;
    dk_u32 ssp;
    dk_u32 d_regs[8];
    dk_u32 a_regs[8];
    dk_u8 io_regs[DK_NATIVE_IO_REG_BYTES];
    dk_u8 rom[DK_NATIVE_MAX_ROM_BYTES];
    dk_u8 wram[DK_NATIVE_WRAM_BYTES];
    dk_vdp_t vdp;
    dk_ym2612_t ym2612;
    dk_z80_t z80;
} dk_native_system_t;

dk_u32 dk_native_abi_version(void);
size_t dk_native_system_size(void);
void dk_native_get_info(dk_native_info_t *out_info);
void dk_native_init(dk_native_system_t *state);
void dk_native_reset(dk_native_system_t *state);
int dk_native_load_rom(dk_native_system_t *state, const dk_u8 *rom_data, size_t rom_size);
void dk_native_set_buttons(dk_native_system_t *state, dk_u16 button_mask);
void dk_native_frame_advance(dk_native_system_t *state, dk_u32 frames);
void dk_native_sync_runtime(
    dk_native_system_t *state,
    dk_u32 master_clock,
    dk_u32 frame_count,
    dk_u32 current_pc,
    dk_u16 current_sr,
    dk_u8 game_mode,
    dk_u8 display_enabled,
    dk_u32 last_illegal_pc
);
void dk_native_sync_cpu_state(
    dk_native_system_t *state,
    const dk_u32 *d_regs,
    const dk_u32 *a_regs,
    dk_u32 usp,
    dk_u32 ssp,
    dk_u8 stopped
);
void dk_native_sync_wram_range(
    dk_native_system_t *state,
    dk_u32 offset,
    const dk_u8 *src,
    size_t length
);
void dk_native_build_sonic_sat(dk_native_system_t *state);
void dk_native_patch_rom_byte(dk_native_system_t *state, dk_u32 addr, dk_u8 value);
dk_u8 dk_native_read_wram_byte(const dk_native_system_t *state, dk_u32 offset);
dk_u8 dk_native_read_bus_byte(const dk_native_system_t *state, dk_u32 addr);
dk_u16 dk_native_read_bus_word(const dk_native_system_t *state, dk_u32 addr);
dk_u32 dk_native_read_bus_long(const dk_native_system_t *state, dk_u32 addr);
dk_u16 dk_native_fetch_current_opcode(dk_native_system_t *state);
int dk_native_try_execute_current(dk_native_system_t *state);
int dk_native_try_execute_slice(dk_native_system_t *state, dk_u32 max_cycles);
int dk_native_run_z80_slice(dk_native_system_t *state, dk_u32 max_cycles);
int dk_native_execute_frame(
    dk_native_system_t *state,
    dk_u32 main_cycles,
    dk_u32 z80_cycles,
    dk_u32 max_vblank_instructions,
    dk_u32 flags,
    dk_native_frame_result_t *out_result
);
int dk_native_run_interrupt(
    dk_native_system_t *state,
    dk_u8 level,
    dk_u32 max_instructions,
    dk_native_interrupt_result_t *out_result
);
const char *dk_native_error_text(const dk_native_system_t *state);

#endif
