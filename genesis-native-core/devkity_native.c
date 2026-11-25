#include "devkity_native.h"

#include <string.h>

#define DK_NATIVE_MASTER_CYCLES_PER_FRAME 127840u

static void dk_native_write_bus_byte(
    dk_native_system_t *state,
    dk_u32 addr,
    dk_u8 value
);

static dk_u8 dk_native_z80_read_68k(void *context, dk_u32 addr)
{
    return dk_native_read_bus_byte((const dk_native_system_t *)context, addr);
}

static void dk_native_z80_write_68k(void *context, dk_u32 addr, dk_u8 value)
{
    dk_native_write_bus_byte((dk_native_system_t *)context, addr, value);
}

static void dk_native_wire_devices(dk_native_system_t *state)
{
    state->z80.read_68k = dk_native_z80_read_68k;
    state->z80.write_68k = dk_native_z80_write_68k;
    state->z80.bus_ctx = state;
    state->z80.ym = &state->ym2612;
}

static dk_u16 dk_native_read_be16(const dk_u8 *src)
{
    return (dk_u16)(((dk_u16)src[0] << 8) | (dk_u16)src[1]);
}

static dk_u32 dk_native_read_be32(const dk_u8 *src)
{
    return ((dk_u32)src[0] << 24)
        | ((dk_u32)src[1] << 16)
        | ((dk_u32)src[2] << 8)
        | (dk_u32)src[3];
}
/* Fast inline opcode fetch for the hot decode loop */
static dk_u16 dk_native_fetch_opcode_fast(const dk_native_system_t *state, dk_u32 pc)
{
    dk_u32 masked = pc & 0x00FFFFFFu;
    if (masked < state->rom_len - 1u) {
        const dk_u8 *p = &state->rom[masked];
        return (dk_u16)(((dk_u16)p[0] << 8) | (dk_u16)p[1]);
    }
    return dk_native_read_bus_word(state, pc);
}

/* Fast inline operand word fetch */



static void dk_native_copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t i;
    if (dst == 0 || dst_size == 0) {
        return;
    }
    if (src == 0) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1 < dst_size && src[i] != '\0'; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static dk_u32 dk_native_default_features(void)
{
    return DK_NATIVE_FEATURE_EXECUTION_SCAFFOLD
        | DK_NATIVE_FEATURE_ROM_LOADING
        | DK_NATIVE_FEATURE_FRAME_CLOCK
        | DK_NATIVE_FEATURE_CONTROLLER_INPUT
        | DK_NATIVE_FEATURE_GENEC_TARGET
        | DK_NATIVE_FEATURE_MULTI_EXPORT
        | DK_NATIVE_FEATURE_HEADER_PARSE
        | DK_NATIVE_FEATURE_RUNTIME_SYNC
        | DK_NATIVE_FEATURE_WRAM_MIRROR
        | DK_NATIVE_FEATURE_ROM_MIRROR
        | DK_NATIVE_FEATURE_BUS_READS
        | DK_NATIVE_FEATURE_CPU_STATE_SHADOW
        | DK_NATIVE_FEATURE_OPCODE_FETCH
        | DK_NATIVE_FEATURE_EXECUTE_ASSIST
        | DK_NATIVE_FEATURE_INTERRUPT_RUNNER
        | DK_NATIVE_FEATURE_YM2612_BUS
        | DK_NATIVE_FEATURE_Z80_SLICE
        | DK_NATIVE_FEATURE_FRAME_EXECUTOR;
}

static int dk_native_sign_extend8(dk_u8 value)
{
    return (value & 0x80u) ? ((int)value - 0x100) : (int)value;
}

static int dk_native_sign_extend16(dk_u16 value)
{
    return (value & 0x8000u) ? ((int)value - 0x10000) : (int)value;
}

static dk_u32 dk_native_postinc_bytes(int areg, int size_bytes)
{
    if (size_bytes == 1 && areg == 7) {
        return 2u;
    }
    return (dk_u32)size_bytes;
}

static int dk_native_ccr_n(const dk_native_system_t *state)
{
    return (state->current_sr >> 3) & 0x01;
}

static int dk_native_ccr_z(const dk_native_system_t *state)
{
    return (state->current_sr >> 2) & 0x01;
}

static int dk_native_ccr_v(const dk_native_system_t *state)
{
    return (state->current_sr >> 1) & 0x01;
}

static int dk_native_ccr_c(const dk_native_system_t *state)
{
    return state->current_sr & 0x01;
}

static dk_u8 dk_native_button_pressed(const dk_native_system_t *state, dk_u16 mask)
{
    if (state == 0) {
        return 0;
    }
    return (state->button_mask & (dk_u32)mask) ? 1u : 0u;
}

static dk_u8 dk_native_controller_read_byte(
    const dk_native_system_t *state,
    dk_u32 control_reg
)
{
    dk_u8 value = 0xFFu;
    int th_high = 1;
    if (state == 0) {
        return 0xFFu;
    }
    if (control_reg < DK_NATIVE_IO_REG_BYTES) {
        th_high = (state->io_regs[control_reg] & 0x40u) != 0u;
    }
    if (th_high) {
        value |= 0x40u;
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_UP)) {
            value &= (dk_u8)~0x01u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_DOWN)) {
            value &= (dk_u8)~0x02u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_LEFT)) {
            value &= (dk_u8)~0x04u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_RIGHT)) {
            value &= (dk_u8)~0x08u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_B)) {
            value &= (dk_u8)~0x10u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_C)) {
            value &= (dk_u8)~0x20u;
        }
    } else {
        value &= (dk_u8)~0x40u;
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_UP)) {
            value &= (dk_u8)~0x01u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_DOWN)) {
            value &= (dk_u8)~0x02u;
        }
        value &= (dk_u8)~0x0Cu;
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_A)) {
            value &= (dk_u8)~0x10u;
        }
        if (dk_native_button_pressed(state, DK_NATIVE_BUTTON_START)) {
            value &= (dk_u8)~0x20u;
        }
    }
    return value;
}

static dk_u32 dk_native_index_brief(
    const dk_native_system_t *state,
    dk_u32 base,
    dk_u16 ext
)
{
    dk_u32 index;
    int reg = (ext >> 12) & 0x7;
    int use_areg = (ext & 0x8000u) != 0u;
    int long_size = (ext & 0x0800u) != 0u;
    int disp = dk_native_sign_extend8((dk_u8)(ext & 0x00FFu));
    if (state == 0) {
        return base;
    }
    if (use_areg) {
        index = state->a_regs[reg];
    } else {
        index = state->d_regs[reg];
    }
    if (!long_size) {
        index = (dk_u32)dk_native_sign_extend16((dk_u16)(index & 0xFFFFu));
    }
    return (base + (dk_u32)disp + index) & 0x00FFFFFFu;
}

static dk_u32 dk_native_index_brief_full(
    const dk_native_system_t *state,
    dk_u32 base,
    dk_u16 ext
)
{
    dk_u32 index;
    int reg = (ext >> 12) & 0x7;
    int use_areg = (ext & 0x8000u) != 0u;
    int long_size = (ext & 0x0800u) != 0u;
    int disp = dk_native_sign_extend8((dk_u8)(ext & 0x00FFu));
    if (state == 0) {
        return base;
    }
    index = use_areg ? state->a_regs[reg] : state->d_regs[reg];
    if (!long_size) {
        index = (dk_u32)dk_native_sign_extend16((dk_u16)(index & 0xFFFFu));
    }
    return (base + (dk_u32)disp + index) & 0xFFFFFFFFu;
}

static int dk_native_read_mul_word_source(
    dk_native_system_t *state,
    dk_u32 pc,
    dk_u16 opcode,
    int *value_out,
    dk_u32 *next_pc_out
)
{
    int mode;
    int reg;
    dk_u32 addr;
    dk_u16 ext;
    int disp;
    dk_u16 raw;
    if (state == 0 || value_out == 0 || next_pc_out == 0) {
        return 0;
    }
    mode = (opcode >> 3) & 0x7u;
    reg = opcode & 0x7u;
    switch (mode) {
        case 0x0:
            raw = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
            *value_out = dk_native_sign_extend16(raw);
            *next_pc_out = (pc + 2u) & 0xFFFFFFFFu;
            return 1;
        case 0x2:
            addr = state->a_regs[reg] & 0x00FFFFFFu;
            raw = dk_native_read_bus_word(state, addr);
            *value_out = dk_native_sign_extend16(raw);
            *next_pc_out = (pc + 2u) & 0xFFFFFFFFu;
            return 1;
        case 0x3:
            addr = state->a_regs[reg] & 0x00FFFFFFu;
            raw = dk_native_read_bus_word(state, addr);
            state->a_regs[reg] = (state->a_regs[reg] + dk_native_postinc_bytes(reg, 2)) & 0xFFFFFFFFu;
            *value_out = dk_native_sign_extend16(raw);
            *next_pc_out = (pc + 2u) & 0xFFFFFFFFu;
            return 1;
        case 0x4:
            state->a_regs[reg] = (state->a_regs[reg] - 2u) & 0xFFFFFFFFu;
            addr = state->a_regs[reg] & 0x00FFFFFFu;
            raw = dk_native_read_bus_word(state, addr);
            *value_out = dk_native_sign_extend16(raw);
            *next_pc_out = (pc + 2u) & 0xFFFFFFFFu;
            return 1;
        case 0x5:
            disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
            raw = dk_native_read_bus_word(state, addr);
            *value_out = dk_native_sign_extend16(raw);
            *next_pc_out = (pc + 4u) & 0xFFFFFFFFu;
            return 1;
        case 0x6:
            ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[reg] & 0x00FFFFFFu, ext);
            raw = dk_native_read_bus_word(state, addr);
            *value_out = dk_native_sign_extend16(raw);
            *next_pc_out = (pc + 4u) & 0xFFFFFFFFu;
            return 1;
        case 0x7:
            switch (reg) {
                case 0x0:
                    addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
                    raw = dk_native_read_bus_word(state, addr);
                    *value_out = dk_native_sign_extend16(raw);
                    *next_pc_out = (pc + 4u) & 0xFFFFFFFFu;
                    return 1;
                case 0x1:
                    addr = dk_native_read_bus_long(state, pc + 2u);
                    raw = dk_native_read_bus_word(state, addr);
                    *value_out = dk_native_sign_extend16(raw);
                    *next_pc_out = (pc + 6u) & 0xFFFFFFFFu;
                    return 1;
                case 0x2:
                    disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
                    addr = ((pc + 2u) + (dk_u32)disp) & 0x00FFFFFFu;
                    raw = dk_native_read_bus_word(state, addr);
                    *value_out = dk_native_sign_extend16(raw);
                    *next_pc_out = (pc + 4u) & 0xFFFFFFFFu;
                    return 1;
                case 0x3:
                    ext = dk_native_read_bus_word(state, pc + 2u);
                    addr = dk_native_index_brief(state, (pc + 2u) & 0x00FFFFFFu, ext);
                    raw = dk_native_read_bus_word(state, addr);
                    *value_out = dk_native_sign_extend16(raw);
                    *next_pc_out = (pc + 4u) & 0xFFFFFFFFu;
                    return 1;
                case 0x4:
                    raw = dk_native_read_bus_word(state, pc + 2u);
                    *value_out = dk_native_sign_extend16(raw);
                    *next_pc_out = (pc + 4u) & 0xFFFFFFFFu;
                    return 1;
                default:
                    return 0;
            }
        default:
            return 0;
    }
}

static void dk_native_set_nzvc(
    dk_native_system_t *state,
    int n,
    int z,
    int v,
    int c
)
{
    dk_u16 sr;
    if (state == 0) {
        return;
    }
    sr = state->current_sr & (dk_u16)~0x000Fu;
    sr |= (dk_u16)(((n ? 1 : 0) << 3)
        | ((z ? 1 : 0) << 2)
        | ((v ? 1 : 0) << 1)
        | (c ? 1 : 0));
    state->current_sr = sr;
}

static void dk_native_set_add_flags(
    dk_native_system_t *state,
    dk_u32 src,
    dk_u32 dst,
    dk_u32 result,
    int bits
)
{
    dk_u32 mask = 0xFFFFFFFFu;
    dk_u32 sign = 0x80000000u;
    int carry;
    int overflow;
    if (bits == 8) {
        mask = 0xFFu;
        sign = 0x80u;
    } else if (bits == 16) {
        mask = 0xFFFFu;
        sign = 0x8000u;
    }
    src &= mask;
    dst &= mask;
    result &= mask;
    carry = result < dst;
    overflow = ((~(dst ^ src) & (dst ^ result) & sign) != 0u);
    dk_native_set_nzvc(state, (result & sign) != 0u, result == 0u, overflow, carry);
    if (carry) {
        state->current_sr |= 0x0010u;
    } else {
        state->current_sr &= (dk_u16)~0x0010u;
    }
}

static void dk_native_set_sub_flags(
    dk_native_system_t *state,
    dk_u32 src,
    dk_u32 dst,
    dk_u32 result,
    int bits
)
{
    dk_u32 mask = 0xFFFFFFFFu;
    dk_u32 sign = 0x80000000u;
    int carry;
    int overflow;
    if (bits == 8) {
        mask = 0xFFu;
        sign = 0x80u;
    } else if (bits == 16) {
        mask = 0xFFFFu;
        sign = 0x8000u;
    }
    src &= mask;
    dst &= mask;
    result &= mask;
    carry = src > dst;
    overflow = (((dst ^ src) & (dst ^ result) & sign) != 0u);
    dk_native_set_nzvc(state, (result & sign) != 0u, result == 0u, overflow, carry);
    if (carry) {
        state->current_sr |= 0x0010u;
    } else {
        state->current_sr &= (dk_u16)~0x0010u;
    }
}

static int dk_native_condition_true(const dk_native_system_t *state, dk_u16 opcode)
{
    int cc;
    int n;
    int z;
    int v;
    int c;
    cc = (opcode >> 8) & 0x0F;
    n = dk_native_ccr_n(state);
    z = dk_native_ccr_z(state);
    v = dk_native_ccr_v(state);
    c = dk_native_ccr_c(state);
    switch (cc) {
        case 0x0: return 1;
        case 0x1: return 0;
        case 0x2: return (!c) && (!z);
        case 0x3: return c || z;
        case 0x4: return !c;
        case 0x5: return c;
        case 0x6: return !z;
        case 0x7: return z;
        case 0x8: return !v;
        case 0x9: return v;
        case 0xA: return !n;
        case 0xB: return n;
        case 0xC: return n == v;
        case 0xD: return n != v;
        case 0xE: return (!z) && (n == v);
        case 0xF: return z || (n != v);
    }
    return 0;
}

/* The Python reference core intentionally uses a compact, approximate cycle
 * model. The fast native player must use that same model at frame boundaries
 * or both cores can pause at different instructions during long routines even
 * when every instruction has produced identical state. */
static int dk_native_reference_cycles(
    const dk_native_system_t *state,
    dk_u32 pc,
    dk_u16 opcode,
    int consumed
)
{
    int top = (opcode >> 12) & 0xFu;
    if (top == 0x1 || top == 0x2 || top == 0x3) {
        return 8;
    }
    if (top == 0x5 && (opcode & 0xF0F8u) != 0x50C8u) {
        return 8;
    }
    if (top == 0x6) {
        return (((opcode >> 8) & 0xFu) == 0x1u) ? 18 : 10;
    }
    if (top == 0x7) {
        return 4;
    }
    if (top == 0x9 || top == 0xD) {
        return 8;
    }
    if (top == 0xB) {
        int opmode = (opcode >> 6) & 0x7u;
        return (opmode <= 0x3 || opmode == 0x7) ? 6 : 8;
    }
    if (
        (opcode & 0xFB80u) == 0x4880u
        && ((opcode >> 3) & 0x7u) != 0x0u
    ) {
        dk_u16 mask = dk_native_read_bus_word(state, pc + 2u);
        int count = 0;
        int bit;
        for (bit = 0; bit < 16; ++bit) {
            if ((mask & (dk_u16)(1u << bit)) != 0u) {
                ++count;
            }
        }
        return 12 + (count * 4);
    }
    return consumed;
}

dk_u32 dk_native_abi_version(void)
{
    return DK_NATIVE_ABI_VERSION;
}

size_t dk_native_system_size(void)
{
    return sizeof(dk_native_system_t);
}

void dk_native_get_info(dk_native_info_t *out_info)
{
    if (out_info == 0) {
        return;
    }
    memset(out_info, 0, sizeof(*out_info));
    out_info->abi_version = DK_NATIVE_ABI_VERSION;
    out_info->feature_flags = dk_native_default_features();
    out_info->max_rom_bytes = DK_NATIVE_MAX_ROM_BYTES;
    out_info->wram_bytes = DK_NATIVE_WRAM_BYTES;
    out_info->vram_bytes = DK_NATIVE_VRAM_BYTES;
    dk_native_copy_text(out_info->name, sizeof(out_info->name), "sonickity-native");
    dk_native_copy_text(out_info->build_tag, sizeof(out_info->build_tag), "scaffold-v12");
}

void dk_native_init(dk_native_system_t *state)
{
    if (state == 0) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->abi_version = DK_NATIVE_ABI_VERSION;
    state->feature_flags = dk_native_default_features();
    dk_native_copy_text(
        state->last_error_text,
        sizeof(state->last_error_text),
        "ok"
    );
    state->rom_checksum = 0;
    state->sega_header_valid = 0;
    state->current_pc = 0;
    state->last_illegal_pc = 0;
    state->current_sr = 0;
    state->current_opcode = 0;
    state->game_mode = 0;
    state->display_enabled = 0;
    state->stopped = 0;
    state->current_opcode_valid = 0;
    state->usp = 0;
    state->ssp = 0;
    memset(state->d_regs, 0, sizeof(state->d_regs));
    memset(state->a_regs, 0, sizeof(state->a_regs));
    memset(state->io_regs, 0, sizeof(state->io_regs));
    memset(state->rom, 0, sizeof(state->rom));
    memset(state->wram, 0, sizeof(state->wram));
    dk_vdp_init(&state->vdp);
    dk_ym2612_init(&state->ym2612);
    dk_z80_init(&state->z80);
    dk_z80_reset(&state->z80);
    dk_native_wire_devices(state);
    state->io_regs[0x03u] = 0x40u;
    state->io_regs[0x05u] = 0x40u;
    state->io_regs[0x09u] = 0x40u;
    state->io_regs[0x0Bu] = 0x40u;
}

void dk_native_reset(dk_native_system_t *state)
{
    if (state == 0) {
        return;
    }
    state->master_clock = 0;
    state->frame_count = 0;
    state->button_mask = 0;
    state->reset_sp = 0;
    state->reset_pc = 0;
    state->current_pc = 0;
    state->last_error = 0;
    state->last_illegal_pc = 0;
    state->rom_checksum = 0;
    state->current_sr = 0;
    state->current_opcode = 0;
    state->sega_header_valid = 0;
    state->game_mode = 0;
    state->display_enabled = 0;
    state->stopped = 0;
    state->current_opcode_valid = 0;
    state->usp = 0;
    state->ssp = 0;
    memset(state->d_regs, 0, sizeof(state->d_regs));
    memset(state->a_regs, 0, sizeof(state->a_regs));
    memset(state->io_regs, 0, sizeof(state->io_regs));
    memset(state->rom, 0, sizeof(state->rom));
    memset(state->wram, 0, sizeof(state->wram));
    dk_vdp_init(&state->vdp);
    dk_ym2612_init(&state->ym2612);
    dk_z80_init(&state->z80);
    dk_z80_reset(&state->z80);
    dk_native_wire_devices(state);
    state->io_regs[0x03u] = 0x40u;
    state->io_regs[0x05u] = 0x40u;
    state->io_regs[0x09u] = 0x40u;
    state->io_regs[0x0Bu] = 0x40u;
    dk_native_copy_text(
        state->last_error_text,
        sizeof(state->last_error_text),
        "ok"
    );
}

int dk_native_load_rom(dk_native_system_t *state, const dk_u8 *rom_data, size_t rom_size)
{
    if (state == 0) {
        return 0;
    }
    if (rom_data == 0 || rom_size == 0) {
        state->rom_len = 0;
        state->last_error = 1;
        dk_native_copy_text(
            state->last_error_text,
            sizeof(state->last_error_text),
            "rom load failed: empty input"
        );
        return 0;
    }
    if (rom_size > DK_NATIVE_MAX_ROM_BYTES) {
        state->last_error = 2;
        dk_native_copy_text(
            state->last_error_text,
            sizeof(state->last_error_text),
            "rom load failed: exceeds 4MB Genesis limit"
        );
        return 0;
    }
    state->rom_len = (dk_u32)rom_size;
    state->reset_sp = 0;
    state->reset_pc = 0;
    state->current_pc = 0;
    state->rom_checksum = 0;
    state->sega_header_valid = 0;
    state->last_illegal_pc = 0;
    state->current_sr = 0;
    state->current_opcode = 0;
    state->game_mode = 0;
    state->display_enabled = 0;
    state->stopped = 0;
    state->current_opcode_valid = 0;
    state->usp = 0;
    state->ssp = 0;
    memset(state->d_regs, 0, sizeof(state->d_regs));
    memset(state->a_regs, 0, sizeof(state->a_regs));
    memset(state->io_regs, 0, sizeof(state->io_regs));
    memset(state->rom, 0, sizeof(state->rom));
    memset(state->wram, 0, sizeof(state->wram));
    dk_vdp_init(&state->vdp);
    dk_ym2612_init(&state->ym2612);
    dk_z80_init(&state->z80);
    dk_z80_reset(&state->z80);
    dk_native_wire_devices(state);
    state->io_regs[0x03u] = 0x40u;
    state->io_regs[0x05u] = 0x40u;
    state->io_regs[0x09u] = 0x40u;
    state->io_regs[0x0Bu] = 0x40u;
    memcpy(state->rom, rom_data, rom_size);
    if (rom_size >= 8) {
        state->reset_sp = dk_native_read_be32(rom_data);
        state->reset_pc = dk_native_read_be32(rom_data + 4);
    }
    if (rom_size >= 0x190u) {
        state->rom_checksum = dk_native_read_be16(rom_data + 0x18Eu);
    }
    if (rom_size >= 0x104u
        && rom_data[0x100u] == 'S'
        && rom_data[0x101u] == 'E'
        && rom_data[0x102u] == 'G'
        && rom_data[0x103u] == 'A') {
        state->sega_header_valid = 1;
    }
    state->last_error = 0;
    dk_native_copy_text(
        state->last_error_text,
        sizeof(state->last_error_text),
        "ok"
    );
    return 1;
}

void dk_native_set_buttons(dk_native_system_t *state, dk_u16 button_mask)
{
    if (state == 0) {
        return;
    }
    state->button_mask = (dk_u32)button_mask;
}

void dk_native_frame_advance(dk_native_system_t *state, dk_u32 frames)
{
    if (state == 0) {
        return;
    }
    state->frame_count += frames;
    state->master_clock += (frames * DK_NATIVE_MASTER_CYCLES_PER_FRAME);
    dk_vdp_vblank(&state->vdp);
    if (dk_vdp_get_vblank_irq(&state->vdp)) {
        state->pending_irq = 6u;
    }
    state->display_enabled = state->vdp.display_enabled;
}

void dk_native_sync_runtime(
    dk_native_system_t *state,
    dk_u32 master_clock,
    dk_u32 frame_count,
    dk_u32 current_pc,
    dk_u16 current_sr,
    dk_u8 game_mode,
    dk_u8 display_enabled,
    dk_u32 last_illegal_pc
)
{
    if (state == 0) {
        return;
    }
    state->master_clock = master_clock;
    state->frame_count = frame_count;
    state->current_pc = current_pc;
    state->current_sr = current_sr;
    state->game_mode = game_mode;
    state->display_enabled = display_enabled ? 1 : 0;
    state->last_illegal_pc = last_illegal_pc;
}

void dk_native_sync_cpu_state(
    dk_native_system_t *state,
    const dk_u32 *d_regs,
    const dk_u32 *a_regs,
    dk_u32 usp,
    dk_u32 ssp,
    dk_u8 stopped
)
{
    if (state == 0) {
        return;
    }
    if (d_regs != 0) {
        memcpy(state->d_regs, d_regs, sizeof(state->d_regs));
    }
    if (a_regs != 0) {
        memcpy(state->a_regs, a_regs, sizeof(state->a_regs));
    }
    state->usp = usp;
    state->ssp = ssp;
    state->stopped = stopped ? 1 : 0;
}

void dk_native_sync_pending_irq(
    dk_native_system_t *state,
    dk_u8 level
)
{
    if (state == 0) {
        return;
    }
    state->pending_irq = level;
}

void dk_native_sync_wram_range(
    dk_native_system_t *state,
    dk_u32 offset,
    const dk_u8 *src,
    size_t length
)
{
    size_t max_len;
    if (state == 0 || src == 0) {
        return;
    }
    if (offset >= DK_NATIVE_WRAM_BYTES) {
        return;
    }
    max_len = (size_t)(DK_NATIVE_WRAM_BYTES - offset);
    if (length > max_len) {
        length = max_len;
    }
    memcpy(state->wram + offset, src, length);
}

static int dk_native_sat_signed_word(dk_u16 value)
{
    if ((value & 0x8000u) != 0u) {
        return (int)value - 0x10000;
    }
    return (int)value;
}

static void dk_native_sat_write_word(dk_native_system_t *state, dk_u16 addr, dk_u16 value)
{
    dk_u16 masked = (dk_u16)(addr & 0xFFFEu);
    state->vdp.vram[masked] = (dk_u8)((value >> 8) & 0xFFu);
    state->vdp.vram[(dk_u16)(masked + 1u)] = (dk_u8)(value & 0xFFu);
}

/*
 * Build the hardware sprite table from the live S1 object table.  This is a
 * deliberately small C counterpart to the retail BuildSprites routine.  The
 * earlier helper rendered only Sonic's four pieces, then cut the SAT linked
 * list there; that made the fast player look as though HUD, rings, enemies,
 * and platforms had disappeared.  Every regular S1 object supplies the same
 * obMap/obFrame/obGfx data, so one faithful mapping pass is both safer and
 * closer to the original engine.
 */
void dk_native_build_sonic_sat(dk_native_system_t *state)
{
    const dk_u32 object_base = 0xFFFFD000u;
    const dk_u32 object_size = 0x40u;
    const dk_u32 object_count = 128u;
    const dk_u32 sat_base = 0xF800u;
    const dk_u32 sat_limit = 80u;
    dk_u32 slot;
    dk_u32 sprite_count = 0u;
    int camera_x;
    int camera_y;

    if (state == 0) {
        return;
    }

    /* A stale SAT link is enough to display arbitrary old VRAM.  Clear the
     * complete 80-entry table before rebuilding the current frame. */
    memset(state->vdp.vram + sat_base, 0, sat_limit * 8u);
    camera_x = dk_native_sat_signed_word(
        dk_native_read_bus_word(state, 0xFFFFF700u)
    );
    camera_y = dk_native_sat_signed_word(
        dk_native_read_bus_word(state, 0xFFFFF704u)
    );

    for (slot = 0u; slot < object_count && sprite_count < sat_limit; ++slot) {
        dk_u32 object = object_base + slot * object_size;
        dk_u8 object_id = dk_native_read_bus_byte(state, object);
        dk_u8 raw_render_flags;
        dk_u8 render_flags;
        dk_u8 frame;
        dk_u32 map_base;
        dk_u32 map_entry;
        dk_u8 piece_count;
        dk_u16 object_gfx;
        int origin_x;
        int origin_y;
        int flip_x;
        int flip_y;
        dk_u32 piece_index;

        if (object_id == 0u) {
            continue;
        }

        /*
         * The object table retains dormant/queued records.  Retail
         * BuildSprites marks only the records selected for this frame with
         * bit 7 of obRender; drawing every non-zero record made those stale
         * mappings land over the HUD.  Sonic and the screen-space HUD are
         * explicit bootstrap objects, so preserve them even before their
         * first display pass.
         */
        raw_render_flags = dk_native_read_bus_byte(state, object + 1u);
        if (slot != 0u && slot != 1u && (raw_render_flags & 0x80u) == 0u) {
            continue;
        }
        render_flags = raw_render_flags & 0x7Fu;
        map_base = dk_native_read_bus_long(state, object + 4u) & 0x00FFFFFFu;
        if (map_base >= state->rom_len) {
            continue;
        }
        frame = dk_native_read_bus_byte(state, object + 0x1Au);
        if ((render_flags & 0x20u) != 0u) {
            /* Raw mappings have a header at obMap itself. */
            map_entry = map_base;
        } else {
            if (map_base + ((dk_u32)frame * 2u) + 1u >= state->rom_len) {
                continue;
            }
            map_entry = map_base + (dk_u32)dk_native_read_bus_word(
                state, map_base + ((dk_u32)frame * 2u)
            );
        }
        if (map_entry >= state->rom_len) {
            continue;
        }
        piece_count = dk_native_read_bus_byte(state, map_entry);
        if (piece_count == 0u || piece_count > 16u
            || map_entry + 1u + ((dk_u32)piece_count * 5u) > state->rom_len) {
            continue;
        }

        if ((render_flags & 0x0Cu) != 0u) {
            int world_x = dk_native_sat_signed_word(
                dk_native_read_bus_word(state, object + 8u)
            );
            int world_y = dk_native_sat_signed_word(
                dk_native_read_bus_word(state, object + 12u)
            );
            int x_on_screen = world_x - camera_x;
            int y_on_screen = world_y - camera_y;
            int radius_x = (int)dk_native_read_bus_byte(state, object + 0x19u);
            int radius_y = (int)dk_native_read_bus_byte(state, object + 0x16u);

            /* Match BuildSprites' broad on-screen test before it consumes
             * one of the Genesis' 80 SAT entries. */
            if (x_on_screen + radius_x < -64 || x_on_screen - radius_x >= 384
                || y_on_screen + radius_y < -64 || y_on_screen - radius_y >= 288) {
                continue;
            }
            origin_x = x_on_screen + 128;
            origin_y = y_on_screen + 128;
        } else {
            /* HUD and other fixed-screen objects already use Genesis sprite
             * coordinates, including the hardware's +128 vertical bias. */
            origin_x = dk_native_sat_signed_word(
                dk_native_read_bus_word(state, object + 8u)
            );
            origin_y = dk_native_sat_signed_word(
                dk_native_read_bus_word(state, object + 10u)
            );
        }

        object_gfx = dk_native_read_bus_word(state, object + 2u);
        flip_x = (render_flags & 0x01u) != 0u;
        flip_y = (render_flags & 0x02u) != 0u;

        for (piece_index = 0u;
             piece_index < (dk_u32)piece_count && sprite_count < sat_limit;
             ++piece_index) {
            dk_u32 piece = map_entry + 1u + piece_index * 5u;
            int map_x = dk_native_sign_extend8(
                dk_native_read_bus_byte(state, piece + 4u)
            );
            int map_y = dk_native_sign_extend8(
                dk_native_read_bus_byte(state, piece)
            );
            dk_u8 size = dk_native_read_bus_byte(state, piece + 1u);
            dk_u16 tile = dk_native_read_bus_word(state, piece + 2u);
            int width = (((int)size >> 2) & 0x03) + 1;
            int height = ((int)size & 0x03) + 1;
            int sprite_x = origin_x + map_x;
            int sprite_y = origin_y + map_y;
            dk_u16 offset;

            if (flip_x) {
                tile ^= 0x0800u;
                sprite_x = origin_x - map_x - width * 8;
            }
            if (flip_y) {
                tile ^= 0x1000u;
                sprite_y = origin_y - map_y - height * 8;
            }

            offset = (dk_u16)(sat_base + sprite_count * 8u);
            dk_native_sat_write_word(state, offset, (dk_u16)sprite_y);
            dk_native_sat_write_word(
                state,
                (dk_u16)(offset + 2u),
                (dk_u16)(((dk_u16)size << 8) | ((sprite_count + 1u) & 0xFFu))
            );
            dk_native_sat_write_word(
                state, (dk_u16)(offset + 4u), (dk_u16)(tile + object_gfx)
            );
            dk_native_sat_write_word(state, (dk_u16)(offset + 6u), (dk_u16)sprite_x);
            ++sprite_count;
        }
    }

    if (sprite_count > 0u) {
        dk_u16 final_link = (dk_u16)(sat_base + (sprite_count - 1u) * 8u + 2u);
        dk_u16 size_and_link = (dk_u16)(
            ((dk_u16)state->vdp.vram[final_link] << 8)
            | (dk_u16)state->vdp.vram[(dk_u16)(final_link + 1u)]
        );
        dk_native_sat_write_word(state, final_link, (dk_u16)(size_and_link & 0xFF00u));
    }
}

void dk_native_patch_rom_byte(dk_native_system_t *state, dk_u32 addr, dk_u8 value)
{
    if (state == 0) {
        return;
    }
    if (addr >= state->rom_len || addr >= DK_NATIVE_MAX_ROM_BYTES) {
        return;
    }
    state->rom[addr] = value;
}

dk_u8 dk_native_read_wram_byte(const dk_native_system_t *state, dk_u32 offset)
{
    if (state == 0 || offset >= DK_NATIVE_WRAM_BYTES) {
        return 0;
    }
    return state->wram[offset];
}

dk_u8 dk_native_read_bus_byte(const dk_native_system_t *state, dk_u32 addr)
{
    if (state == 0) {
        return 0;
    }
    addr &= 0x00FFFFFFu;
    if (addr < state->rom_len) {
        return state->rom[addr];
    }
    if (addr < DK_NATIVE_MAX_ROM_BYTES) {
        return state->rom_len > 0u ? state->rom[addr % state->rom_len] : 0u;
    }
    if (addr >= 0x00A00000u && addr <= 0x00A01FFFu) {
        return dk_z80_read_bus(
            &((dk_native_system_t *)state)->z80,
            (dk_u16)(addr - 0x00A00000u)
        );
    }
    if (addr >= 0x00A04000u && addr <= 0x00A04003u) {
        dk_native_system_t *mutable_state = (dk_native_system_t *)state;
        int bank = (addr >= 0x00A04002u) ? 1 : 0;
        dk_ym2612_update(&mutable_state->ym2612, 8);
        return dk_ym2612_read(&mutable_state->ym2612, bank);
    }
    if (addr == 0x00A11100u || addr == 0x00A11101u) {
        /* The current bus arbiter grants a 68K request immediately, matching
         * core/bus.py: a granted request reads back with bit 0 clear. */
        return 0u;
    }
    if (addr == 0x00A11200u || addr == 0x00A11201u) {
        return state->z80.reset_flag;
    }
    if (addr >= 0x00A10000u && addr <= 0x00A1001Fu) {
        dk_u32 offset = addr - 0x00A10000u;
        if (offset == 0x03u) {
            return dk_native_controller_read_byte(state, 0x09u);
        }
        if (offset == 0x05u) {
            return dk_native_controller_read_byte(state, 0x0Bu);
        }
        if (offset < DK_NATIVE_IO_REG_BYTES) {
            return state->io_regs[offset];
        }
        return 0;
    }
    if (addr >= 0x00C00000u && addr <= 0x00C0001Fu) {
        dk_u32 port = addr & 0x1Eu;
        dk_u16 value;
        if (port == 0x00u || port == 0x02u) {
            value = dk_vdp_read_data(&((dk_native_system_t *)state)->vdp);
            return (dk_u8)((value >> ((addr & 1u) ? 0 : 8)) & 0xFFu);
        }
        if (port == 0x04u || port == 0x06u) {
            value = (dk_u16)dk_vdp_read_status(
                &((dk_native_system_t *)state)->vdp
            );
            return (dk_u8)((value >> ((addr & 1u) ? 0 : 8)) & 0xFFu);
        }
        if (
            port == 0x08u || port == 0x0Au
            || port == 0x0Cu || port == 0x0Eu
        ) {
            value = (dk_u16)(
                (state->vdp.scanline << 8) | (state->vdp.hcounter & 0xFFu)
            );
            return (dk_u8)((value >> ((addr & 1u) ? 0 : 8)) & 0xFFu);
        }
        return 0u;
    }
    if (addr >= 0x00FF0000u) {
        return state->wram[addr & 0xFFFFu];
    }
    return 0xFFu;
}

dk_u16 dk_native_read_bus_word(const dk_native_system_t *state, dk_u32 addr)
{
    dk_u32 masked = addr & 0x00FFFFFFu;
    /* Fast path: WRAM word read (big-endian, no alignment requirement on 68K) */
    if (masked >= 0x00FF0000u) {
        const dk_u8 *p = &state->wram[masked & 0xFFFFu];
        return (dk_u16)(((dk_u16)p[0] << 8) | (dk_u16)p[1]);
    }
    /* Fast path: ROM word read */
    if (masked < state->rom_len - 1u) {
        const dk_u8 *p = &state->rom[masked];
        return (dk_u16)(((dk_u16)p[0] << 8) | (dk_u16)p[1]);
    }
    if (masked >= 0x00C00000u && masked <= 0x00C00003u) {
        return dk_vdp_read_data(&((dk_native_system_t *)state)->vdp);
    }
    if (masked >= 0x00C00004u && masked <= 0x00C00007u) {
        return (dk_u16)dk_vdp_read_status(&((dk_native_system_t *)state)->vdp);
    }
    {
        dk_u16 hi = dk_native_read_bus_byte(state, addr);
        dk_u16 lo = dk_native_read_bus_byte(state, addr + 1u);
        return (dk_u16)((hi << 8) | lo);
    }
}

dk_u32 dk_native_read_bus_long(const dk_native_system_t *state, dk_u32 addr)
{
    dk_u32 masked = addr & 0x00FFFFFFu;
    /* Fast path: WRAM long read */
    if (masked >= 0x00FF0000u && masked <= 0x00FFFFFdu) {
        const dk_u8 *p = &state->wram[masked & 0xFFFFu];
        return ((dk_u32)p[0] << 24) | ((dk_u32)p[1] << 16)
             | ((dk_u32)p[2] << 8)  |  (dk_u32)p[3];
    }
    /* Fast path: ROM long read */
    if (masked < state->rom_len - 3u) {
        const dk_u8 *p = &state->rom[masked];
        return ((dk_u32)p[0] << 24) | ((dk_u32)p[1] << 16)
             | ((dk_u32)p[2] << 8)  |  (dk_u32)p[3];
    }
    {
        dk_u32 upper = dk_native_read_bus_word(state, addr);
        dk_u32 lower = dk_native_read_bus_word(state, addr + 2u);
        return (upper << 16) | lower;
    }
}

static void dk_native_write_bus_byte(dk_native_system_t *state, dk_u32 addr, dk_u8 value)
{
    if (state == 0) {
        return;
    }
    addr &= 0x00FFFFFFu;
    /* Fast path: WRAM write (most common write target in Sonic) */
    if (addr >= 0x00FF0000u) {
        state->wram[addr & 0xFFFFu] = value;
        return;
    }
    if (addr >= 0x00A00000u && addr <= 0x00A01FFFu) {
        dk_z80_write_bus(
            &state->z80,
            (dk_u16)(addr - 0x00A00000u),
            value
        );
        return;
    }
    if (addr >= 0x00A04000u && addr <= 0x00A04003u) {
        int bank = (addr >= 0x00A04002u) ? 1 : 0;
        if ((addr & 1u) == 0u) {
            dk_ym2612_write_addr(&state->ym2612, bank, value);
        } else {
            dk_ym2612_write(&state->ym2612, bank, value);
        }
        return;
    }
    if (addr == 0x00A11100u || addr == 0x00A11101u) {
        state->io_regs[0x1Cu] = value & 1u;
        dk_z80_set_busreq(&state->z80, value);
        return;
    }
    if (addr == 0x00A11200u || addr == 0x00A11201u) {
        state->io_regs[0x1Du] = value & 1u;
        dk_z80_set_reset(&state->z80, value);
        return;
    }
    if (addr >= 0x00A10000u && addr <= 0x00A1001Fu) {
        dk_u32 offset = addr - 0x00A10000u;
        if (offset < DK_NATIVE_IO_REG_BYTES) {
            state->io_regs[offset] = value;
        }
        return;
    }
    if (addr >= 0x00FF0000u) {
        state->wram[addr & 0xFFFFu] = value;
    }
}

static dk_u16 dk_native_dma_read_word(void *context, dk_u32 addr)
{
    return dk_native_read_bus_word((const dk_native_system_t *)context, addr);
}

static void dk_native_write_bus_word(dk_native_system_t *state, dk_u32 addr, dk_u16 value)
{
    dk_u32 masked = addr & 0x00FFFFFFu;
    if (masked >= 0x00C00000u && masked <= 0x00C00003u) {
        dk_vdp_write_data(&state->vdp, value);
        return;
    }
    if (masked >= 0x00C00004u && masked <= 0x00C00007u) {
        dk_vdp_write_control(&state->vdp, value);
        if (state->vdp.dma_pending && state->vdp.dma_mode != DK_DMA_MODE_VRAM_FILL) {
            dk_vdp_dma_execute(
                &state->vdp,
                dk_native_dma_read_word,
                state
            );
        }
        return;
    }
    if (masked == 0x00A11100u) {
        dk_u8 bus_value = (dk_u8)((value >> 8) & 0xFFu);
        state->io_regs[0x1Cu] = bus_value & 1u;
        dk_z80_set_busreq(&state->z80, bus_value);
        return;
    }
    if (masked == 0x00A11200u) {
        dk_u8 reset_value = (dk_u8)((value >> 8) & 0xFFu);
        state->io_regs[0x1Du] = reset_value & 1u;
        dk_z80_set_reset(&state->z80, reset_value);
        return;
    }
    dk_native_write_bus_byte(state, addr, (dk_u8)((value >> 8) & 0xFFu));
    dk_native_write_bus_byte(state, addr + 1u, (dk_u8)(value & 0xFFu));
}

static void dk_native_write_bus_long(dk_native_system_t *state, dk_u32 addr, dk_u32 value)
{
    dk_native_write_bus_word(state, addr, (dk_u16)((value >> 16) & 0xFFFFu));
    dk_native_write_bus_word(state, addr + 2u, (dk_u16)(value & 0xFFFFu));
}

static int dk_native_should_service_interrupt(const dk_native_system_t *state)
{
    if (state == 0) {
        return 0;
    }
    return state->pending_irq > ((state->current_sr >> 8) & 0x07u);
}

static void dk_native_push_word(dk_native_system_t *state, dk_u16 value)
{
    dk_u32 sp = (state->a_regs[7] - 2u) & 0xFFFFFFFFu;
    state->a_regs[7] = sp;
    dk_native_write_bus_word(state, sp, value);
}

static void dk_native_push_long(dk_native_system_t *state, dk_u32 value)
{
    dk_u32 sp = (state->a_regs[7] - 4u) & 0xFFFFFFFFu;
    state->a_regs[7] = sp;
    dk_native_write_bus_long(state, sp, value);
}

static int dk_native_service_interrupt(dk_native_system_t *state, dk_u8 level)
{
    dk_u32 vector;
    dk_u32 vector_addr;
    if (state == 0) {
        return 0;
    }
    vector = 24u + (dk_u32)(level & 0x07u);
    state->pending_irq = 0u;
    state->stopped = 0u;
    if ((state->current_sr & 0x2000u) == 0u) {
        state->usp = state->a_regs[7];
        state->a_regs[7] = state->ssp;
    }
    /* The original 68000 interrupt frame is six bytes: saved SR followed
     * by saved PC.  Later 68K family members add a format/vector word, but
     * the Genesis CPU does not.  Push PC first so the final stack pointer
     * addresses SR, exactly as RTE expects below. */
    dk_native_push_long(state, state->current_pc);
    dk_native_push_word(state, state->current_sr);
    state->ssp = state->a_regs[7];
    state->current_sr = (dk_u16)((state->current_sr & (dk_u16)~0x0700u) | ((level & 0x07u) << 8) | 0x2000u);
    vector_addr = vector * 4u;
    state->current_pc = dk_native_read_bus_long(state, vector_addr) & 0xFFFFFFFFu;
    return 44;
}

dk_u16 dk_native_fetch_current_opcode(dk_native_system_t *state)
{
    dk_u32 pc;
    if (state == 0) {
        return 0;
    }
    pc = state->current_pc & 0x00FFFFFFu;
    state->current_opcode = dk_native_read_bus_word(state, pc);
    state->current_opcode_valid = 0;
    if (pc < state->rom_len) {
        state->current_opcode_valid = 1;
    } else if (pc >= 0x00FF0000u) {
        state->current_opcode_valid = 1;
    }
    return state->current_opcode;
}

int dk_native_try_execute_current(dk_native_system_t *state)
{
    dk_u16 opcode;
    dk_u32 pc;
    if (state == 0) {
        return 0;
    }
    if (dk_native_should_service_interrupt(state)) {
        return dk_native_service_interrupt(state, (dk_u8)state->pending_irq);
    }
    if (state->stopped) {
        return 0;
    }
    pc = state->current_pc & 0x00FFFFFFu;
    opcode = dk_native_fetch_current_opcode(state);
    if (!state->current_opcode_valid) {
        return 0;
    }

    if (opcode == 0x4E71u) {
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* MOVE.W #imm, SR */
    if (opcode == 0x46FCu) {
        state->current_sr = dk_native_read_bus_word(state, pc + 2u);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    if (opcode == 0x6100u) {
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 return_pc = (pc + 4u) & 0xFFFFFFFFu;
        dk_u32 a7 = (state->a_regs[7] - 4u) & 0xFFFFFFFFu;
        state->a_regs[7] = a7;
        dk_native_write_bus_long(state, a7, return_pc);
        state->current_pc = (dk_u32)((int)pc + 2 + disp);
        return 18;
    }

    if (opcode == 0x4EB8u || opcode == 0x4EB9u || opcode == 0x4EBAu) {
        dk_u32 target;
        dk_u32 return_pc;
        dk_u32 a7;
        if (opcode == 0x4EB8u) {
            target = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            return_pc = (pc + 4u) & 0xFFFFFFFFu;
        } else if (opcode == 0x4EBAu) {
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            target = ((pc + 2u) + (dk_u32)disp) & 0x00FFFFFFu;
            return_pc = (pc + 4u) & 0xFFFFFFFFu;
        } else {
            target = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            return_pc = (pc + 6u) & 0xFFFFFFFFu;
        }
        a7 = (state->a_regs[7] - 4u) & 0xFFFFFFFFu;
        state->a_regs[7] = a7;
        dk_native_write_bus_long(state, a7, return_pc);
        state->current_pc = target & 0xFFFFFFFFu;
        return 18;
    }

    if (opcode == 0x4EBBu) {
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 target = dk_native_index_brief(state, (pc + 2u) & 0x00FFFFFFu, ext);
        dk_u32 return_pc = (pc + 4u) & 0xFFFFFFFFu;
        dk_u32 a7 = (state->a_regs[7] - 4u) & 0xFFFFFFFFu;
        state->a_regs[7] = a7;
        dk_native_write_bus_long(state, a7, return_pc);
        state->current_pc = target & 0xFFFFFFFFu;
        return 18;
    }

    if ((opcode & 0xFFF8u) == 0x4E90u) {
        int reg = opcode & 0x7u;
        dk_u32 target = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u32 return_pc = (pc + 2u) & 0xFFFFFFFFu;
        dk_u32 a7 = (state->a_regs[7] - 4u) & 0xFFFFFFFFu;
        state->a_regs[7] = a7;
        dk_native_write_bus_long(state, a7, return_pc);
        state->current_pc = target & 0xFFFFFFFFu;
        return 16;
    }

    if (opcode == 0x4EFBu) {
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 target = dk_native_index_brief(state, (pc + 2u) & 0x00FFFFFFu, ext);
        state->current_pc = target & 0xFFFFFFFFu;
        return 10;
    }

    /* JMP (xxx).L */
    if (opcode == 0x4EF9u) {
        state->current_pc = dk_native_read_bus_long(state, pc + 2u) & 0xFFFFFFFFu;
        return 12;
    }

    /* JMP (An) */
    if ((opcode & 0xFFF8u) == 0x4ED0u) {
        int reg = opcode & 0x7u;
        state->current_pc = state->a_regs[reg] & 0xFFFFFFFFu;
        return 12;
    }

    if (opcode == 0x4A38u) {
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = ((dk_u32)dk_native_sign_extend16(ext)) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if (opcode == 0x4A78u) {
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = ((dk_u32)dk_native_sign_extend16(ext)) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 4;
    }

    /* TST.L (xxx).W */
    if (opcode == 0x4AB8u) {
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, addr);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 4;
    }

    /* TST.L (xxx).L */
    if (opcode == 0x4AB9u) {
        dk_u32 addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, addr);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* TST.B/W (xxx).L */
    if (opcode == 0x4A39u) {
        dk_u32 addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if (opcode == 0x4A79u) {
        dk_u32 addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x4A28u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x4A68u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4AA8u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, addr);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x4A10u) {
        int reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* TST.W (An) */
    if ((opcode & 0xFFF8u) == 0x4A50u) {
        int reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* TST.L (An) */
    if ((opcode & 0xFFF8u) == 0x4A90u) {
        int reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, addr);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x4A00u) {
        int reg = opcode & 0x7u;
        dk_u8 value = (dk_u8)(state->d_regs[reg] & 0xFFu);
        dk_native_set_nzvc(
            state,
            (value & 0x80u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4A40u) {
        int reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4A80u) {
        int reg = opcode & 0x7u;
        dk_u32 value = state->d_regs[reg];
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* ASR.W d16(An) */
    if ((opcode & 0xFFF8u) == 0xE0E8u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        int last_out = (value & 0x0001u) != 0u;
        dk_u16 result = (dk_u16)(
            (value >> 1) | (value & 0x8000u)
        );
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_nzvc(
            state,
            (result & 0x8000u) != 0,
            result == 0,
            0,
            last_out
        );
        if (last_out) {
            state->current_sr |= 0x0010u;
        } else {
            state->current_sr &= (dk_u16)~0x0010u;
        }
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 16;
    }

    if ((opcode & 0xF1F8u) == 0xD168u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u16 src = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(dst + src);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_add_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.B Dn, d16(An) */
    if ((opcode & 0xF1F8u) == 0xD128u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 src = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(dst + src);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_add_flags(state, src, dst, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF0F8u) == 0x50C8u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u16 counter;
        if (dk_native_condition_true(state, opcode)) {
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 12;
        }
        counter = (dk_u16)((state->d_regs[reg] - 1u) & 0xFFFFu);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)counter;
        if (counter != 0xFFFFu) {
            state->current_pc = (dk_u32)((int)pc + 2 + disp);
            return 10;
        }
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 14;
    }

    if ((opcode & 0xF100u) == 0x7000u) {
        int reg = (opcode >> 9) & 0x7u;
        dk_u32 value = (dk_u32)dk_native_sign_extend8((dk_u8)(opcode & 0xFFu));
        state->d_regs[reg] = value;
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* NOT.B Dn */
    if ((opcode & 0xFFF8u) == 0x4600u) {
        int reg = opcode & 0x7u;
        dk_u8 value = (dk_u8)(state->d_regs[reg] & 0xFFu);
        dk_u8 result = (dk_u8)(~value);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)result;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* NOT.W Dn */
    if ((opcode & 0xFFF8u) == 0x4640u) {
        int reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)~(state->d_regs[reg] & 0xFFFFu);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1F8u) == 0x1030u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = dk_native_index_brief(state, state->a_regs[src] & 0x00FFFFFFu, ext);
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.B d8(An,Xn), (An) */
    if ((opcode & 0xF1F8u) == 0x10B0u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src] & 0x00FFFFFFu,
            ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        dk_native_write_bus_byte(
            state,
            state->a_regs[dst] & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 10;
    }

    /* MOVE.B (xxx).W, d16(An) */
    if ((opcode & 0xF1FFu) == 0x1178u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 src_addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 dst_addr = (
            state->a_regs[dst] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        dk_native_write_bus_byte(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.B (An), d16(An) */
    if ((opcode & 0xF1F8u) == 0x1150u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u32 dst_addr = (
            state->a_regs[dst] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        dk_native_write_bus_byte(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.B Dn, d16(An) */
    if ((opcode & 0xF1F8u) == 0x1140u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 dst_addr = (
            state->a_regs[dst] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 value = (dk_u8)(state->d_regs[src] & 0xFFu);
        dk_native_write_bus_byte(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.B (An)+, (An)+ */
    if ((opcode & 0xF1F8u) == 0x10D8u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        state->a_regs[src] = (
            state->a_regs[src] + dk_native_postinc_bytes(src, 1)
        ) & 0xFFFFFFFFu;
        dk_native_write_bus_byte(
            state,
            state->a_regs[dst] & 0x00FFFFFFu,
            value
        );
        state->a_regs[dst] = (
            state->a_regs[dst] + dk_native_postinc_bytes(dst, 1)
        ) & 0xFFFFFFFFu;
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.B (An)+, d16(An) */
    if ((opcode & 0xF1F8u) == 0x1158u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        state->a_regs[src] = (
            state->a_regs[src] + dk_native_postinc_bytes(src, 1)
        ) & 0xFFFFFFFFu;
        dk_native_write_bus_byte(
            state,
            (state->a_regs[dst] + (dk_u32)disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x80u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.B d16(An), d16(An) */
    if ((opcode & 0xF1F8u) == 0x1168u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int src_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u8 value = dk_native_read_bus_byte(
            state,
            (state->a_regs[src] + (dk_u32)src_disp) & 0x00FFFFFFu
        );
        dk_native_write_bus_byte(
            state,
            (state->a_regs[dst] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x80u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.B d8(An,Xn), d16(An) */
    if ((opcode & 0xF1F8u) == 0x1170u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 index_ext = dk_native_read_bus_word(state, pc + 2u);
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src] & 0x00FFFFFFu,
            index_ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        dk_native_write_bus_byte(
            state,
            (state->a_regs[dst] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xF000u) == 0x1000u && ((opcode >> 6) & 0x7u) == 0u) {
        int dst = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 addr;
        dk_u8 value;
        if (src_mode == 0) {
            value = (dk_u8)(state->d_regs[src] & 0xFFu);
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 4;
        } else if (src_mode == 5) {
            addr = (state->a_regs[src] + (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u))) & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            return 8;
        } else if (src_mode == 6) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[src] & 0x00FFFFFFu, ext);
            value = dk_native_read_bus_byte(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            return 10;
        } else if (src_mode == 2 || src_mode == 3) {
            addr = state->a_regs[src] & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            if (src_mode == 3) {
                state->a_regs[src] = (state->a_regs[src] + dk_native_postinc_bytes(src, 1)) & 0xFFFFFFFFu;
            }
        } else if (src_mode == 4) {
            state->a_regs[src] = (
                state->a_regs[src] - dk_native_postinc_bytes(src, 1)
            ) & 0xFFFFFFFFu;
            addr = state->a_regs[src] & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
        } else if (src_mode == 7 && src == 0) {
            addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            return 8;
        } else if (src_mode == 7 && src == 1) {
            addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            return 8;
        } else if (src_mode == 7 && src == 3) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(
                state,
                (pc + 2u) & 0x00FFFFFFu,
                ext
            );
            value = dk_native_read_bus_byte(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            return 10;
        } else if (src_mode == 7 && src == 4) {
            value = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            return 8;
        } else {
            return 0;
        }
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.B Dn, (An)+ and MOVE.B #imm, (An) */
    if ((opcode & 0xF000u) == 0x1000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int dst_mode = (opcode >> 6) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        if (src_mode == 0u && dst_mode == 3u) {
            dk_u8 value = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            state->a_regs[dst_reg] = (state->a_regs[dst_reg] + dk_native_postinc_bytes(dst_reg, 1)) & 0xFFFFFFFFu;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0u && dst_mode == 4u) {
            dk_u8 value = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
            dk_u32 addr = (state->a_regs[dst_reg] - dk_native_postinc_bytes(dst_reg, 1)) & 0xFFFFFFFFu;
            state->a_regs[dst_reg] = addr;
            dk_native_write_bus_byte(state, addr & 0x00FFFFFFu, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0u && dst_mode == 7u && dst_reg == 1u) {
            dk_u8 value = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
            dk_u32 addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 0u && dst_mode == 7u && dst_reg == 0u) {
            dk_u8 value = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
            dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 2u) {
            dk_u8 value = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 3u) {
            dk_u8 value = (dk_u8)(
                dk_native_read_bus_word(state, pc + 2u) & 0xFFu
            );
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            state->a_regs[dst_reg] = (
                state->a_regs[dst_reg]
                + dk_native_postinc_bytes(dst_reg, 1)
            ) & 0xFFFFFFFFu;
            dk_native_set_nzvc(
                state,
                (value & 0x80u) != 0,
                value == 0,
                0,
                0
            );
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 5u) {
            dk_u8 value = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u));
            dk_u32 addr = (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 7u && dst_reg == 0u) {
            dk_u8 value = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
            dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 7u && dst_reg == 1u) {
            dk_u8 value = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
            dk_u32 addr = dk_native_read_bus_long(state, pc + 4u) & 0x00FFFFFFu;
            dk_native_write_bus_byte(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 8u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    if ((opcode & 0xF000u) == 0x3000u && ((opcode >> 3) & 0x7u) == 0u) {
        int src = opcode & 0x7u;
        int dst_mode = (opcode >> 6) & 0x7u;
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[src] & 0xFFFFu);
        if (dst_mode == 0u) {
            state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 4;
        }
        if (dst_mode == 3u) {
            dk_u32 addr = state->a_regs[dst] & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            state->a_regs[dst] = (state->a_regs[dst] + 2u) & 0xFFFFFFFFu;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    if (opcode == 0x31F8u) {
        dk_u32 src_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
        dk_u32 dst_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.W d8(PC,Xn), (xxx).W.
     * Signpost bonus scoring indexes TimeBonuses then stores v_timebonus. */
    if (opcode == 0x31FBu) {
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 src_addr = dk_native_index_brief(
            state,
            (pc + 2u) & 0x00FFFFFFu,
            ext
        );
        dk_u32 dst_addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W Dn, (xxx).W */
    if ((opcode & 0xFFF8u) == 0x31C0u) {
        int src = opcode & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u16 value = (dk_u16)(state->d_regs[src] & 0xFFFFu);
        dk_native_write_bus_word(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W An, (xxx).W */
    if ((opcode & 0xF1F8u) == 0x31C8u) {
        int src = opcode & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u16 value = (dk_u16)(state->a_regs[src] & 0xFFFFu);
        dk_native_write_bus_word(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1FFu) == 0x303Cu) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1FFu) == 0x307Cu) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
        state->a_regs[dst] = (dk_u32)dk_native_sign_extend16(value);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVEA.W Dn, An */
    if ((opcode & 0xF1F8u) == 0x3040u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        state->a_regs[dst] = (dk_u32)dk_native_sign_extend16(
            (dk_u16)(state->d_regs[src] & 0xFFFFu)
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* MOVEA.W (xxx).W, An */
    if ((opcode & 0xF1FFu) == 0x3078u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 value = dk_native_read_bus_word(
            state,
            (dk_u32)dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            ) & 0x00FFFFFFu
        );
        state->a_regs[dst] = (dk_u32)dk_native_sign_extend16(value);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVEA.W d8(An,Xn), An */
    if ((opcode & 0xF1F8u) == 0x3070u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = dk_native_index_brief(
            state,
            state->a_regs[src] & 0x00FFFFFFu,
            ext
        );
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->a_regs[dst] = (dk_u32)dk_native_sign_extend16(value);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 10;
    }

    /* MOVEA.W d16(An), An */
    if ((opcode & 0xF1F8u) == 0x3068u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[src] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->a_regs[dst] = (dk_u32)dk_native_sign_extend16(value);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1F8u) == 0x3010u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1FFu) == 0x3038u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1FFu) == 0x3039u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1F8u) == 0x3028u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[src] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1FFu) == 0x303Bu) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = dk_native_index_brief(state, (pc + 2u) & 0x00FFFFFFu, ext);
        dk_u16 value = dk_native_read_bus_word(state, addr);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1FFu) == 0x207Bu) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = dk_native_index_brief(state, (pc + 2u) & 0x00FFFFFFu, ext);
        dk_u32 value = dk_native_read_bus_long(state, addr);
        state->a_regs[dst] = value;
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFFF8u) == 0x4228u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_native_write_bus_byte(state, addr, 0u);
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CLR.B (An) */
    if ((opcode & 0xFFF8u) == 0x4210u) {
        int reg = opcode & 0x7u;
        dk_native_write_bus_byte(
            state,
            state->a_regs[reg] & 0x00FFFFFFu,
            0u
        );
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CLR.W (An) */
    if ((opcode & 0xFFF8u) == 0x4250u) {
        int reg = opcode & 0x7u;
        dk_native_write_bus_word(
            state,
            state->a_regs[reg] & 0x00FFFFFFu,
            0u
        );
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CLR.B (xxx).W */
    if (opcode == 0x4238u) {
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_native_write_bus_byte(state, addr, 0u);
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CLR.W (xxx).W */
    if (opcode == 0x4278u) {
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_native_write_bus_word(state, addr, 0u);
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CLR.L (xxx).W */
    if (opcode == 0x42B8u) {
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_native_write_bus_long(state, addr, 0u);
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CLR.L (An)+ */
    if ((opcode & 0xFFF8u) == 0x4298u) {
        int reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_native_write_bus_long(state, addr, 0u);
        state->a_regs[reg] = (state->a_regs[reg] + 4u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(state, 0, 1, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF000u) == 0x2000u && ((opcode >> 3) & 0x7u) == 0u) {
        int src = opcode & 0x7u;
        int dst_mode = (opcode >> 6) & 0x7u;
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 value = state->d_regs[src];
        if (dst_mode == 2u) {
            dk_u32 addr = state->a_regs[dst] & 0x00FFFFFFu;
            dk_native_write_bus_long(state, addr, value);
            dk_native_set_nzvc(
                state,
                (value & 0x80000000u) != 0,
                value == 0,
                0,
                0
            );
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 12;
        }
        if (dst_mode == 3u) {
            dk_u32 addr = state->a_regs[dst] & 0x00FFFFFFu;
            dk_native_write_bus_long(state, addr, value);
            state->a_regs[dst] = (state->a_regs[dst] + 4u) & 0xFFFFFFFFu;
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 12;
        }
        if (dst_mode == 5u) {
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            dk_u32 addr = (state->a_regs[dst] + (dk_u32)disp) & 0x00FFFFFFu;
            dk_native_write_bus_long(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 16;
        }
    }

    /* MOVE.L Dn, Dn */
    if ((opcode & 0xF1F8u) == 0x2000u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 value = state->d_regs[src];
        state->d_regs[dst] = value;
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* MOVE.L Dn, (xxx).W */
    if ((opcode & 0xFFF8u) == 0x21C0u) {
        int src = opcode & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u32 value = state->d_regs[src];
        dk_native_write_bus_long(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.L An, (xxx).W / (xxx).L */
    if (
        (opcode & 0xF038u) == 0x2008u
        && ((opcode >> 6) & 0x7u) == 0x7u
    ) {
        int dst_mode = (opcode >> 6) & 0x7u;
        int dst_reg = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 value = state->a_regs[src];
        dk_u32 addr;
        dk_u32 advance;
        if (dst_mode == 7u && dst_reg == 0u) {
            addr = (dk_u32)dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            ) & 0x00FFFFFFu;
            advance = 4u;
        } else if (dst_mode == 7u && dst_reg == 1u) {
            addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            advance = 6u;
        } else {
            return 0;
        }
        dk_native_write_bus_long(state, addr, value);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + advance) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.L An, d16(An) */
    if ((opcode & 0xF000u) == 0x2000u) {
        int dst_mode = (opcode >> 6) & 0x7u;
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        if (src_mode == 1u && dst_mode == 5u) {
            dk_u32 value = state->a_regs[src_reg];
            int disp = dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            );
            dk_u32 addr = (
                state->a_regs[dst_reg] + (dk_u32)disp
            ) & 0x00FFFFFFu;
            dk_native_write_bus_long(state, addr, value);
            dk_native_set_nzvc(
                state,
                (value & 0x80000000u) != 0,
                value == 0,
                0,
                0
            );
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 16;
        }
    }

    /* MOVE.L Dn, -(An) - stack pushes (ROM $2F01 = D1 -> -(A7)). */
    if (
        (opcode & 0xF000u) == 0x2000u
        && ((opcode >> 6) & 0x7u) == 4u
        && ((opcode >> 3) & 0x7u) == 0u
    ) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 value = state->d_regs[src_reg];
        dk_u32 addr = (state->a_regs[dst_reg] - 4u) & 0xFFFFFFFFu;
        state->a_regs[dst_reg] = addr;
        dk_native_write_bus_long(state, addr & 0x00FFFFFFu, value);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.L (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0x2038u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, addr);
        state->d_regs[dst] = value;
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.L #imm, Dn */
    if ((opcode & 0xF1FFu) == 0x203Cu) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 value = dk_native_read_bus_long(state, pc + 2u);
        state->d_regs[dst] = value;
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.L #imm, (xxx).W */
    if (opcode == 0x21FCu) {
        dk_u32 value = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 6u)
        ) & 0x00FFFFFFu;
        dk_native_write_bus_long(state, addr, value);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 8u) & 0xFFFFFFFFu;
        return 20;
    }

    /* MOVE.L d8(An,Xn), (An)+ */
    if ((opcode & 0xF1F8u) == 0x20F0u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src] & 0x00FFFFFFu,
            ext
        );
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        dk_u32 dst_addr = state->a_regs[dst] & 0x00FFFFFFu;
        dk_native_write_bus_long(state, dst_addr, value);
        state->a_regs[dst] = (state->a_regs[dst] + 4u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.L d16(An), (An)+.
     * QuickPLC compacts its queue by copying the next long record over the
     * current entry before DBF advances to the next destination. */
    if ((opcode & 0xF1F8u) == 0x20E8u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 value = dk_native_read_bus_long(
            state,
            (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu
        );
        dk_u32 dst_addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
        dk_native_write_bus_long(state, dst_addr, value);
        state->a_regs[dst_reg] = (
            state->a_regs[dst_reg] + 4u
        ) & 0xFFFFFFFFu;
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.L (An)+, d16(An) */
    if ((opcode & 0xF1F8u) == 0x2158u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        state->a_regs[src] = (
            state->a_regs[src] + 4u
        ) & 0xFFFFFFFFu;
        dk_native_write_bus_long(
            state,
            (state->a_regs[dst] + (dk_u32)disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 20;
    }

    /* MOVE.L (An)+, (An) */
    if ((opcode & 0xF1F8u) == 0x20D8u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        dk_u32 dst_addr = state->a_regs[dst] & 0x00FFFFFFu;
        state->a_regs[src] = (state->a_regs[src] + 4u) & 0xFFFFFFFFu;
        dk_native_write_bus_long(state, dst_addr, value);
        state->a_regs[dst] = (state->a_regs[dst] + 4u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 20;
    }

    /* MOVE.W (An)+, (An)+ */
    if ((opcode & 0xF1F8u) == 0x30D8u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_u32 dst_addr = state->a_regs[dst] & 0x00FFFFFFu;
        state->a_regs[src] = (state->a_regs[src] + 2u) & 0xFFFFFFFFu;
        dk_native_write_bus_word(state, dst_addr, value);
        state->a_regs[dst] = (state->a_regs[dst] + 2u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xF1F8u) == 0x2098u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src] & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        dk_u32 dst_addr;
        state->a_regs[src] = (state->a_regs[src] + 4u) & 0xFFFFFFFFu;
        dst_addr = state->a_regs[dst] & 0x00FFFFFFu;
        dk_native_write_bus_long(state, dst_addr, value);
        dk_native_set_nzvc(
            state,
            (value & 0x80000000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 20;
    }

    /* MOVE.L d8(An,Xn), (An) */
    if ((opcode & 0xF1F8u) == 0x20B0u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src] & 0x00FFFFFFu,
            ext
        );
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        dk_native_write_bus_long(
            state,
            state->a_regs[dst] & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.L d8(An,Xn), d16(An) */
    if ((opcode & 0xF1F8u) == 0x2170u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 index_ext = dk_native_read_bus_word(state, pc + 2u);
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src] & 0x00FFFFFFu,
            index_ext
        );
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        dk_u32 dst_addr = (
            state->a_regs[dst] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_native_write_bus_long(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 20;
    }

    if (opcode == 0x23FCu) {
        dk_u32 value = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 addr = dk_native_read_bus_long(state, pc + 6u) & 0x00FFFFFFu;
        dk_native_write_bus_long(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 10u) & 0xFFFFFFFFu;
        return 20;
    }

    if (opcode == 0x23F8u) {
        dk_u32 src_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
        dk_u32 dst_addr = dk_native_read_bus_long(state, pc + 4u) & 0x00FFFFFFu;
        dk_u32 value = dk_native_read_bus_long(state, src_addr);
        dk_native_write_bus_long(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 8u) & 0xFFFFFFFFu;
        return 20;
    }

    if ((opcode & 0xFFF8u) == 0x4400u) {
        int reg = opcode & 0x7u;
        dk_u8 src = (dk_u8)(state->d_regs[reg] & 0xFFu);
        dk_u8 result = (dk_u8)(0u - src);
        state->d_regs[reg] = (
            state->d_regs[reg] & 0xFFFFFF00u
        ) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, 0u, result, 8);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4440u) {
        int reg = opcode & 0x7u;
        dk_u16 src = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(0u - src);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, 0u, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4480u) {
        int reg = opcode & 0x7u;
        dk_u32 src = state->d_regs[reg];
        dk_u32 result = 0u - src;
        state->d_regs[reg] = result;
        dk_native_set_sub_flags(state, src, 0u, result, 32);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }

    /* NEG.W d16(An) */
    if ((opcode & 0xFFF8u) == 0x4468u) {
        int reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 src = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(0u - src);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_sub_flags(state, src, 0u, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* NEG.B (An) */
    if ((opcode & 0xFFF8u) == 0x4410u) {
        int reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 src = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(0u - src);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_sub_flags(state, src, 0u, result, 8);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x4840u) {
        int reg = opcode & 0x7u;
        dk_u32 value = state->d_regs[reg];
        value = (value << 16) | (value >> 16);
        state->d_regs[reg] = value;
        dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4880u) {
        int reg = opcode & 0x7u;
        int value = dk_native_sign_extend8((dk_u8)(state->d_regs[reg] & 0xFFu));
        dk_u16 result = (dk_u16)(value & 0xFFFFu);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x0240u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u16 result = (dk_u16)((state->d_regs[reg] & 0xFFFFu) & imm);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0400u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u8 dst = (dk_u8)(state->d_regs[reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - imm);
        state->d_regs[reg] = (
            state->d_regs[reg] & 0xFFFFFF00u
        ) | (dk_u32)result;
        dk_native_set_sub_flags(state, imm, dst, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUBI.B #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x0410u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(
            dk_native_read_bus_word(state, pc + 2u) & 0xFFu
        );
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(dst - imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_sub_flags(state, imm, dst, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* ANDI.L #imm, Dn */
    if ((opcode & 0xFFF8u) == 0x0280u) {
        int reg = opcode & 0x7u;
        dk_u32 imm = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 result = state->d_regs[reg] & imm;
        state->d_regs[reg] = result;
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 14;
    }

    if ((opcode & 0xFFF8u) == 0x0440u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u16 dst = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - imm);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, imm, dst, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0480u) {
        int reg = opcode & 0x7u;
        dk_u32 imm = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 dst = state->d_regs[reg];
        dk_u32 result = dst - imm;
        state->d_regs[reg] = result;
        dk_native_set_sub_flags(state, imm, dst, result, 32);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* SUBI.W #imm, (An)+ */
    if ((opcode & 0xFFF8u) == 0x0458u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(dst - imm);
        dk_native_write_bus_word(state, addr, result);
        state->a_regs[reg] = (
            state->a_regs[reg] + 2u
        ) & 0xFFFFFFFFu;
        dk_native_set_sub_flags(state, imm, dst, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* SUBI.W #imm, (d16,An) */
    if ((opcode & 0xFFF8u) == 0x0468u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        int displacement = (int)dk_native_read_bus_word(state, pc + 4u);
        dk_u32 addr;
        dk_u16 dst;
        dk_u16 result;
        if ((displacement & 0x8000) != 0) {
            displacement -= 0x10000;
        }
        addr = (state->a_regs[reg] + (dk_u32)displacement) & 0x00FFFFFFu;
        dst = dk_native_read_bus_word(state, addr);
        result = (dk_u16)(dst - imm);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_sub_flags(state, imm, dst, result, 16);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    if ((opcode & 0xFFF8u) == 0x0600u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u8 src = (dk_u8)(state->d_regs[reg] & 0xFFu);
        dk_u16 wide = (dk_u16)src + (dk_u16)imm;
        dk_u8 result = (dk_u8)wide;
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)result;
        dk_native_set_add_flags(state, imm, src, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADDI.B #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x0610u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(
            dk_native_read_bus_word(state, pc + 2u) & 0xFFu
        );
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)((dk_u16)dst + (dk_u16)imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_add_flags(state, imm, dst, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFFF8u) == 0x0640u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u16 src = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u32 wide = (dk_u32)src + (dk_u32)imm;
        dk_u16 result = (dk_u16)wide;
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_add_flags(state, imm, src, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0680u) {
        int reg = opcode & 0x7u;
        dk_u32 imm = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 dst = state->d_regs[reg];
        dk_u32 result = dst + imm;
        state->d_regs[reg] = result;
        dk_native_set_add_flags(state, imm, dst, result, 32);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* ADDI.W #imm, d16(An) */
    if ((opcode & 0xFFF8u) == 0x0668u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)((dk_u32)dst + (dk_u32)imm);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_add_flags(state, imm, dst, result, 16);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* ADDI.W #imm, (xxx).W.
     * ProcessPLC_9Tiles advances the PLC source pointer by $120 here. */
    if (opcode == 0x0678u) {
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)((dk_u32)dst + (dk_u32)imm);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_add_flags(state, imm, dst, result, 16);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* ADDI.L #imm, (An)+ */
    if ((opcode & 0xFFF8u) == 0x0698u) {
        int reg = opcode & 0x7u;
        dk_u32 imm = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u32 dst = dk_native_read_bus_long(state, addr);
        dk_u32 result = dst + imm;
        dk_native_write_bus_long(state, addr, result);
        state->a_regs[reg] = (state->a_regs[reg] + 4u) & 0xFFFFFFFFu;
        dk_native_set_add_flags(state, imm, dst, result, 32);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    if ((opcode & 0xFFF8u) == 0x0C00u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u8 dst = (dk_u8)(state->d_regs[reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - imm);
        int v = (((dst ^ imm) & (dst ^ result)) & 0x80u) != 0u;
        int c = dst < imm;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, v, c);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* EORI.B/W/L #imm, Dn */
    if ((opcode & 0xFFF8u) == 0x0A00u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u8 result = (dk_u8)(state->d_regs[reg] ^ (dk_u32)imm);
        state->d_regs[reg] = (
            state->d_regs[reg] & 0xFFFFFF00u
        ) | (dk_u32)result;
        dk_native_set_nzvc(
            state,
            (result & 0x80u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* EORI.B #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x0A10u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(dk_native_read_bus_byte(state, addr) ^ imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(
            state,
            (result & 0x80u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFFF8u) == 0x0A40u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u16 result = (dk_u16)(state->d_regs[reg] ^ (dk_u32)imm);
        state->d_regs[reg] = (
            state->d_regs[reg] & 0xFFFF0000u
        ) | (dk_u32)result;
        dk_native_set_nzvc(
            state,
            (result & 0x8000u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0A80u) {
        int reg = opcode & 0x7u;
        dk_u32 imm = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 result = state->d_regs[reg] ^ imm;
        state->d_regs[reg] = result;
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* EORI.B #imm, (xxx).W */
    if (opcode == 0x0A38u) {
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(dk_native_read_bus_byte(state, addr) ^ imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0C40u) {
        int reg = opcode & 0x7u;
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u16 dst = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - imm);
        int v = (((dst ^ imm) & (dst ^ result)) & 0x8000u) != 0u;
        int c = dst < imm;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, v, c);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CMPI.W #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x0C50u) {
        int reg = opcode & 0x7u;
        dk_u16 src = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(dst - src);
        int v = (((dst ^ src) & (dst ^ result)) & 0x8000u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, v, c);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* CMPI.W #imm, d16(An) */
    if ((opcode & 0xFFF8u) == 0x0C68u) {
        int reg = opcode & 0x7u;
        dk_u16 src = dk_native_read_bus_word(state, pc + 2u);
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(dst - src);
        int v = (((dst ^ src) & (dst ^ result)) & 0x8000u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(
            state,
            (result & 0x8000u) != 0,
            result == 0,
            v,
            c
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFF38u) == 0x0200u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u8 result = (dk_u8)((state->d_regs[reg] & 0xFFu) & imm);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)result;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ANDI.B #imm, d16(An) */
    if ((opcode & 0xFFF8u) == 0x0228u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(dk_native_read_bus_byte(state, addr) & imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ORI.B #imm, d16(An) */
    if ((opcode & 0xFFF8u) == 0x0028u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(
            dk_native_read_bus_word(state, pc + 2u) & 0xFFu
        );
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(dk_native_read_bus_byte(state, addr) | imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(
            state,
            (result & 0x80u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* ORI.B #imm, (xxx).W */
    if (opcode == 0x0038u) {
        dk_u8 imm = (dk_u8)(
            dk_native_read_bus_word(state, pc + 2u) & 0xFFu
        );
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(dk_native_read_bus_byte(state, addr) | imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(
            state,
            (result & 0x80u) != 0,
            result == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* ANDI.B #imm, (xxx).W */
    if (opcode == 0x0238u) {
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(dk_native_read_bus_byte(state, addr) & imm);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ANDI.W #imm, (xxx).W */
    if (opcode == 0x0278u) {
        dk_u16 imm = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u16 result = (dk_u16)(dk_native_read_bus_word(state, addr) & imm);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFF38u) == 0x0000u) {
        int reg = opcode & 0x7u;
        dk_u8 imm = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u8 result = (dk_u8)((state->d_regs[reg] & 0xFFu) | imm);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)result;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if (opcode == 0x0839u) {
        dk_u32 bitnum = (dk_u32)(dk_native_read_bus_word(state, pc + 2u) & 0x00FFu);
        dk_u32 addr = dk_native_read_bus_long(state, pc + 4u) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u16 sr = state->current_sr & (dk_u16)~0x0004u;
        if (((value >> (bitnum & 7u)) & 0x1u) == 0u) {
            sr |= 0x0004u;
        }
        state->current_sr = sr;
        state->current_pc = (pc + 8u) & 0xFFFFFFFFu;
        return 8;
    }

    if (opcode == 0x0838u) {
        dk_u32 bitnum = (dk_u32)(dk_native_read_bus_word(state, pc + 2u) & 0x00FFu);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u16 sr = state->current_sr & (dk_u16)~0x0004u;
        if (((value >> (bitnum & 7u)) & 0x1u) == 0u) {
            sr |= 0x0004u;
        }
        state->current_sr = sr;
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0828u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(dk_native_read_bus_word(state, pc + 2u) & 0x00FFu);
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u16 sr = state->current_sr & (dk_u16)~0x0004u;
        if (((value >> (bitnum & 7u)) & 0x1u) == 0u) {
            sr |= 0x0004u;
        }
        state->current_sr = sr;
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BTST #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x0810u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u8 value = dk_native_read_bus_byte(
            state,
            state->a_regs[reg] & 0x00FFFFFFu
        );
        if (((value >> (bitnum & 7u)) & 0x1u) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BTST #imm, d8(An,Xn) */
    if ((opcode & 0xFFF8u) == 0x0830u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u16 ext = dk_native_read_bus_word(state, pc + 4u);
        dk_u32 addr = dk_native_index_brief(
            state,
            state->a_regs[reg] & 0x00FFFFFFu,
            ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        if (((value >> (bitnum & 7u)) & 0x1u) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFFF8u) == 0x08A8u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(dk_native_read_bus_word(state, pc + 2u) & 0x00FFu);
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u16 sr = state->current_sr & (dk_u16)~0x0004u;
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            sr |= 0x0004u;
        }
        value = (dk_u8)(value & (dk_u8)~mask);
        dk_native_write_bus_byte(state, addr, value);
        state->current_sr = sr;
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BCLR #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x0890u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value & (dk_u8)~mask));
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BCLR #imm, d8(An,Xn) */
    if ((opcode & 0xFFF8u) == 0x08B0u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u16 ext = dk_native_read_bus_word(state, pc + 4u);
        dk_u32 addr = dk_native_index_brief(
            state,
            state->a_regs[reg] & 0x00FFFFFFu,
            ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(
            state,
            addr,
            (dk_u8)(value & (dk_u8)~mask)
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* BCHG #imm, d16(An) */
    if ((opcode & 0xFFF8u) == 0x0868u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value ^ mask));
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* BSET #imm, d16(An) */
    if ((opcode & 0xFFF8u) == 0x08E8u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 addr = (
            state->a_regs[reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value | mask));
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BSET #imm, (An) */
    if ((opcode & 0xFFF8u) == 0x08D0u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value | mask));
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BSET #imm, d8(An,Xn) */
    if ((opcode & 0xFFF8u) == 0x08F0u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u16 ext = dk_native_read_bus_word(state, pc + 4u);
        dk_u32 addr = dk_native_index_brief(
            state,
            state->a_regs[reg] & 0x00FFFFFFu,
            ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value | mask));
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* BSET #imm, (xxx).W */
    if (opcode == 0x08F8u) {
        dk_u32 bitnum = (dk_u32)(
            dk_native_read_bus_word(state, pc + 2u) & 0x00FFu
        );
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value | mask));
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1C0u) == 0x0100u && ((opcode >> 3) & 0x7u) == 0x0u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        dk_u32 bitnum = state->d_regs[src_reg] & 31u;
        dk_u32 value = state->d_regs[dst_reg];
        dk_u16 sr = state->current_sr & (dk_u16)~0x0004u;
        if (((value >> bitnum) & 0x1u) == 0u) {
            sr |= 0x0004u;
        }
        state->current_sr = sr;
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }

    /* BSET Dn, Dn */
    if ((opcode & 0xF1F8u) == 0x01C0u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        dk_u32 bitnum = state->d_regs[src_reg] & 31u;
        dk_u32 mask = 1u << bitnum;
        dk_u32 value = state->d_regs[dst_reg];
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        state->d_regs[dst_reg] = value | mask;
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BSET Dn, (xxx).W */
    if ((opcode & 0xF1FFu) == 0x01F8u) {
        int src_reg = (opcode >> 9) & 0x7u;
        dk_u32 bitnum = state->d_regs[src_reg] & 7u;
        dk_u8 mask = (dk_u8)(1u << bitnum);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value | mask));
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* BSET Dn, d8(An,Xn) */
    if ((opcode & 0xF1F8u) == 0x01F0u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int addr_reg = opcode & 0x7u;
        dk_u32 bitnum = state->d_regs[src_reg];
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = dk_native_index_brief(
            state,
            state->a_regs[addr_reg] & 0x00FFFFFFu,
            ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, addr);
        dk_u8 mask = (dk_u8)(1u << (bitnum & 7u));
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        dk_native_write_bus_byte(state, addr, (dk_u8)(value | mask));
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* BCLR Dn, Dn */
    if ((opcode & 0xF1F8u) == 0x0180u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        dk_u32 bitnum = state->d_regs[src_reg] & 31u;
        dk_u32 mask = 1u << bitnum;
        dk_u32 value = state->d_regs[dst_reg];
        if ((value & mask) == 0u) {
            state->current_sr |= 0x0004u;
        } else {
            state->current_sr &= (dk_u16)~0x0004u;
        }
        state->d_regs[dst_reg] = value & ~mask;
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0800u) {
        int reg = opcode & 0x7u;
        dk_u32 bitnum = (dk_u32)(dk_native_read_bus_word(state, pc + 2u) & 0x00FFu);
        dk_u32 value = state->d_regs[reg];
        dk_u16 sr = state->current_sr & (dk_u16)~0x0004u;
        if (((value >> (bitnum & 31u)) & 0x1u) == 0u) {
            sr |= 0x0004u;
        }
        state->current_sr = sr;
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if (opcode == 0x0C38u) {
        dk_u8 src = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(dst - src);
        int v = (((dst ^ src) & (dst ^ result) & 0x80u) != 0u);
        int c = src > dst;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, v, c);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if (opcode == 0x0C78u) {
        dk_u16 src = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(dst - src);
        int v = (((dst ^ src) & (dst ^ result)) & 0x8000u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, v, c);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0C10u) {
        int reg = opcode & 0x7u;
        dk_u8 src = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(dst - src);
        int v = (((dst ^ src) & (dst ^ result) & 0x80u) != 0u);
        int c = src > dst;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, v, c);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0C28u) {
        int reg = opcode & 0x7u;
        dk_u8 src = (dk_u8)(dk_native_read_bus_word(state, pc + 2u) & 0xFFu);
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u));
        dk_u32 addr = (state->a_regs[reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(dst - src);
        int v = (((dst ^ src) & (dst ^ result) & 0x80u) != 0u);
        int c = src > dst;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, v, c);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0x0C98u) {
        int reg = opcode & 0x7u;
        dk_u32 src = dk_native_read_bus_long(state, pc + 2u);
        dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
        dk_u32 dst = dk_native_read_bus_long(state, addr);
        dk_u32 result = dst - src;
        int v = (((dst ^ src) & (dst ^ result) & 0x80000000u) != 0u);
        int c = src > dst;
        state->a_regs[reg] = (state->a_regs[reg] + 4u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, v, c);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CMP.B Dn, Dn */
    if ((opcode & 0xF1F8u) == 0xB000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u8 src = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
        dk_u8 dst = (dk_u8)(state->d_regs[dst_reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - src);
        int v = (((dst ^ src) & (dst ^ result)) & 0x80u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(
            state,
            (result & 0x80u) != 0,
            result == 0,
            v,
            c
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    /* CMP.B d16(An), Dn */
    if ((opcode & 0xF1F8u) == 0xB028u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 src = dk_native_read_bus_byte(state, addr);
        dk_u8 dst = (dk_u8)(state->d_regs[dst_reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - src);
        int v = (((dst ^ src) & (dst ^ result)) & 0x80u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, v, c);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CMP.B (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0xB038u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u8 src = dk_native_read_bus_byte(state, addr);
        dk_u8 dst = (dk_u8)(state->d_regs[dst_reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - src);
        int v = (((dst ^ src) & (dst ^ result)) & 0x80u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, v, c);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1F8u) == 0x41E8u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        state->a_regs[dst] = (state->a_regs[src] + (dk_u32)disp) & 0xFFFFFFFFu;
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* LEA d8(An,Xn), An */
    if ((opcode & 0xF1F8u) == 0x41F0u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        state->a_regs[dst] = dk_native_index_brief_full(
            state,
            state->a_regs[src],
            ext
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* LEA d8(PC,Xn), An */
    if ((opcode & 0xF1FFu) == 0x41FBu) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        state->a_regs[dst] = dk_native_index_brief_full(
            state,
            (pc + 2u) & 0xFFFFFFFFu,
            ext
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* LEA d16(PC), An. The 68000 PC base is the extension word. */
    if ((opcode & 0xF1FFu) == 0x41FAu) {
        int dst = (opcode >> 9) & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        state->a_regs[dst] = (dk_u32)((int)pc + 2 + disp);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if (((opcode & 0xF1C0u) == 0xD0C0u || (opcode & 0xF1C0u) == 0x90C0u) && (((opcode >> 6) & 0x7u) == 0x3u)) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        int is_sub = (opcode & 0xF000u) == 0x9000u;
        int src_value;
        dk_u32 advance = 2u;

        if (src_mode == 0u) {
            src_value = dk_native_sign_extend16((dk_u16)(state->d_regs[src_reg] & 0xFFFFu));
        } else if (src_mode == 1u) {
            src_value = dk_native_sign_extend16((dk_u16)(state->a_regs[src_reg] & 0xFFFFu));
        } else if (src_mode == 2u) {
            dk_u32 addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            src_value = dk_native_sign_extend16(dk_native_read_bus_word(state, addr));
        } else if (src_mode == 5u) {
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            dk_u32 addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            src_value = dk_native_sign_extend16(dk_native_read_bus_word(state, addr));
            advance = 4u;
        } else if (src_mode == 6u) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            dk_u32 addr = dk_native_index_brief(
                state,
                state->a_regs[src_reg] & 0x00FFFFFFu,
                ext
            );
            src_value = dk_native_sign_extend16(dk_native_read_bus_word(state, addr));
            advance = 4u;
        } else if (src_mode == 7u && src_reg == 4u) {
            src_value = dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            );
            advance = 4u;
        } else {
            return 0;
        }

        if (is_sub) {
            state->a_regs[dst_reg] = (state->a_regs[dst_reg] - (dk_u32)src_value) & 0xFFFFFFFFu;
        } else {
            state->a_regs[dst_reg] = (state->a_regs[dst_reg] + (dk_u32)src_value) & 0xFFFFFFFFu;
        }
        state->current_pc = (pc + advance) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADDA.L/SUBA.L Dn, An */
    if ((opcode & 0xF1F8u) == 0xD1C0u || (opcode & 0xF1F8u) == 0x91C0u) {
        int dst = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        int is_sub = (opcode & 0xF000u) == 0x9000u;
        dk_u32 value = state->d_regs[src];
        state->a_regs[dst] = is_sub
            ? (state->a_regs[dst] - value)
            : (state->a_regs[dst] + value);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1F8u) == 0xD080u) {
        int src = opcode & 0x7u;
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 lhs = state->d_regs[dst];
        dk_u32 rhs = state->d_regs[src];
        dk_u32 result = (lhs + rhs) & 0xFFFFFFFFu;
        state->d_regs[dst] = result;
        dk_native_set_add_flags(state, rhs, lhs, result, 32);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }

    /* ADD.B Dn, Dn */
    if ((opcode & 0xF1F8u) == 0xD000u) {
        int src = opcode & 0x7u;
        int dst = (opcode >> 9) & 0x7u;
        dk_u8 lhs = (dk_u8)(state->d_regs[dst] & 0xFFu);
        dk_u8 rhs = (dk_u8)(state->d_regs[src] & 0xFFu);
        dk_u8 result = (dk_u8)(lhs + rhs);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFFFF00u) | (dk_u32)result;
        dk_native_set_add_flags(state, rhs, lhs, result, 8);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.B (xxx).W, Dn.
     * GotThroughCard adds the current act to the card-table frame byte. */
    if ((opcode & 0xF1FFu) == 0xD038u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u8 lhs = (dk_u8)(state->d_regs[dst] & 0xFFu);
        dk_u8 rhs = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(lhs + rhs);
        state->d_regs[dst] = (
            state->d_regs[dst] & 0xFFFFFF00u
        ) | (dk_u32)result;
        dk_native_set_add_flags(state, rhs, lhs, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xF1F8u) == 0xD040u) {
        int src = opcode & 0x7u;
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 lhs = (dk_u16)(state->d_regs[dst] & 0xFFFFu);
        dk_u16 rhs = (dk_u16)(state->d_regs[src] & 0xFFFFu);
        dk_u16 word_result = (dk_u16)(lhs + rhs);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)word_result;
        dk_native_set_add_flags(state, rhs, lhs, word_result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.W An, Dn */
    if ((opcode & 0xF1F8u) == 0xD048u) {
        int src = opcode & 0x7u;
        int dst = (opcode >> 9) & 0x7u;
        dk_u16 lhs = (dk_u16)(state->d_regs[dst] & 0xFFFFu);
        dk_u16 rhs = (dk_u16)(state->a_regs[src] & 0xFFFFu);
        dk_u16 result = (dk_u16)(lhs + rhs);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_add_flags(state, rhs, lhs, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.W (An)+, Dn */
    if ((opcode & 0xF1F8u) == 0xD058u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[src_reg] & 0x00FFFFFFu;
        dk_u16 lhs = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 rhs = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(lhs + rhs);
        state->d_regs[dst_reg] = (
            state->d_regs[dst_reg] & 0xFFFF0000u
        ) | (dk_u32)result;
        state->a_regs[src_reg] = (state->a_regs[src_reg] + 2u) & 0xFFFFFFFFu;
        dk_native_set_add_flags(state, rhs, lhs, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.W (An), Dn */
    if ((opcode & 0xF1F8u) == 0xD050u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[src_reg] & 0x00FFFFFFu;
        dk_u16 lhs = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 rhs = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(lhs + rhs);
        state->d_regs[dst_reg] = (
            state->d_regs[dst_reg] & 0xFFFF0000u
        ) | (dk_u32)result;
        dk_native_set_add_flags(state, rhs, lhs, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.W d16(An), Dn */
    if ((opcode & 0xF1F8u) == 0xD068u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 addr = (
            state->a_regs[src_reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 lhs = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 rhs = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(lhs + rhs);
        state->d_regs[dst_reg] = (
            state->d_regs[dst_reg] & 0xFFFF0000u
        ) | (dk_u32)result;
        dk_native_set_add_flags(state, rhs, lhs, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.W (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0xD078u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u16 lhs = (dk_u16)(state->d_regs[dst] & 0xFFFFu);
        dk_u16 rhs = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(lhs + rhs);
        state->d_regs[dst] = (state->d_regs[dst] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_add_flags(state, rhs, lhs, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADD.L (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0xD0B8u) {
        int dst = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u32 lhs = state->d_regs[dst];
        dk_u32 rhs = dk_native_read_bus_long(state, addr);
        dk_u32 result = lhs + rhs;
        state->d_regs[dst] = result;
        dk_native_set_add_flags(state, rhs, lhs, result, 32);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    if ((opcode & 0xFFF8u) == 0xE248u) {
        int reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u8 last_out = (dk_u8)(value & 0x1u);
        value = (dk_u16)(value >> 1);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, last_out != 0);
        if (last_out != 0u) {
            state->current_sr |= 0x0010u;
        } else {
            state->current_sr &= (dk_u16)~0x0010u;
        }
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    if ((opcode & 0xFFF8u) == 0xE048u) {
        int reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u8 last_out = (dk_u8)(value & 0x1u);
        int count;
        for (count = 0; count < 8; ++count) {
            last_out = (dk_u8)(value & 0x1u);
            value = (dk_u16)(value >> 1);
        }
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)value;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, last_out != 0);
        if (last_out != 0u) {
            state->current_sr |= 0x0010u;
        } else {
            state->current_sr &= (dk_u16)~0x0010u;
        }
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 22;
    }

    if ((opcode & 0xFFF8u) == 0x4840u) {
        int reg = opcode & 0x7u;
        dk_u32 value = state->d_regs[reg] & 0xFFFFFFFFu;
        dk_u32 result = ((value & 0xFFFFu) << 16) | ((value >> 16) & 0xFFFFu);
        state->d_regs[reg] = result;
        dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x4880u) {
        int reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)dk_native_sign_extend8((dk_u8)(value & 0x00FFu));
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if ((opcode & 0xFFF8u) == 0x48C0u) {
        int reg = opcode & 0x7u;
        dk_u32 result = (dk_u32)dk_native_sign_extend16((dk_u16)(state->d_regs[reg] & 0xFFFFu));
        state->d_regs[reg] = result;
        dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 4;
    }

    if (opcode == 0x4E75u) {
        dk_u32 a7 = state->a_regs[7] & 0x00FFFFFFu;
        dk_u32 target = dk_native_read_bus_long(state, a7);
        state->a_regs[7] = (state->a_regs[7] + 4u) & 0xFFFFFFFFu;
        state->current_pc = target & 0xFFFFFFFFu;
        return 16;
    }

    if (opcode == 0x4E73u) {
        dk_u32 sp = state->a_regs[7] & 0x00FFFFFFu;
        dk_u16 new_sr = dk_native_read_bus_word(state, sp);
        dk_u32 new_pc = dk_native_read_bus_long(state, (sp + 2u) & 0x00FFFFFFu);
        dk_u32 sp_after = (state->a_regs[7] + 6u) & 0xFFFFFFFFu;
        if ((state->current_sr & 0x2000u) != 0u) {
            state->ssp = sp_after;
        } else {
            state->usp = sp_after;
        }
        state->current_sr = new_sr;
        if ((new_sr & 0x2000u) != 0u) {
            state->a_regs[7] = state->ssp;
        } else {
            state->a_regs[7] = state->usp;
        }
        state->current_pc = new_pc & 0xFFFFFFFFu;
        return 20;
    }

    if ((opcode & 0xFF00u) == 0x6100u) {
        dk_u8 disp8 = opcode & 0x00FFu;
        int disp;
        dk_u32 return_pc;
        if (disp8 == 0x00u) {
            disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            return_pc = (pc + 4u) & 0xFFFFFFFFu;
            dk_native_push_long(state, return_pc);
            state->current_pc = (dk_u32)((int)pc + 2 + disp);
            return 18;
        }
        disp = dk_native_sign_extend8(disp8);
        return_pc = (pc + 2u) & 0xFFFFFFFFu;
        dk_native_push_long(state, return_pc);
        state->current_pc = (pc + 2u + (dk_u32)disp) & 0xFFFFFFFFu;
        return 18;
    }

    if ((opcode & 0xF000u) == 0x6000u) {
        dk_u8 disp8 = opcode & 0x00FFu;
        int disp;
        if (disp8 == 0x00u) {
            disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            if (dk_native_condition_true(state, opcode)) {
                state->current_pc = (dk_u32)((int)pc + 2 + disp);
                return 12;
            }
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 10;
        }
        disp = dk_native_sign_extend8(disp8);
        if (dk_native_condition_true(state, opcode)) {
            state->current_pc = (pc + 2u + (dk_u32)disp) & 0xFFFFFFFFu;
            return 10;
        }
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* DBcc Dn, disp - 0x50C8 + (cond<<8) + reg */
    if ((opcode & 0xF0F8u) == 0x50C8u) {
        int reg = opcode & 0x7u;
        int cond = (opcode >> 8) & 0xFu;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u16 counter = (dk_u16)((state->d_regs[reg] & 0xFFFFu) - 1u);
        state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)counter;
        if (cond == 0x1u) {
            /* DBF / DBRA - always decrement, branch if counter != -1 */
            if (counter != 0xFFFFu) {
                state->current_pc = (dk_u32)((int)pc + 2 + disp);
                return 10;
            }
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 14;
        }
        if (!dk_native_condition_true(state, (dk_u16)((cond << 8) | 0x6000u))) {
            if (counter != 0xFFFFu) {
                state->current_pc = (dk_u32)((int)pc + 2 + disp);
                return 10;
            }
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 14;
        }
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* LEA (xxx).W, An and LEA (xxx).L, An - full 0x41C0 base */
    if ((opcode & 0xF1C0u) == 0x41C0u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_rm = opcode & 0x7u;
        dk_u32 addr;

        if (src_mode == 0x7u && src_rm == 0x0u) {
            /* LEA (xxx).W, An */
            addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            state->a_regs[dst_reg] = addr & 0xFFFFFFFFu;
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x7u && src_rm == 0x1u) {
            /* LEA (xxx).L, An */
            addr = dk_native_read_bus_long(state, pc + 2u);
            state->a_regs[dst_reg] = addr;
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 12;
        }
    }

    /* SUB.W Dn, Dm */
    if ((opcode & 0xF1C0u) == 0x9040u && ((opcode >> 3) & 0x7u) == 0x0u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 dst = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 src = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - src);
        state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.B Dn, Dm */
    if ((opcode & 0xF1F8u) == 0x9000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u8 src = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
        dk_u8 dst = (dk_u8)(state->d_regs[dst_reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - src);
        state->d_regs[dst_reg] = (
            state->d_regs[dst_reg] & 0xFFFFFF00u
        ) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 8);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.B d8(PC,Xn), Dn */
    if ((opcode & 0xF1FFu) == 0x903Bu) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = dk_native_index_brief(
            state,
            (pc + 2u) & 0x00FFFFFFu,
            ext
        );
        dk_u8 src = dk_native_read_bus_byte(state, addr);
        dk_u8 dst = (dk_u8)(state->d_regs[dst_reg] & 0xFFu);
        dk_u8 result = (dk_u8)(dst - src);
        state->d_regs[dst_reg] = (
            state->d_regs[dst_reg] & 0xFFFFFF00u
        ) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 14;
    }

    /* SUB.B Dn, d16(An) */
    if ((opcode & 0xF1F8u) == 0x9128u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 addr = (
            state->a_regs[dst_reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u8 src = (dk_u8)(state->d_regs[src_reg] & 0xFFu);
        dk_u8 dst = dk_native_read_bus_byte(state, addr);
        dk_u8 result = (dk_u8)(dst - src);
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_sub_flags(state, src, dst, result, 8);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.W Dn, d16(An) */
    if ((opcode & 0xF1F8u) == 0x9168u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 addr = (
            state->a_regs[dst_reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 src = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u16 dst = dk_native_read_bus_word(state, addr);
        dk_u16 result = (dk_u16)(dst - src);
        dk_native_write_bus_word(state, addr, result);
        dk_native_set_sub_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.L Dn, Dn */
    if ((opcode & 0xF1F8u) == 0x9080u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 src = state->d_regs[src_reg];
        dk_u32 dst = state->d_regs[dst_reg];
        dk_u32 result = dst - src;
        state->d_regs[dst_reg] = result;
        dk_native_set_sub_flags(state, src, dst, result, 32);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.W d16(An), Dn */
    if ((opcode & 0xF1F8u) == 0x9068u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u16 src = dk_native_read_bus_word(state, addr);
        dk_u16 dst = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - src);
        state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.W (An), Dn */
    if ((opcode & 0xF1F8u) == 0x9050u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr = state->a_regs[src_reg] & 0x00FFFFFFu;
        dk_u16 src = dk_native_read_bus_word(state, addr);
        dk_u16 dst = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - src);
        state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.W An, Dn */
    if ((opcode & 0xF1F8u) == 0x9048u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 src = (dk_u16)(state->a_regs[src_reg] & 0xFFFFu);
        dk_u16 dst = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - src);
        state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.W (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0x9078u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u16 src = dk_native_read_bus_word(state, addr);
        dk_u16 dst = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 result = (dk_u16)(dst - src);
        state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
        dk_native_set_sub_flags(state, src, dst, result, 16);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* SUB.L (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0x90B8u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u32 src = dk_native_read_bus_long(state, addr);
        dk_u32 dst = state->d_regs[dst_reg];
        dk_u32 result = dst - src;
        state->d_regs[dst_reg] = result;
        dk_native_set_sub_flags(state, src, dst, result, 32);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* SUB.L <ea>, Dn.  The Nick Arcade object loop uses the common
     * register-indirect form (SUB.L (An),Dn) while resolving platform
     * movement.  Keep the other long-source modes here so the native slice
     * does not fall back again when the object crosses a different path. */
    if ((opcode & 0xF1C0u) == 0x9080u && ((opcode >> 6) & 0x3u) == 0x2u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 destination = state->d_regs[dst_reg];
        dk_u32 source;
        dk_u32 advance;

        if (src_mode == 0u) {
            source = state->d_regs[src_reg];
            advance = 2u;
        } else if (src_mode == 1u) {
            source = state->a_regs[src_reg];
            advance = 2u;
        } else if (src_mode == 2u) {
            source = dk_native_read_bus_long(
                state,
                state->a_regs[src_reg] & 0x00FFFFFFu
            );
            advance = 2u;
        } else if (src_mode == 3u) {
            source = dk_native_read_bus_long(
                state,
                state->a_regs[src_reg] & 0x00FFFFFFu
            );
            state->a_regs[src_reg] = (state->a_regs[src_reg] + 4u) & 0xFFFFFFFFu;
            advance = 2u;
        } else if (src_mode == 4u) {
            state->a_regs[src_reg] = (state->a_regs[src_reg] - 4u) & 0xFFFFFFFFu;
            source = dk_native_read_bus_long(
                state,
                state->a_regs[src_reg] & 0x00FFFFFFu
            );
            advance = 2u;
        } else if (src_mode == 5u) {
            int disp = dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            );
            source = dk_native_read_bus_long(
                state,
                (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu
            );
            advance = 4u;
        } else if (src_mode == 6u) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            source = dk_native_read_bus_long(
                state,
                dk_native_index_brief(
                    state,
                    state->a_regs[src_reg] & 0x00FFFFFFu,
                    ext
                )
            );
            advance = 4u;
        } else if (src_mode == 7u && src_reg == 0u) {
            source = dk_native_read_bus_long(
                state,
                (dk_u32)dk_native_sign_extend16(
                    dk_native_read_bus_word(state, pc + 2u)
                ) & 0x00FFFFFFu
            );
            advance = 4u;
        } else if (src_mode == 7u && src_reg == 1u) {
            source = dk_native_read_bus_long(state, pc + 2u);
            advance = 6u;
        } else if (src_mode == 7u && src_reg == 4u) {
            source = dk_native_read_bus_long(state, pc + 2u);
            advance = 6u;
        } else {
            return 0;
        }

        {
            dk_u32 result = destination - source;
            state->d_regs[dst_reg] = result;
            dk_native_set_sub_flags(state, source, destination, result, 32);
        }
        state->current_pc = (pc + advance) & 0xFFFFFFFFu;
        return 12;
    }

    /* ADD.L Dn, Dm - 0xD080 + variants */
    if ((opcode & 0xF000u) == 0xD000u) {
        int size = (opcode >> 6) & 0x3u;
        int dst_reg = (opcode >> 9) & 0x7u;
        int src = opcode & 0x7u;
        dk_u32 src_val, result;

        if (size == 0x1u && (opcode & 0x38u) == 0x00u) {
            dk_u16 lhs = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
            dk_u16 rhs = (dk_u16)(state->d_regs[src] & 0xFFFFu);
            dk_u16 word_result = (dk_u16)(lhs + rhs);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)word_result;
            dk_native_set_nzvc(state, (word_result & 0x8000u) != 0, word_result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x1u && (opcode & 0x0100u) != 0u && (opcode & 0x38u) == 0x38u) {
            /* ADD.W Dn,(xxx).W. DynamicLevelEvents uses $D378 to adjust
             * v_limitbtm2 while GHZ streams toward its far-side terrain. */
            dk_u32 addr = (dk_u32)dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            ) & 0x00FFFFFFu;
            dk_u16 destination = dk_native_read_bus_word(state, addr);
            dk_u16 source = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
            dk_u16 word_result = (dk_u16)(destination + source);
            dk_native_write_bus_word(state, addr, word_result);
            dk_native_set_add_flags(state, source, destination, word_result, 16);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x2u) {
            if ((opcode & 0x38u) == 0x00u) {
                /* ADD.L Dn, Dm (both data regs) */
                src_val = state->d_regs[src];
                result = (state->d_regs[dst_reg] + src_val) & 0xFFFFFFFFu;
                state->d_regs[dst_reg] = result;
                dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 6;
            }
            if ((opcode & 0x0100u) == 0u && (opcode & 0x38u) == 0x10u) {
                /* ADD.L (An), Dm. Nick Arcade uses this while accumulating
                 * the moving-platform/object coordinate deltas. */
                dk_u32 addr = state->a_regs[src] & 0x00FFFFFFu;
                dk_u32 destination = state->d_regs[dst_reg];
                dk_u32 source = dk_native_read_bus_long(state, addr);
                dk_u32 result = (destination + source) & 0xFFFFFFFFu;
                state->d_regs[dst_reg] = result;
                dk_native_set_add_flags(state, source, destination, result, 32);
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 12;
            }
            if ((opcode & 0x38u) == 0x18u) {
                /* ADD.L (An)+, Dm */
                dk_u32 addr = state->a_regs[src] & 0x00FFFFFFu;
                dk_u32 destination = state->d_regs[dst_reg];
                dk_u32 source = dk_native_read_bus_long(state, addr);
                dk_u32 result = (destination + source) & 0xFFFFFFFFu;
                state->a_regs[src] = (state->a_regs[src] + 4u) & 0xFFFFFFFFu;
                state->d_regs[dst_reg] = result;
                dk_native_set_add_flags(state, source, destination, result, 32);
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 12;
            }
            if ((opcode & 0x38u) == 0x20u) {
                /* ADD.L -(An), Dm */
                dk_u32 addr = (state->a_regs[src] - 4u) & 0xFFFFFFFFu;
                dk_u32 destination = state->d_regs[dst_reg];
                dk_u32 source = dk_native_read_bus_long(state, addr & 0x00FFFFFFu);
                dk_u32 result = (destination + source) & 0xFFFFFFFFu;
                state->a_regs[src] = addr;
                state->d_regs[dst_reg] = result;
                dk_native_set_add_flags(state, source, destination, result, 32);
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 14;
            }
            if ((opcode & 0x38u) == 0x28u) {
                /* ADD.L d16(An), Dm */
                int disp = dk_native_sign_extend16(
                    dk_native_read_bus_word(state, pc + 2u)
                );
                dk_u32 addr = (
                    state->a_regs[src] + (dk_u32)disp
                ) & 0x00FFFFFFu;
                dk_u32 destination = state->d_regs[dst_reg];
                dk_u32 source = dk_native_read_bus_long(state, addr);
                dk_u32 result = (destination + source) & 0xFFFFFFFFu;
                state->d_regs[dst_reg] = result;
                dk_native_set_add_flags(state, source, destination, result, 32);
                state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
                return 16;
            }
            if ((opcode & 0x38u) == 0x30u) {
                /* ADD.L d8(An,Xn), Dm */
                dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
                dk_u32 addr = dk_native_index_brief(
                    state,
                    state->a_regs[src] & 0x00FFFFFFu,
                    ext
                );
                dk_u32 destination = state->d_regs[dst_reg];
                dk_u32 source = dk_native_read_bus_long(state, addr);
                dk_u32 result = (destination + source) & 0xFFFFFFFFu;
                state->d_regs[dst_reg] = result;
                dk_native_set_add_flags(state, source, destination, result, 32);
                state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
                return 18;
            }
            if ((opcode & 0x38u) == 0x38u) {
                dk_u32 addr;
                dk_u32 destination = state->d_regs[dst_reg];
                dk_u32 source;
                dk_u32 advance;
                if (src == 0u) {
                    addr = (dk_u32)dk_native_sign_extend16(
                        dk_native_read_bus_word(state, pc + 2u)
                    ) & 0x00FFFFFFu;
                    source = dk_native_read_bus_long(state, addr);
                    advance = 4u;
                } else if (src == 1u) {
                    addr = dk_native_read_bus_long(state, pc + 2u);
                    source = dk_native_read_bus_long(state, addr);
                    advance = 6u;
                } else if (src == 4u) {
                    source = dk_native_read_bus_long(state, pc + 2u);
                    advance = 6u;
                } else {
                    return 0;
                }
                {
                    dk_u32 result = (destination + source) & 0xFFFFFFFFu;
                    state->d_regs[dst_reg] = result;
                    dk_native_set_add_flags(state, source, destination, result, 32);
                }
                state->current_pc = (pc + advance) & 0xFFFFFFFFu;
                return 20;
            }
            if ((opcode & 0x38u) == 0x08u) {
                /* ADD.L An, Dm (addr reg + data reg) */
                src_val = state->a_regs[src];
                result = (state->d_regs[dst_reg] + src_val) & 0xFFFFFFFFu;
                state->d_regs[dst_reg] = result;
                dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 6;
            }
            if ((opcode & 0x0100u) != 0u && ((opcode & 0x38u) == 0x10u)) {
                /* ADD.L Dn,(An).  Sonic 1's AddPoints uses D193 here. */
                dk_u32 addr = state->a_regs[src] & 0x00FFFFFFu;
                dk_u32 destination = dk_native_read_bus_long(state, addr);
                src_val = state->d_regs[dst_reg];
                result = (destination + src_val) & 0xFFFFFFFFu;
                dk_native_write_bus_long(state, addr, result);
                dk_native_set_add_flags(state, src_val, destination, result, 32);
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                /* Keep the native slice cycle-accounting identical to m68k_core. */
                return 8;
            }
        }
    }

    if (opcode == 0x4EFBu) {
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        state->current_pc = dk_native_index_brief(state, (pc + 2u) & 0x00FFFFFFu, ext);
        return 10;
    }

    /* EXG Dn,Dn / An,An / Dn,An */
    if ((opcode & 0xF1F8u) == 0xC140u) {
        int reg_x = (opcode >> 9) & 0x7u;
        int reg_y = opcode & 0x7u;
        dk_u32 value = state->d_regs[reg_x];
        state->d_regs[reg_x] = state->d_regs[reg_y];
        state->d_regs[reg_y] = value;
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }
    if ((opcode & 0xF1F8u) == 0xC148u) {
        int reg_x = (opcode >> 9) & 0x7u;
        int reg_y = opcode & 0x7u;
        dk_u32 value = state->a_regs[reg_x];
        state->a_regs[reg_x] = state->a_regs[reg_y];
        state->a_regs[reg_y] = value;
        state->ssp = state->a_regs[7];
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }
    if ((opcode & 0xF1F8u) == 0xC188u) {
        int data_reg = (opcode >> 9) & 0x7u;
        int addr_reg = opcode & 0x7u;
        dk_u32 value = state->d_regs[data_reg];
        state->d_regs[data_reg] = state->a_regs[addr_reg];
        state->a_regs[addr_reg] = value;
        state->ssp = state->a_regs[7];
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }

    /* DIVU.W <ea>, Dn */
    if ((opcode & 0xF1C0u) == 0x80C0u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src;
        dk_u32 next_pc;
        dk_u32 divisor;
        dk_u32 dividend;
        dk_u32 quotient;
        dk_u32 remainder;
        if (!dk_native_read_mul_word_source(
            state,
            pc,
            opcode,
            &src,
            &next_pc
        )) {
            return 0;
        }
        divisor = (dk_u32)((dk_u16)src);
        if (divisor == 0u) {
            /* Let the reference core construct the vector-5 exception frame. */
            return 0;
        }
        dividend = state->d_regs[dst_reg];
        quotient = dividend / divisor;
        remainder = dividend % divisor;
        if (quotient > 0xFFFFu) {
            state->current_sr = (dk_u16)(
                (state->current_sr & (dk_u16)~0x0003u) | 0x0002u
            );
        } else {
            state->d_regs[dst_reg] = (
                (remainder & 0xFFFFu) << 16
            ) | (quotient & 0xFFFFu);
            dk_native_set_nzvc(
                state,
                (quotient & 0x8000u) != 0u,
                (quotient & 0xFFFFu) == 0u,
                0,
                0
            );
        }
        state->current_pc = next_pc;
        return 140;
    }

    /* DIVS.W #imm, Dn */
    if ((opcode & 0xF1FFu) == 0x81FCu) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int divisor = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        int dividend = (int)state->d_regs[dst_reg];
        int quotient;
        int remainder;
        if (divisor == 0) {
            return 0;
        }
        quotient = dividend / divisor;
        remainder = dividend - (quotient * divisor);
        if (quotient < -32768 || quotient > 32767) {
            state->current_sr = (dk_u16)(
                (state->current_sr & (dk_u16)~0x0003u) | 0x0002u
            );
        } else {
            state->d_regs[dst_reg] = (
                ((dk_u32)remainder & 0xFFFFu) << 16
            ) | ((dk_u32)quotient & 0xFFFFu);
            dk_native_set_nzvc(state, quotient < 0, quotient == 0, 0, 0);
        }
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 158;
    }

    /* MULU.W <ea>, Dn */
    if ((opcode & 0xF1C0u) == 0xC0C0u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src;
        dk_u32 next_pc;
        dk_u32 result;
        if (!dk_native_read_mul_word_source(state, pc, opcode, &src, &next_pc)) {
            return 0;
        }
        result = (dk_u32)(((dk_u16)src) * ((dk_u16)(state->d_regs[dst_reg] & 0xFFFFu)));
        state->d_regs[dst_reg] = result;
        dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
        state->current_pc = next_pc;
        return 38;
    }

    /* MULS.W <ea>, Dn */
    if ((opcode & 0xF1C0u) == 0xC1C0u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src;
        dk_u32 next_pc;
        int dst = dk_native_sign_extend16((dk_u16)(state->d_regs[dst_reg] & 0xFFFFu));
        dk_u32 result;
        if (!dk_native_read_mul_word_source(state, pc, opcode, &src, &next_pc)) {
            return 0;
        }
        result = (dk_u32)(src * dst);
        state->d_regs[dst_reg] = result;
        dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
        state->current_pc = next_pc;
        return 38;
    }

    /* AND.B/W/L Dn, Dm */
    if ((opcode & 0xF1C0u) == 0xC000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int size = (opcode >> 6) & 0x3u;
        int src_reg = opcode & 0x7u;
        if (((opcode >> 3) & 0x7u) != 0x0u) {
            return 0;
        }
        if (size == 0x0u) {
            dk_u8 result = (dk_u8)((state->d_regs[dst_reg] & 0xFFu) & (state->d_regs[src_reg] & 0xFFu));
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)result;
            dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x1u) {
            dk_u16 result = (dk_u16)((state->d_regs[dst_reg] & 0xFFFFu) & (state->d_regs[src_reg] & 0xFFFFu));
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
            dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x2u) {
            dk_u32 result = state->d_regs[dst_reg] & state->d_regs[src_reg];
            state->d_regs[dst_reg] = result;
            dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    /*
     * CMPA.L An,An.  This exact form is used by Sonic 1's collapsing
     * ledge object immediately before BCC.  It sits in the same broad
     * $B1xx family as EOR, so it must be decoded before the generic EOR
     * handler below.
     */
    if ((opcode & 0xF1F8u) == 0xB1C8u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 src = state->a_regs[src_reg];
        dk_u32 dst = state->a_regs[dst_reg];
        dk_u32 result = dst - src;
        int v = (((dst ^ src) & (dst ^ result)) & 0x80000000u) != 0u;
        int c = dst < src;
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0u,
            result == 0u,
            v,
            c
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }

    /* EOR.B/W/L Dn, Dm */
    if ((opcode & 0xF100u) == 0xB100u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int size = (opcode >> 6) & 0x3u;
        int dst_reg = opcode & 0x7u;
        if (((opcode >> 3) & 0x7u) != 0x0u) {
            return 0;
        }
        if (size == 0x0u) {
            dk_u8 result = (dk_u8)((state->d_regs[dst_reg] & 0xFFu) ^ (state->d_regs[src_reg] & 0xFFu));
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)result;
            dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x1u) {
            dk_u16 result = (dk_u16)((state->d_regs[dst_reg] & 0xFFFFu) ^ (state->d_regs[src_reg] & 0xFFFFu));
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
            dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x2u) {
            dk_u32 result = state->d_regs[dst_reg] ^ state->d_regs[src_reg];
            state->d_regs[dst_reg] = result;
            dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    /* CMP.W <ea>, Dn */
    if ((opcode & 0xF1C0u) == 0xB040u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr;
        dk_u16 src;
        dk_u16 dst = (dk_u16)(state->d_regs[dst_reg] & 0xFFFFu);
        dk_u16 result;
        int v;
        int c;
        dk_u32 next_pc;
        if (src_mode == 0x0u) {
            src = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
            next_pc = (pc + 2u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x1u) {
            src = (dk_u16)(state->a_regs[src_reg] & 0xFFFFu);
            next_pc = (pc + 2u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x2u) {
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            src = dk_native_read_bus_word(state, addr);
            next_pc = (pc + 2u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x3u) {
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            src = dk_native_read_bus_word(state, addr);
            state->a_regs[src_reg] = (state->a_regs[src_reg] + 2u) & 0xFFFFFFFFu;
            next_pc = (pc + 2u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x5u) {
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            src = dk_native_read_bus_word(state, addr);
            next_pc = (pc + 4u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x6u) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[src_reg] & 0x00FFFFFFu, ext);
            src = dk_native_read_bus_word(state, addr);
            next_pc = (pc + 4u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x7u && src_reg == 0x0u) {
            addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            src = dk_native_read_bus_word(state, addr);
            next_pc = (pc + 4u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x7u && src_reg == 0x1u) {
            addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            src = dk_native_read_bus_word(state, addr);
            next_pc = (pc + 6u) & 0xFFFFFFFFu;
        } else if (src_mode == 0x7u && src_reg == 0x4u) {
            src = dk_native_read_bus_word(state, pc + 2u);
            next_pc = (pc + 4u) & 0xFFFFFFFFu;
        } else {
            return 0;
        }
        result = (dk_u16)(dst - src);
        v = (((dst ^ src) & (dst ^ result)) & 0x8000u) != 0u;
        c = dst < src;
        dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, v, c);
        state->current_pc = next_pc;
        return 8;
    }

    /* CMPA.W #imm, An */
    if ((opcode & 0xF1FFu) == 0xB0FCu) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 source = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 destination = state->a_regs[dst_reg];
        dk_u32 result = (destination - source) & 0xFFFFFFFFu;
        int v = (((destination ^ source) & (destination ^ result)) & 0x80000000u) != 0u;
        int c = destination < source;
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0u,
            result == 0u,
            v,
            c
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 6;
    }

    /* CMP.L (An), Dn */
    if ((opcode & 0xF1F8u) == 0xB090u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 src = dk_native_read_bus_long(
            state,
            state->a_regs[src_reg] & 0x00FFFFFFu
        );
        dk_u32 dst = state->d_regs[dst_reg];
        dk_u32 result = (dst - src) & 0xFFFFFFFFu;
        int v = (((dst ^ src) & (dst ^ result)) & 0x80000000u) != 0u;
        int c = dst < src;
        /* CMP updates NZVC but must preserve the X flag. */
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0u,
            result == 0u,
            v,
            c
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CMP.L (xxx).W, Dn */
    if ((opcode & 0xF1FFu) == 0xB0B8u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u32 src = dk_native_read_bus_long(state, addr);
        dk_u32 dst = state->d_regs[dst_reg];
        dk_u32 result = (dst - src) & 0xFFFFFFFFu;
        int v = (((dst ^ src) & (dst ^ result)) & 0x80000000u) != 0u;
        int c = dst < src;
        /* CMP updates NZVC but must preserve the X flag. */
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0u,
            result == 0u,
            v,
            c
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* CMP.L An, Dn */
    if ((opcode & 0xF1F8u) == 0xB088u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 src = state->a_regs[src_reg];
        dk_u32 dst = state->d_regs[dst_reg];
        dk_u32 result = (dst - src) & 0xFFFFFFFFu;
        int v = (((dst ^ src) & (dst ^ result)) & 0x80000000u) != 0u;
        int c = dst < src;
        /* CMP updates NZVC but must preserve the X flag. */
        dk_native_set_nzvc(
            state,
            (result & 0x80000000u) != 0u,
            result == 0u,
            v,
            c
        );
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6;
    }

    /* OR.B/W/L Dn, Dm */
    if ((opcode & 0xF000u) == 0x8000u && ((opcode >> 3) & 0x7u) == 0x0u) {
        int size = (opcode >> 6) & 0x3u;
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        if (size == 0x0u) {
            dk_u8 result = (dk_u8)((state->d_regs[dst_reg] & 0xFFu) | (state->d_regs[src_reg] & 0xFFu));
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)result;
            dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x1u) {
            dk_u16 result = (dk_u16)((state->d_regs[dst_reg] & 0xFFFFu) | (state->d_regs[src_reg] & 0xFFFFu));
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)result;
            dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (size == 0x2u) {
            dk_u32 result = state->d_regs[dst_reg] | state->d_regs[src_reg];
            state->d_regs[dst_reg] = result;
            dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    /* OR.B Dn, d16(An) */
    if ((opcode & 0xF1F8u) == 0x8128u) {
        int src_reg = (opcode >> 9) & 0x7u;
        int dst_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 addr = (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u8 result = (dk_u8)(
            dk_native_read_bus_byte(state, addr)
            | (dk_u8)(state->d_regs[src_reg] & 0xFFu)
        );
        dk_native_write_bus_byte(state, addr, result);
        dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* ADDQ/SUBQ #n, Dn/An */
    if ((opcode & 0xF000u) == 0x5000u) {
        int data = (opcode >> 9) & 0x7u;
        int size = (opcode >> 6) & 0x3u;
        int mode = (opcode >> 3) & 0x7u;
        int reg = opcode & 0x7u;
        int is_sub = (opcode & 0x0100u) != 0;
        dk_u32 quick = (data == 0) ? 8u : (dk_u32)data;
        if (mode == 1u) {
            dk_u32 before = state->a_regs[reg];
            dk_u32 delta = quick;
            if (size == 0x0u) {
                delta &= 0xFFu;
            } else if (size == 0x1u) {
                delta &= 0xFFFFu;
            }
            state->a_regs[reg] = is_sub ? ((before - delta) & 0xFFFFFFFFu) : ((before + delta) & 0xFFFFFFFFu);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (mode == 0u) {
            dk_u32 before = state->d_regs[reg];
            if (size == 0x0u) {
                dk_u8 lhs = (dk_u8)(before & 0xFFu);
                dk_u8 rhs = (dk_u8)(quick & 0xFFu);
                dk_u8 result = is_sub ? (dk_u8)(lhs - rhs) : (dk_u8)(lhs + rhs);
                state->d_regs[reg] = (before & 0xFFFFFF00u) | (dk_u32)result;
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 8);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 8);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 4;
            }
            if (size == 0x1u) {
                dk_u16 lhs = (dk_u16)(before & 0xFFFFu);
                dk_u16 rhs = (dk_u16)(quick & 0xFFFFu);
                dk_u16 result = is_sub ? (dk_u16)(lhs - rhs) : (dk_u16)(lhs + rhs);
                state->d_regs[reg] = (before & 0xFFFF0000u) | (dk_u32)result;
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 16);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 16);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 4;
            }
            if (size == 0x2u) {
                dk_u32 rhs = quick;
                dk_u32 result = is_sub ? (before - rhs) : (before + rhs);
                state->d_regs[reg] = result;
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, before, result, 32);
                } else {
                    dk_native_set_add_flags(state, rhs, before, result, 32);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 8;
            }
        }
        if (mode == 2u) {
            dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
            if (size == 0x0u) {
                dk_u8 lhs = dk_native_read_bus_byte(state, addr);
                dk_u8 result = is_sub
                    ? (dk_u8)(lhs - (dk_u8)quick)
                    : (dk_u8)(lhs + (dk_u8)quick);
                dk_native_write_bus_byte(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 8);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 8);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x1u) {
                dk_u16 lhs = dk_native_read_bus_word(state, addr);
                dk_u16 result = is_sub
                    ? (dk_u16)(lhs - (dk_u16)quick)
                    : (dk_u16)(lhs + (dk_u16)quick);
                dk_native_write_bus_word(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 16);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 16);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x2u) {
                dk_u32 lhs = dk_native_read_bus_long(state, addr);
                dk_u32 result = is_sub ? (lhs - quick) : (lhs + quick);
                dk_native_write_bus_long(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 32);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 32);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 12;
            }
        }
        if (mode == 3u) {
            dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
            if (size == 0x0u) {
                dk_u8 lhs = dk_native_read_bus_byte(state, addr);
                dk_u8 rhs = (dk_u8)(quick & 0xFFu);
                dk_u8 result = is_sub ? (dk_u8)(lhs - rhs) : (dk_u8)(lhs + rhs);
                dk_native_write_bus_byte(state, addr, result);
                state->a_regs[reg] = (
                    state->a_regs[reg] + dk_native_postinc_bytes(reg, 1)
                ) & 0xFFFFFFFFu;
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 8);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 8);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x1u) {
                dk_u16 lhs = dk_native_read_bus_word(state, addr);
                dk_u16 rhs = (dk_u16)(quick & 0xFFFFu);
                dk_u16 result = is_sub ? (dk_u16)(lhs - rhs) : (dk_u16)(lhs + rhs);
                dk_native_write_bus_word(state, addr, result);
                state->a_regs[reg] = (state->a_regs[reg] + 2u) & 0xFFFFFFFFu;
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 16);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 16);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x2u) {
                dk_u32 lhs = dk_native_read_bus_long(state, addr);
                dk_u32 result = is_sub ? (lhs - quick) : (lhs + quick);
                dk_native_write_bus_long(state, addr, result);
                state->a_regs[reg] = (state->a_regs[reg] + 4u) & 0xFFFFFFFFu;
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 32);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 32);
                }
                state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
                return 12;
            }
        }
        if (mode == 5u) {
            int disp = dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 2u)
            );
            dk_u32 addr = (
                state->a_regs[reg] + (dk_u32)disp
            ) & 0x00FFFFFFu;
            if (size == 0x0u) {
                dk_u8 lhs = dk_native_read_bus_byte(state, addr);
                dk_u8 result = is_sub
                    ? (dk_u8)(lhs - (dk_u8)quick)
                    : (dk_u8)(lhs + (dk_u8)quick);
                dk_native_write_bus_byte(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 8);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 8);
                }
                state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x1u) {
                dk_u16 lhs = dk_native_read_bus_word(state, addr);
                dk_u16 result = is_sub
                    ? (dk_u16)(lhs - (dk_u16)quick)
                    : (dk_u16)(lhs + (dk_u16)quick);
                dk_native_write_bus_word(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 16);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 16);
                }
                state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x2u) {
                dk_u32 lhs = dk_native_read_bus_long(state, addr);
                dk_u32 result = is_sub ? (lhs - quick) : (lhs + quick);
                dk_native_write_bus_long(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, quick, lhs, result, 32);
                } else {
                    dk_native_set_add_flags(state, quick, lhs, result, 32);
                }
                state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
                return 12;
            }
        }
        if (mode == 7u && (reg == 0u || reg == 1u)) {
            dk_u32 addr;
            dk_u32 advance;
            if (reg == 0u) {
                addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
                advance = 4u;
            } else {
                addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
                advance = 6u;
            }
            if (size == 0x0u) {
                dk_u8 lhs = dk_native_read_bus_byte(state, addr);
                dk_u8 rhs = (dk_u8)(quick & 0xFFu);
                dk_u8 result = is_sub ? (dk_u8)(lhs - rhs) : (dk_u8)(lhs + rhs);
                dk_native_write_bus_byte(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 8);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 8);
                }
                state->current_pc = (pc + advance) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x1u) {
                dk_u16 lhs = dk_native_read_bus_word(state, addr);
                dk_u16 rhs = (dk_u16)(quick & 0xFFFFu);
                dk_u16 result = is_sub ? (dk_u16)(lhs - rhs) : (dk_u16)(lhs + rhs);
                dk_native_write_bus_word(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 16);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 16);
                }
                state->current_pc = (pc + advance) & 0xFFFFFFFFu;
                return 8;
            }
            if (size == 0x2u) {
                dk_u32 lhs = dk_native_read_bus_long(state, addr);
                dk_u32 rhs = quick;
                dk_u32 result = is_sub ? (lhs - rhs) : (lhs + rhs);
                dk_native_write_bus_long(state, addr, result);
                if (is_sub) {
                    dk_native_set_sub_flags(state, rhs, lhs, result, 32);
                } else {
                    dk_native_set_add_flags(state, rhs, lhs, result, 32);
                }
                state->current_pc = (pc + advance) & 0xFFFFFFFFu;
                return 12;
            }
        }
        if (mode == 4u && size == 0x0u) {
            dk_u32 addr = (state->a_regs[reg] - dk_native_postinc_bytes(reg, 1)) & 0xFFFFFFFFu;
            dk_u8 lhs;
            dk_u8 rhs;
            dk_u8 result;
            state->a_regs[reg] = addr;
            lhs = dk_native_read_bus_byte(state, addr & 0x00FFFFFFu);
            rhs = (dk_u8)(quick & 0xFFu);
            result = is_sub ? (dk_u8)(lhs - rhs) : (dk_u8)(lhs + rhs);
            dk_native_write_bus_byte(state, addr & 0x00FFFFFFu, result);
            dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    /* ASL/ASR Dn, Dn */
    if ((opcode & 0xF018u) == 0xE000u && (opcode & 0x0020u) != 0u) {
        int count_reg = (opcode >> 9) & 0x7u;
        int count = (int)(state->d_regs[count_reg] & 0x3Fu);
        int left = (opcode & 0x0100u) != 0;
        int size = (opcode >> 6) & 0x3u;
        int reg = opcode & 0x7u;
        dk_u32 mask;
        dk_u32 sign;
        dk_u32 value;
        dk_u8 last_out = 0;
        dk_u8 overflow = 0;
        dk_u16 sr;
        int width;
        int i;

        if (((opcode >> 3) & 0x3u) != 0x0u || size == 0x3u) {
            return 0;
        }
        width = size == 0x0u ? 8 : (size == 0x1u ? 16 : 32);
        mask = width == 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
        sign = width == 32 ? 0x80000000u : (1u << (width - 1));
        value = state->d_regs[reg] & mask;

        if (count == 0) {
            sr = state->current_sr & (dk_u16)~0x000Fu;
            if ((value & sign) != 0u) sr |= 0x0008u;
            if (value == 0u) sr |= 0x0004u;
        } else {
            sr = state->current_sr & (dk_u16)~0x001Fu;
            for (i = 0; i < count; ++i) {
                if (left) {
                    dk_u8 old_sign = (dk_u8)((value & sign) != 0u);
                    last_out = old_sign;
                    value = (value << 1) & mask;
                    if ((dk_u8)((value & sign) != 0u) != old_sign) {
                        overflow = 1;
                    }
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = ((value >> 1) | (value & sign)) & mask;
                }
            }
            if (last_out != 0u) sr |= 0x0011u;
            if ((value & sign) != 0u) sr |= 0x0008u;
            if (value == 0u) sr |= 0x0004u;
            if (left && overflow != 0u) sr |= 0x0002u;
        }

        state->d_regs[reg] = (state->d_regs[reg] & ~mask) | value;
        state->current_sr = sr;
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6 + (count * 2);
    }

    /* ASL/ASR #n, Dn */
    if ((opcode & 0xF018u) == 0xE000u && (opcode & 0x0020u) == 0u) {
        int count = (opcode >> 9) & 0x7u;
        int left = (opcode & 0x0100u) != 0;
        int size = (opcode >> 6) & 0x3u;
        int reg = opcode & 0x7u;
        dk_u16 sr = state->current_sr & (dk_u16)~0x001Fu;
        if (count == 0) {
            count = 8;
        }
        if (((opcode >> 3) & 0x3u) != 0x0u) {
            return 0;
        }
        if (size == 0x0u) {
            dk_u8 value = (dk_u8)(state->d_regs[reg] & 0xFFu);
            dk_u8 last_out = 0;
            dk_u8 overflow = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    dk_u8 old_sign = (dk_u8)((value >> 7) & 0x1u);
                    last_out = (dk_u8)((value >> 7) & 0x1u);
                    value = (dk_u8)(value << 1);
                    if (((value >> 7) & 0x1u) != old_sign) {
                        overflow = 1;
                    }
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (dk_u8)((value >> 1) | (value & 0x80u));
                }
            }
            state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)value;
            sr |= (dk_u16)(((last_out ? 1 : 0) << 4)
                | ((value & 0x80u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (overflow ? 0x2u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
        if (size == 0x1u) {
            dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
            dk_u8 last_out = 0;
            dk_u8 overflow = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    dk_u8 old_sign = (dk_u8)((value >> 15) & 0x1u);
                    last_out = (dk_u8)((value >> 15) & 0x1u);
                    value = (dk_u16)(value << 1);
                    if (((value >> 15) & 0x1u) != old_sign) {
                        overflow = 1;
                    }
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (dk_u16)((value >> 1) | (value & 0x8000u));
                }
            }
            state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)value;
            sr |= (dk_u16)(((last_out ? 1 : 0) << 4)
                | ((value & 0x8000u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (overflow ? 0x2u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
        if (size == 0x2u) {
            dk_u32 value = state->d_regs[reg];
            dk_u8 last_out = 0;
            dk_u8 overflow = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    dk_u8 old_sign = (dk_u8)((value >> 31) & 0x1u);
                    last_out = (dk_u8)((value >> 31) & 0x1u);
                    value <<= 1;
                    if (((value >> 31) & 0x1u) != old_sign) {
                        overflow = 1;
                    }
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (value >> 1) | (value & 0x80000000u);
                }
            }
            state->d_regs[reg] = value;
            sr |= (dk_u16)(((last_out ? 1 : 0) << 4)
                | ((value & 0x80000000u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (overflow ? 0x2u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
    }

    /* LSL/LSR Dn, Dn (word size) */
    if (
        (opcode & 0xF018u) == 0xE008u
        && (opcode & 0x0020u) != 0u
        && ((opcode >> 6) & 0x3u) == 0x1u
    ) {
        int count_reg = (opcode >> 9) & 0x7u;
        int count = (int)(state->d_regs[count_reg] & 0x3Fu);
        int left = (opcode & 0x0100u) != 0;
        int reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
        dk_u8 last_out = 0;
        dk_u16 sr = state->current_sr & (dk_u16)~0x000Fu;
        int i;
        for (i = 0; i < count; ++i) {
            if (left) {
                last_out = (dk_u8)((value >> 15) & 0x1u);
                value = (dk_u16)(value << 1);
            } else {
                last_out = (dk_u8)(value & 0x1u);
                value = (dk_u16)(value >> 1);
            }
        }
        state->d_regs[reg] = (
            state->d_regs[reg] & 0xFFFF0000u
        ) | (dk_u32)value;
        if (count > 0) {
            sr &= (dk_u16)~0x0010u;
            if (last_out != 0u) {
                sr |= 0x0011u;
            }
        }
        if ((value & 0x8000u) != 0u) {
            sr |= 0x0008u;
        }
        if (value == 0u) {
            sr |= 0x0004u;
        }
        state->current_sr = sr;
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 6 + (count * 2);
    }

    /* LSL/LSR #n, Dn */
    if ((opcode & 0xF018u) == 0xE008u && (opcode & 0x0020u) == 0u) {
        int count = (opcode >> 9) & 0x7u;
        int left = (opcode & 0x0100u) != 0;
        int size = (opcode >> 6) & 0x3u;
        int reg = opcode & 0x7u;
        dk_u16 sr = state->current_sr & (dk_u16)~0x001Fu;
        if (count == 0) {
            count = 8;
        }
        if (((opcode >> 3) & 0x3u) != 0x1u) {
            return 0;
        }
        if (size == 0x0u) {
            dk_u8 value = (dk_u8)(state->d_regs[reg] & 0xFFu);
            dk_u8 last_out = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    last_out = (dk_u8)((value >> 7) & 0x1u);
                    value = (dk_u8)(value << 1);
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (dk_u8)(value >> 1);
                }
            }
            state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)value;
            sr |= (dk_u16)(((last_out ? 1 : 0) << 4)
                | ((value & 0x80u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
        if (size == 0x1u) {
            dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
            dk_u8 last_out = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    last_out = (dk_u8)((value >> 15) & 0x1u);
                    value = (dk_u16)(value << 1);
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (dk_u16)(value >> 1);
                }
            }
            state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)value;
            sr |= (dk_u16)(((last_out ? 1 : 0) << 4)
                | ((value & 0x8000u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
        if (size == 0x2u) {
            dk_u32 value = state->d_regs[reg];
            dk_u8 last_out = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    last_out = (dk_u8)((value >> 31) & 0x1u);
                    value <<= 1;
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value >>= 1;
                }
            }
            state->d_regs[reg] = value;
            sr |= (dk_u16)(((last_out ? 1 : 0) << 4)
                | ((value & 0x80000000u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
    }

    /* ROL/ROR #n, Dn */
    if ((opcode & 0xF018u) == 0xE018u && (opcode & 0x0020u) == 0u) {
        int count = (opcode >> 9) & 0x7u;
        int left = (opcode & 0x0100u) != 0;
        int size = (opcode >> 6) & 0x3u;
        int reg = opcode & 0x7u;
        dk_u16 sr = state->current_sr & (dk_u16)~0x000Fu;
        if (count == 0) {
            count = 8;
        }
        if (((opcode >> 3) & 0x3u) != 0x3u) {
            return 0;
        }
        if (size == 0x0u) {
            dk_u8 value = (dk_u8)(state->d_regs[reg] & 0xFFu);
            dk_u8 last_out = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    last_out = (dk_u8)((value >> 7) & 0x1u);
                    value = (dk_u8)((value << 1) | last_out);
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (dk_u8)((value >> 1) | (last_out << 7));
                }
            }
            state->d_regs[reg] = (state->d_regs[reg] & 0xFFFFFF00u) | (dk_u32)value;
            sr |= (dk_u16)(((value & 0x80u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
        if (size == 0x1u) {
            dk_u16 value = (dk_u16)(state->d_regs[reg] & 0xFFFFu);
            dk_u8 last_out = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    last_out = (dk_u8)((value >> 15) & 0x1u);
                    value = (dk_u16)((value << 1) | last_out);
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (dk_u16)((value >> 1) | ((dk_u16)last_out << 15));
                }
            }
            state->d_regs[reg] = (state->d_regs[reg] & 0xFFFF0000u) | (dk_u32)value;
            sr |= (dk_u16)(((value & 0x8000u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
        if (size == 0x2u) {
            dk_u32 value = state->d_regs[reg];
            dk_u8 last_out = 0;
            int i;
            for (i = 0; i < count; ++i) {
                if (left) {
                    last_out = (dk_u8)((value >> 31) & 0x1u);
                    value = (value << 1) | (dk_u32)last_out;
                } else {
                    last_out = (dk_u8)(value & 0x1u);
                    value = (value >> 1) | ((dk_u32)last_out << 31);
                }
            }
            state->d_regs[reg] = value;
            sr |= (dk_u16)(((value & 0x80000000u) ? 0x8u : 0u)
                | ((value == 0) ? 0x4u : 0u)
                | (last_out ? 0x1u : 0u));
            state->current_sr = sr;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 6 + (count * 2);
        }
    }

    /* MOVE.B (An)+, Dn and MOVE.B (An), Dn - 0x1000 base */
    if ((opcode & 0xF1C0u) == 0x1000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr, value;

        if (src_mode == 0x3u) {
            /* MOVE.B (An)+, Dn */
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            if (src_reg == 7u) {
                state->a_regs[src_reg] = (state->a_regs[src_reg] + 2u) & 0xFFFFFFFFu;
            } else {
                state->a_regs[src_reg] = (state->a_regs[src_reg] + 1u) & 0xFFFFFFFFu;
            }
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x2u) {
            /* MOVE.B (An), Dn */
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x5u) {
            /* MOVE.B (d16, An), Dn */
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            value = dk_native_read_bus_byte(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x6u) {
            /* MOVE.B (d8, An, Xn), Dn */
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[src_reg] & 0x00FFFFFFu, ext);
            value = dk_native_read_bus_byte(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFFFF00u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x80u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    /* MOVE.W <ea>, Dn */
    if ((opcode & 0xF1C0u) == 0x3000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr;
        dk_u16 value;

        if (src_mode == 0x0u) {
            value = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x1u) {
            value = (dk_u16)(state->a_regs[src_reg] & 0xFFFFu);
            state->d_regs[dst_reg] = (
                state->d_regs[dst_reg] & 0xFFFF0000u
            ) | (dk_u32)value;
            dk_native_set_nzvc(
                state,
                (value & 0x8000u) != 0,
                value == 0,
                0,
                0
            );
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 4;
        }
        if (src_mode == 0x2u) {
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            value = dk_native_read_bus_word(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x3u) {
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            value = dk_native_read_bus_word(state, addr);
            state->a_regs[src_reg] = (state->a_regs[src_reg] + 2u) & 0xFFFFFFFFu;
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x5u) {
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            value = dk_native_read_bus_word(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x6u) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[src_reg] & 0x00FFFFFFu, ext);
            value = dk_native_read_bus_word(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 10;
        }
        if (src_mode == 0x7u && src_reg == 0x0u) {
            addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            value = dk_native_read_bus_word(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x7u && src_reg == 0x1u) {
            addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            value = dk_native_read_bus_word(state, addr);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x7u && src_reg == 0x4u) {
            value = dk_native_read_bus_word(state, pc + 2u);
            state->d_regs[dst_reg] = (state->d_regs[dst_reg] & 0xFFFF0000u) | (dk_u32)value;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    /* MOVEA.L <ea>, An */
    if ((opcode & 0xF1C0u) == 0x2040u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr;
        dk_u32 value;

        if (src_mode == 0x0u) {
            state->a_regs[dst_reg] = state->d_regs[src_reg] & 0xFFFFFFFFu;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 0x1u) {
            state->a_regs[dst_reg] = state->a_regs[src_reg] & 0xFFFFFFFFu;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 4;
        }
        if (src_mode == 0x2u) {
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            state->a_regs[dst_reg] = dk_native_read_bus_long(state, addr);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 0x5u) {
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            state->a_regs[dst_reg] = dk_native_read_bus_long(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 0x6u) {
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[src_reg] & 0x00FFFFFFu, ext);
            state->a_regs[dst_reg] = dk_native_read_bus_long(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 14;
        }
        if (src_mode == 0x7u && src_reg == 0x0u) {
            addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            state->a_regs[dst_reg] = dk_native_read_bus_long(state, addr);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 0x7u && src_reg == 0x1u) {
            addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu;
            state->a_regs[dst_reg] = dk_native_read_bus_long(state, addr);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 16;
        }
        if (src_mode == 0x7u && src_reg == 0x4u) {
            value = dk_native_read_bus_long(state, pc + 2u);
            state->a_regs[dst_reg] = value;
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 12;
        }
    }

    /* MOVE.W/#imm and MOVE.L/#imm setup writes used by the live checkpoint */
    if ((opcode & 0xF000u) == 0x3000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int dst_mode = (opcode >> 6) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        if (src_mode == 7u && src_reg == 4u && dst_mode == 2u) {
            dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 3u) {
            dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            state->a_regs[dst_reg] = (state->a_regs[dst_reg] + 2u) & 0xFFFFFFFFu;
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 5u) {
            dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
            int disp = dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 4u)
            );
            dk_u32 addr = (
                state->a_regs[dst_reg] + (dk_u32)disp
            ) & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            dk_native_set_nzvc(
                state,
                (value & 0x8000u) != 0,
                value == 0,
                0,
                0
            );
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 7u && dst_reg == 0u) {
            dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
            dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 7u && dst_reg == 1u) {
            dk_u16 value = dk_native_read_bus_word(state, pc + 2u);
            dk_u32 addr = dk_native_read_bus_long(state, pc + 4u) & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 8u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 0u && dst_mode == 2u) {
            dk_u32 src_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu;
            dk_u16 value = dk_native_read_bus_word(state, src_addr);
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_word(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8;
        }
    }

    if ((opcode & 0xF000u) == 0x2000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int dst_mode = (opcode >> 6) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        if (src_mode == 7u && src_reg == 4u && dst_mode == 2u) {
            dk_u32 value = dk_native_read_bus_long(state, pc + 2u);
            dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
            dk_native_write_bus_long(state, addr, value);
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return 8;
        }
        if (src_mode == 7u && src_reg == 4u && dst_mode == 5u) {
            dk_u32 value = dk_native_read_bus_long(state, pc + 2u);
            int disp = dk_native_sign_extend16(
                dk_native_read_bus_word(state, pc + 6u)
            );
            dk_u32 addr = (
                state->a_regs[dst_reg] + (dk_u32)disp
            ) & 0x00FFFFFFu;
            dk_native_write_bus_long(state, addr, value);
            dk_native_set_nzvc(
                state,
                (value & 0x80000000u) != 0,
                value == 0,
                0,
                0
            );
            state->current_pc = (pc + 8u) & 0xFFFFFFFFu;
            return 16;
        }
    }

    if ((opcode & 0xFFC0u) == 0x4CC0u) {
        dk_u16 mask = dk_native_read_bus_word(state, pc + 2u);
        int mode = (opcode >> 3) & 0x7u;
        int reg = opcode & 0x7u;
        dk_u32 addr;
        dk_u32 advance;
        int bit;
        if (mode == 7u && reg == 0u) {
            addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
            advance = 6u;
        } else if (mode == 3u) {
            addr = state->a_regs[reg] & 0x00FFFFFFu;
            advance = 4u;
        } else {
            return 0;
        }
        for (bit = 0; bit < 16; ++bit) {
            if ((mask & (dk_u16)(1u << bit)) == 0u) {
                continue;
            }
            if (bit < 8) {
                state->d_regs[bit] = dk_native_read_bus_long(state, addr);
            } else {
                state->a_regs[bit - 8] = dk_native_read_bus_long(state, addr);
            }
            addr = (addr + 4u) & 0x00FFFFFFu;
        }
        if (mode == 3u) {
            state->a_regs[reg] = (state->a_regs[reg] + (dk_u32)(4 * __builtin_popcount((unsigned int)mask))) & 0xFFFFFFFFu;
        }
        state->current_pc = (pc + advance) & 0xFFFFFFFFu;
        return 20;
    }

    if (opcode == 0x48F8u) {
        dk_u16 mask = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 4u)) & 0x00FFFFFFu;
        int bit;
        for (bit = 0; bit < 16; ++bit) {
            if ((mask & (dk_u16)(1u << bit)) == 0u) {
                continue;
            }
            if (bit < 8) {
                dk_native_write_bus_long(state, addr, state->d_regs[bit]);
            } else {
                dk_native_write_bus_long(state, addr, state->a_regs[bit - 8]);
            }
            addr = (addr + 4u) & 0x00FFFFFFu;
        }
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 20;
    }

    if ((opcode & 0xFB80u) == 0x4880u) {
        dk_u16 mask = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 size_bytes = (opcode & 0x0040u) ? 4u : 2u;
        dk_u32 mode = (opcode >> 3) & 0x7u;
        dk_u32 reg = opcode & 0x7u;
        int bit;

        if (opcode & 0x0400u) {
            return 0;
        }

        if (mode == 4u) {
            dk_u32 addr;
            dk_u32 values[16];
            int value_count = 0;
            for (bit = 0; bit < 16; ++bit) {
                dk_u32 value;
                if ((mask & (dk_u16)(1u << bit)) == 0u) {
                    continue;
                }
                if (bit < 8) {
                    value = state->a_regs[7 - bit];
                } else {
                    value = state->d_regs[15 - bit];
                }
                values[value_count++] = value;
            }
            addr = (state->a_regs[reg] - (size_bytes * (dk_u32)value_count)) & 0xFFFFFFFFu;
            state->a_regs[reg] = addr & 0xFFFFFFFFu;
            while (value_count > 0) {
                dk_u32 value = values[--value_count];
                if (size_bytes == 4u) {
                    dk_native_write_bus_long(state, addr & 0x00FFFFFFu, value);
                } else {
                    dk_native_write_bus_word(state, addr & 0x00FFFFFFu, (dk_u16)(value & 0xFFFFu));
                }
                addr = (addr + size_bytes) & 0xFFFFFFFFu;
            }
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8 + (int)(size_bytes * (dk_u32)__builtin_popcount((unsigned int)mask));
        }

        if (mode == 2u) {
            dk_u32 addr = state->a_regs[reg] & 0x00FFFFFFu;
            for (bit = 0; bit < 16; ++bit) {
                dk_u32 value;
                if ((mask & (dk_u16)(1u << bit)) == 0u) {
                    continue;
                }
                value = bit < 8
                    ? state->d_regs[bit]
                    : state->a_regs[bit - 8];
                if (size_bytes == 4u) {
                    dk_native_write_bus_long(state, addr, value);
                } else {
                    dk_native_write_bus_word(state, addr, (dk_u16)(value & 0xFFFFu));
                }
                addr = (addr + size_bytes) & 0x00FFFFFFu;
            }
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 8 + (int)(size_bytes * (dk_u32)__builtin_popcount((unsigned int)mask));
        }

        return 0;
    }

    /* MOVE.W Dn, (An) - 0x3080 base */
    if ((opcode & 0xF1F8u) == 0x3080u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
        dk_native_write_bus_word(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W An, (An) - address-register direct source */
    if ((opcode & 0xF1F8u) == 0x3088u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->a_regs[src_reg] & 0xFFFFu);
        dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
        dk_native_write_bus_word(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W Dn, (An)+ - 0x30C0 base */
    if ((opcode & 0xF1F8u) == 0x30C0u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u32 addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
        dk_native_write_bus_word(state, addr, value);
        state->a_regs[dst_reg] = (state->a_regs[dst_reg] + 2u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W (An)+, (Am) - source postincrement to address-indirect.
     * The palette/PLC copy loops use this form (ROM $309A = (A2)+ -> (A0)). */
    if (
        (opcode & 0xF000u) == 0x3000u
        && ((opcode >> 6) & 0x7u) == 2u
        && ((opcode >> 3) & 0x7u) == 3u
    ) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src_reg] & 0x00FFFFFFu;
        dk_u32 dst_addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        state->a_regs[src_reg] = (state->a_regs[src_reg] + 2u) & 0xFFFFFFFFu;
        dk_native_write_bus_word(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W Dn, -(An) */
    if ((opcode & 0xF1F8u) == 0x3100u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 value = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u32 addr = (state->a_regs[dst_reg] - 2u) & 0xFFFFFFFFu;
        state->a_regs[dst_reg] = addr;
        dk_native_write_bus_word(state, addr & 0x00FFFFFFu, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W d16(An), -(An).
     * GHZ's moving platforms preserve their previous X coordinate this way
     * before carrying Sonic to their new position. */
    if ((opcode & 0xF1F8u) == 0x3128u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u32 src_addr = (
            state->a_regs[src_reg] + (dk_u32)disp
        ) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_u32 dst_addr = (state->a_regs[dst_reg] - 2u) & 0xFFFFFFFFu;
        state->a_regs[dst_reg] = dst_addr;
        dk_native_write_bus_word(state, dst_addr & 0x00FFFFFFu, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W d16(An), (An)+ */
    if ((opcode & 0xF1F8u) == 0x30E8u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u32 src_addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_u32 dst_addr = state->a_regs[dst_reg] & 0x00FFFFFFu;
        dk_native_write_bus_word(state, dst_addr, value);
        state->a_regs[dst_reg] = (state->a_regs[dst_reg] + 2u) & 0xFFFFFFFFu;
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.W (An), d16(An).
     * GotThroughCard copies each card start coordinate from its table this
     * way before the adjacent post-increment records provide the endpoints. */
    if ((opcode & 0xF1F8u) == 0x3150u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        dk_u16 value = dk_native_read_bus_word(
            state,
            state->a_regs[src_reg] & 0x00FFFFFFu
        );
        dk_native_write_bus_word(
            state,
            (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W (An)+, d16(An) */
    if ((opcode & 0xF1F8u) == 0x3158u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 src_addr = state->a_regs[src_reg] & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        int disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        state->a_regs[src_reg] = (
            state->a_regs[src_reg] + 2u
        ) & 0xFFFFFFFFu;
        dk_native_write_bus_word(
            state,
            (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.W d16(An), d16(An) */
    if ((opcode & 0xF1F8u) == 0x3168u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int src_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        );
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u16 value = dk_native_read_bus_word(
            state,
            (state->a_regs[src_reg] + (dk_u32)src_disp) & 0x00FFFFFFu
        );
        dk_native_write_bus_word(
            state,
            (state->a_regs[dst_reg] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.W (xxx).W, d16(An).
     * Nick Arcade uses this form in its player/object synchronisation path
     * ($3178 / v_player2+obX).  Leaving it unsupported demotes the whole
     * gameplay frame to the Python reference core as soon as the second
     * player record is updated. */
    if ((opcode & 0xF1FFu) == 0x3178u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 src_addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(
            state,
            (state->a_regs[dst_reg] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.W (xxx).W, d8(An,Xn) */
    if ((opcode & 0xF1FFu) == 0x31B8u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u32 src_addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 2u)
        ) & 0x00FFFFFFu;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 4u);
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(
            state,
            dk_native_index_brief(
                state,
                state->a_regs[dst_reg] & 0x00FFFFFFu,
                ext
            ),
            value
        );
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.W d8(An,Xn), (xxx).W */
    if ((opcode & 0xF1F8u) == 0x31F0u) {
        int src_reg = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src_reg] & 0x00FFFFFFu,
            ext
        );
        dk_u32 dst_addr = (dk_u32)dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        ) & 0x00FFFFFFu;
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(state, dst_addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.B d8(PC,Xn), d16(An) */
    if ((opcode & 0xF1FFu) == 0x117Bu) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 src_addr = dk_native_index_brief(
            state,
            (pc + 2u) & 0x00FFFFFFu,
            ext
        );
        dk_u8 value = dk_native_read_bus_byte(state, src_addr);
        dk_native_write_bus_byte(
            state,
            (state->a_regs[dst_reg] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x80u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.W d8(PC,Xn), d16(An) */
    if ((opcode & 0xF1FFu) == 0x317Bu) {
        int dst_reg = (opcode >> 9) & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 src_addr = dk_native_index_brief(
            state,
            (pc + 2u) & 0x00FFFFFFu,
            ext
        );
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(
            state,
            (state->a_regs[dst_reg] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 16;
    }

    /* MOVE.W d8(An,Xn), d16(An) */
    if ((opcode & 0xF1F8u) == 0x3170u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        int dst_disp = dk_native_sign_extend16(
            dk_native_read_bus_word(state, pc + 4u)
        );
        dk_u32 src_addr = dk_native_index_brief(
            state,
            state->a_regs[src_reg] & 0x00FFFFFFu,
            ext
        );
        dk_u16 value = dk_native_read_bus_word(state, src_addr);
        dk_native_write_bus_word(
            state,
            (state->a_regs[dst_reg] + (dk_u32)dst_disp) & 0x00FFFFFFu,
            value
        );
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.W Dn, d16(An) - 0x3140 base */
    if ((opcode & 0xF1F8u) == 0x3140u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
        dk_u16 value = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u32 addr = (state->a_regs[dst_reg] + (dk_u32)disp) & 0x00FFFFFFu;
        dk_native_write_bus_word(state, addr, value);
        dk_native_set_nzvc(state, (value & 0x8000u) != 0, value == 0, 0, 0);
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 8;
    }

    /* MOVE.W Dn, d8(An,Xn) */
    if ((opcode & 0xF1F8u) == 0x3180u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
        dk_u16 value = (dk_u16)(state->d_regs[src_reg] & 0xFFFFu);
        dk_u32 addr = dk_native_index_brief(
            state,
            state->a_regs[dst_reg] & 0x00FFFFFFu,
            ext
        );
        dk_native_write_bus_word(state, addr, value);
        dk_native_set_nzvc(
            state,
            (value & 0x8000u) != 0,
            value == 0,
            0,
            0
        );
        state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
        return 12;
    }

    /* MOVE.L (An)+, Dn and MOVE.L (An), Dn - 0x2000 base */
    if ((opcode & 0xF1C0u) == 0x2000u) {
        int dst_reg = (opcode >> 9) & 0x7u;
        int src_mode = (opcode >> 3) & 0x7u;
        int src_reg = opcode & 0x7u;
        dk_u32 addr, value;

        if (src_mode == 0x3u) {
            /* MOVE.L (An)+, Dn */
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            value = dk_native_read_bus_long(state, addr);
            state->a_regs[src_reg] = (state->a_regs[src_reg] + 4u) & 0xFFFFFFFFu;
            state->d_regs[dst_reg] = value;
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 0x2u) {
            /* MOVE.L (An), Dn */
            addr = state->a_regs[src_reg] & 0x00FFFFFFu;
            value = dk_native_read_bus_long(state, addr);
            state->d_regs[dst_reg] = value;
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 12;
        }
        if (src_mode == 0x5u) {
            /* MOVE.L d16(An), Dn */
            int disp = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u));
            addr = (state->a_regs[src_reg] + (dk_u32)disp) & 0x00FFFFFFu;
            value = dk_native_read_bus_long(state, addr);
            state->d_regs[dst_reg] = value;
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 16;
        }
        if (src_mode == 0x6u) {
            /* MOVE.L (d8,An,Xn), Dn */
            dk_u16 ext = dk_native_read_bus_word(state, pc + 2u);
            addr = dk_native_index_brief(state, state->a_regs[src_reg] & 0x00FFFFFFu, ext);
            value = dk_native_read_bus_long(state, addr);
            state->d_regs[dst_reg] = value;
            dk_native_set_nzvc(state, (value & 0x80000000u) != 0, value == 0, 0, 0);
            state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            return 16;
        }
    }

    /* ====================================================================
     * ADDQ/SUBQ #n, Dn/An  (0x5000-0x5FFF)
     * Quick arithmetic: ADDQ adds 1-8, SUBQ subtracts 1-8.
     * ==================================================================== */
    if ((opcode & 0xF000u) == 0x5000u && (opcode & 0x00C0u) != 0x00C0u) {
        int is_subq;
        int data;
        int size;
        int ea_mode;
        int ea_reg;
        is_subq = (opcode & 0x0100u) != 0;
        data = (opcode >> 9) & 0x7u;
        if (data == 0) data = 8;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;

        if (ea_mode == 0x1u) {
            /* Dn */
            dk_u32 src = (dk_u32)data;
            dk_u32 dst = state->d_regs[ea_reg];
            dk_u32 result;
            if (is_subq) {
                result = (dst - src) & 0xFFFFFFFFu;
                dk_native_set_sub_flags(state, (int)src, (int)dst, (int)result, size);
            } else {
                result = (dst + src) & 0xFFFFFFFFu;
                dk_native_set_add_flags(state, (int)src, (int)dst, (int)result, size);
            }
            if (size == 0) state->d_regs[ea_reg] = (state->d_regs[ea_reg] & 0xFFFFFF00u) | (result & 0xFFu);
            else if (size == 1) state->d_regs[ea_reg] = (state->d_regs[ea_reg] & 0xFFFF0000u) | (result & 0xFFFFu);
            else state->d_regs[ea_reg] = result;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 4;
        }
        if (ea_mode == 0x1u) {
            /* An - ADDQ/SUBQ to address register: no flags, always long */
            dk_u32 src = (dk_u32)data;
            dk_u32 dst = state->a_regs[ea_reg];
            if (is_subq) state->a_regs[ea_reg] = (dst - src) & 0xFFFFFFFFu;
            else state->a_regs[ea_reg] = (dst + src) & 0xFFFFFFFFu;
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            return 4;
        }
        /* EA targets: (An), (An)+, -(An), d16(An), d8(An,Xn), (xxx).W, (xxx).L */
        {
            dk_u32 ea_addr = 0;
            dk_u32 src_val = (dk_u32)data;
            int cycles = 8;
            if (ea_mode == 0x2u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; }
            else if (ea_mode == 0x3u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; state->a_regs[ea_reg] = (state->a_regs[ea_reg] + (size == 0 ? 1 : (size == 1 ? 2 : 4))) & 0xFFFFFFFFu; }
            else if (ea_mode == 0x4u) { state->a_regs[ea_reg] = (state->a_regs[ea_reg] - (size == 0 ? 1 : (size == 1 ? 2 : 4))) & 0xFFFFFFFFu; ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; cycles = 8; }
            else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; cycles = 12; }
            else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + 2u); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); cycles = 14; }
            else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + 2u)) & 0x00FFFFFFu; cycles = 12; }
            else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + 2u) & 0x00FFFFFFu; cycles = 16; }
            if (size == 0) {
                dk_u8 dst = dk_native_read_bus_byte(state, ea_addr);
                dk_u8 result;
                if (is_subq) { result = (dk_u8)((int)dst - (int)src_val); dk_native_set_sub_flags(state, (int)src_val, (int)dst, (int)result, 0); }
                else { result = (dk_u8)((int)dst + (int)src_val); dk_native_set_add_flags(state, (int)src_val, (int)dst, (int)result, 0); }
                dk_native_write_bus_byte(state, ea_addr, result);
            } else if (size == 1) {
                dk_u16 dst = dk_native_read_bus_word(state, ea_addr);
                dk_u16 result;
                if (is_subq) { result = (dk_u16)((int)dst - (int)src_val); dk_native_set_sub_flags(state, (int)src_val, (int)dst, (int)result, 1); }
                else { result = (dk_u16)((int)dst + (int)src_val); dk_native_set_add_flags(state, (int)src_val, (int)dst, (int)result, 1); }
                dk_native_write_bus_word(state, ea_addr, result);
            } else {
                dk_u32 dst = dk_native_read_bus_long(state, ea_addr);
                dk_u32 result;
                if (is_subq) { result = (dst - src_val); dk_native_set_sub_flags(state, (int)src_val, (int)dst, (int)result, 2); }
                else { result = (dst + src_val); dk_native_set_add_flags(state, (int)src_val, (int)dst, (int)result, 2); }
                dk_native_write_bus_long(state, ea_addr, result);
            }
            state->current_pc = (pc + 2u) & 0xFFFFFFFFu;
            if (ea_mode == 0x5u || ea_mode == 0x7u) state->current_pc = (pc + 4u) & 0xFFFFFFFFu;
            if (ea_mode == 0x7u && ea_reg == 0x1u) state->current_pc = (pc + 6u) & 0xFFFFFFFFu;
            return cycles;
        }
    }

    /* ====================================================================
     * ADDI/SUBI/ORI/ANDI/EORI #imm, <ea>  (0x0n00-0x0nFF range)
     * Immediate ALU operations.
     * ==================================================================== */
    /* ORI #imm, Dn/EA  (0x0000-0x003F) */
    if ((opcode & 0xFF00u) == 0x0000u) {
        int size;
        int ea_mode;
        int ea_reg;
        dk_u32 imm = 0;
        dk_u32 ea_addr = 0;
        int pc_inc = 2;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;
        if (size == 0) { imm = dk_native_read_bus_byte(state, pc + 2u); pc_inc = 4; }
        else if (size == 1) { imm = dk_native_read_bus_word(state, pc + 2u); pc_inc = 4; }
        else { imm = dk_native_read_bus_long(state, pc + 2u); pc_inc = 6; }
        if (ea_mode == 0x0u) {
            dk_u32 dst = state->d_regs[ea_reg];
            dk_u32 result = dst | imm;
            if (size == 0) { result = (dst & 0xFFFFFF00u) | ((dst | imm) & 0xFFu); state->d_regs[ea_reg] = result; }
            else if (size == 1) { result = (dst & 0xFFFF0000u) | ((dst | imm) & 0xFFFFu); state->d_regs[ea_reg] = result; }
            else { state->d_regs[ea_reg] = dst | imm; }
            dk_native_set_nzvc(state, (state->d_regs[ea_reg] & (size==0?0x80u:size==1?0x8000u:0x80000000u)) != 0, state->d_regs[ea_reg] == 0, 0, 0);
            state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
            return (size == 2) ? 8 : 4;
        }
        /* EA targets */
        if (ea_mode == 0x2u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x3u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; state->a_regs[ea_reg] = (state->a_regs[ea_reg] + (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; }
        else if (ea_mode == 0x4u) { state->a_regs[ea_reg] = (state->a_regs[ea_reg] - (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; }
        else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + (dk_u32)pc_inc); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + (dk_u32)pc_inc) & 0x00FFFFFFu; pc_inc += 4; }
        if (size == 0) { dk_u8 dst = dk_native_read_bus_byte(state, ea_addr); dk_u8 result = (dk_u8)(dst | (dk_u8)imm); dk_native_write_bus_byte(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0); }
        else if (size == 1) { dk_u16 dst = dk_native_read_bus_word(state, ea_addr); dk_u16 result = (dk_u16)(dst | (dk_u16)imm); dk_native_write_bus_word(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0); }
        else { dk_u32 dst = dk_native_read_bus_long(state, ea_addr); dk_u32 result = dst | imm; dk_native_write_bus_long(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0); }
        state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
        return (ea_mode >= 5) ? 12 : 8;
    }

    /* ANDI #imm, Dn/EA  (0x0200-0x023F) */
    if ((opcode & 0xFF00u) == 0x0200u) {
        int size;
        int ea_mode;
        int ea_reg;
        dk_u32 imm = 0;
        dk_u32 ea_addr = 0;
        int pc_inc = 2;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;
        if (size == 0) { imm = dk_native_read_bus_byte(state, pc + 2u); pc_inc = 4; }
        else if (size == 1) { imm = dk_native_read_bus_word(state, pc + 2u); pc_inc = 4; }
        else { imm = dk_native_read_bus_long(state, pc + 2u); pc_inc = 6; }
        if (ea_mode == 0x0u) {
            dk_u32 dst = state->d_regs[ea_reg];
            if (size == 0) state->d_regs[ea_reg] = (dst & 0xFFFFFF00u) | ((dst & imm) & 0xFFu);
            else if (size == 1) state->d_regs[ea_reg] = (dst & 0xFFFF0000u) | ((dst & imm) & 0xFFFFu);
            else state->d_regs[ea_reg] = dst & imm;
            dk_native_set_nzvc(state, (state->d_regs[ea_reg] & (size==0?0x80u:size==1?0x8000u:0x80000000u)) != 0, state->d_regs[ea_reg] == 0, 0, 0);
            state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
            return (size == 2) ? 8 : 4;
        }
        if (ea_mode == 0x2u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x3u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; state->a_regs[ea_reg] = (state->a_regs[ea_reg] + (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; }
        else if (ea_mode == 0x4u) { state->a_regs[ea_reg] = (state->a_regs[ea_reg] - (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; }
        else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + (dk_u32)pc_inc); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + (dk_u32)pc_inc) & 0x00FFFFFFu; pc_inc += 4; }
        if (size == 0) { dk_u8 dst = dk_native_read_bus_byte(state, ea_addr); dk_u8 result = (dk_u8)(dst & (dk_u8)imm); dk_native_write_bus_byte(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0); }
        else if (size == 1) { dk_u16 dst = dk_native_read_bus_word(state, ea_addr); dk_u16 result = (dk_u16)(dst & (dk_u16)imm); dk_native_write_bus_word(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0); }
        else { dk_u32 dst = dk_native_read_bus_long(state, ea_addr); dk_u32 result = dst & imm; dk_native_write_bus_long(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0); }
        state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
        return (ea_mode >= 5) ? 12 : 8;
    }

    /* SUBI #imm, Dn/EA  (0x0400-0x043F) */
    if ((opcode & 0xFF00u) == 0x0400u) {
        int size;
        int ea_mode;
        int ea_reg;
        dk_u32 imm = 0;
        dk_u32 ea_addr = 0;
        int pc_inc = 2;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;
        if (size == 0) { imm = dk_native_read_bus_byte(state, pc + 2u); pc_inc = 4; }
        else if (size == 1) { imm = dk_native_read_bus_word(state, pc + 2u); pc_inc = 4; }
        else { imm = dk_native_read_bus_long(state, pc + 2u); pc_inc = 6; }
        if (ea_mode == 0x0u) {
            dk_u32 dst = state->d_regs[ea_reg];
            dk_u32 result = (dst - imm) & (size==0?0xFFu:size==1?0xFFFFu:0xFFFFFFFFu);
            if (size == 0) state->d_regs[ea_reg] = (dst & 0xFFFFFF00u) | (result & 0xFFu);
            else if (size == 1) state->d_regs[ea_reg] = (dst & 0xFFFF0000u) | (result & 0xFFFFu);
            else state->d_regs[ea_reg] = result;
            dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, size);
            state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
            return (size == 2) ? 8 : 4;
        }
        if (ea_mode == 0x2u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x3u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; state->a_regs[ea_reg] = (state->a_regs[ea_reg] + (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; }
        else if (ea_mode == 0x4u) { state->a_regs[ea_reg] = (state->a_regs[ea_reg] - (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; }
        else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + (dk_u32)pc_inc); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + (dk_u32)pc_inc) & 0x00FFFFFFu; pc_inc += 4; }
        if (size == 0) { dk_u8 dst = dk_native_read_bus_byte(state, ea_addr); dk_u8 result = (dk_u8)(dst - (dk_u8)imm); dk_native_write_bus_byte(state, ea_addr, result); dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, 0); }
        else if (size == 1) { dk_u16 dst = dk_native_read_bus_word(state, ea_addr); dk_u16 result = (dk_u16)(dst - (dk_u16)imm); dk_native_write_bus_word(state, ea_addr, result); dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, 1); }
        else { dk_u32 dst = dk_native_read_bus_long(state, ea_addr); dk_u32 result = dst - imm; dk_native_write_bus_long(state, ea_addr, result); dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, 2); }
        state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
        return (ea_mode >= 5) ? 12 : 8;
    }

    /* ADDI #imm, Dn/EA  (0x0600-0x063F) */
    if ((opcode & 0xFF00u) == 0x0600u) {
        int size;
        int ea_mode;
        int ea_reg;
        dk_u32 imm = 0;
        dk_u32 ea_addr = 0;
        int pc_inc = 2;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;
        if (size == 0) { imm = dk_native_read_bus_byte(state, pc + 2u); pc_inc = 4; }
        else if (size == 1) { imm = dk_native_read_bus_word(state, pc + 2u); pc_inc = 4; }
        else { imm = dk_native_read_bus_long(state, pc + 2u); pc_inc = 6; }
        if (ea_mode == 0x0u) {
            dk_u32 dst = state->d_regs[ea_reg];
            dk_u32 result = (dst + imm) & (size==0?0xFFu:size==1?0xFFFFu:0xFFFFFFFFu);
            if (size == 0) state->d_regs[ea_reg] = (dst & 0xFFFFFF00u) | (result & 0xFFu);
            else if (size == 1) state->d_regs[ea_reg] = (dst & 0xFFFF0000u) | (result & 0xFFFFu);
            else state->d_regs[ea_reg] = result;
            dk_native_set_add_flags(state, (int)imm, (int)dst, (int)result, size);
            state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
            return (size == 2) ? 8 : 4;
        }
        if (ea_mode == 0x2u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x3u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; state->a_regs[ea_reg] = (state->a_regs[ea_reg] + (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; }
        else if (ea_mode == 0x4u) { state->a_regs[ea_reg] = (state->a_regs[ea_reg] - (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; }
        else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + (dk_u32)pc_inc); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + (dk_u32)pc_inc) & 0x00FFFFFFu; pc_inc += 4; }
        if (size == 0) { dk_u8 dst = dk_native_read_bus_byte(state, ea_addr); dk_u8 result = (dk_u8)(dst + (dk_u8)imm); dk_native_write_bus_byte(state, ea_addr, result); dk_native_set_add_flags(state, (int)imm, (int)dst, (int)result, 0); }
        else if (size == 1) { dk_u16 dst = dk_native_read_bus_word(state, ea_addr); dk_u16 result = (dk_u16)(dst + (dk_u16)imm); dk_native_write_bus_word(state, ea_addr, result); dk_native_set_add_flags(state, (int)imm, (int)dst, (int)result, 1); }
        else { dk_u32 dst = dk_native_read_bus_long(state, ea_addr); dk_u32 result = dst + imm; dk_native_write_bus_long(state, ea_addr, result); dk_native_set_add_flags(state, (int)imm, (int)dst, (int)result, 2); }
        state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
        return (ea_mode >= 5) ? 12 : 8;
    }

    /* EORI #imm, Dn/EA  (0x0A00-0x0A3F) */
    if ((opcode & 0xFF00u) == 0x0A00u) {
        int size;
        int ea_mode;
        int ea_reg;
        dk_u32 imm = 0;
        dk_u32 ea_addr = 0;
        int pc_inc = 2;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;
        if (size == 0) { imm = dk_native_read_bus_byte(state, pc + 2u); pc_inc = 4; }
        else if (size == 1) { imm = dk_native_read_bus_word(state, pc + 2u); pc_inc = 4; }
        else { imm = dk_native_read_bus_long(state, pc + 2u); pc_inc = 6; }
        if (ea_mode == 0x0u) {
            dk_u32 dst = state->d_regs[ea_reg];
            if (size == 0) state->d_regs[ea_reg] = (dst & 0xFFFFFF00u) | ((dst ^ imm) & 0xFFu);
            else if (size == 1) state->d_regs[ea_reg] = (dst & 0xFFFF0000u) | ((dst ^ imm) & 0xFFFFu);
            else state->d_regs[ea_reg] = dst ^ imm;
            dk_native_set_nzvc(state, (state->d_regs[ea_reg] & (size==0?0x80u:size==1?0x8000u:0x80000000u)) != 0, state->d_regs[ea_reg] == 0, 0, 0);
            state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
            return (size == 2) ? 8 : 4;
        }
        if (ea_mode == 0x2u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x3u) { ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; state->a_regs[ea_reg] = (state->a_regs[ea_reg] + (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; }
        else if (ea_mode == 0x4u) { state->a_regs[ea_reg] = (state->a_regs[ea_reg] - (size==0?1:size==1?2:4)) & 0xFFFFFFFFu; ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu; }
        else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + (dk_u32)pc_inc); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + (dk_u32)pc_inc) & 0x00FFFFFFu; pc_inc += 4; }
        if (size == 0) { dk_u8 dst = dk_native_read_bus_byte(state, ea_addr); dk_u8 result = (dk_u8)(dst ^ (dk_u8)imm); dk_native_write_bus_byte(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x80u) != 0, result == 0, 0, 0); }
        else if (size == 1) { dk_u16 dst = dk_native_read_bus_word(state, ea_addr); dk_u16 result = (dk_u16)(dst ^ (dk_u16)imm); dk_native_write_bus_word(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x8000u) != 0, result == 0, 0, 0); }
        else { dk_u32 dst = dk_native_read_bus_long(state, ea_addr); dk_u32 result = dst ^ imm; dk_native_write_bus_long(state, ea_addr, result); dk_native_set_nzvc(state, (result & 0x80000000u) != 0, result == 0, 0, 0); }
        state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
        return (ea_mode >= 5) ? 12 : 8;
    }

    /* CMPI #imm, Dn/EA  (0x0C00-0x0C3F) */
    if ((opcode & 0xFF00u) == 0x0C00u) {
        int size;
        int ea_mode;
        int ea_reg;
        dk_u32 imm = 0;
        dk_u32 ea_addr = 0;
        int pc_inc = 2;
        size = (opcode >> 6) & 0x3u;
        ea_mode = (opcode >> 3) & 0x7u;
        ea_reg = opcode & 0x7u;
        if (size == 0) { imm = dk_native_read_bus_byte(state, pc + 2u); pc_inc = 4; }
        else if (size == 1) { imm = dk_native_read_bus_word(state, pc + 2u); pc_inc = 4; }
        else { imm = dk_native_read_bus_long(state, pc + 2u); pc_inc = 6; }
        if (ea_mode == 0x0u) {
            dk_u32 dst = state->d_regs[ea_reg] & (size==0?0xFFu:size==1?0xFFFFu:0xFFFFFFFFu);
            dk_u32 result = (dst - imm) & 0xFFFFFFFFu;
            dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, size);
            state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
            return (size == 2) ? 6 : 4;
        }
        if (ea_mode == 0x2u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x3u) ea_addr = state->a_regs[ea_reg] & 0x00FFFFFFu;
        else if (ea_mode == 0x4u) { ea_addr = (state->a_regs[ea_reg] - (size==0?1:size==1?2:4)) & 0x00FFFFFFu; }
        else if (ea_mode == 0x5u) { int d = dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)); ea_addr = (state->a_regs[ea_reg] + (dk_u32)d) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x6u) { dk_u16 ext = dk_native_read_bus_word(state, pc + (dk_u32)pc_inc); ea_addr = dk_native_index_brief(state, state->a_regs[ea_reg] & 0x00FFFFFFu, ext); pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x0u) { ea_addr = (dk_u32)dk_native_sign_extend16(dk_native_read_bus_word(state, pc + (dk_u32)pc_inc)) & 0x00FFFFFFu; pc_inc += 2; }
        else if (ea_mode == 0x7u && ea_reg == 0x1u) { ea_addr = dk_native_read_bus_long(state, pc + (dk_u32)pc_inc) & 0x00FFFFFFu; pc_inc += 4; }
        if (size == 0) { dk_u8 dst = dk_native_read_bus_byte(state, ea_addr); dk_u8 result = (dk_u8)(dst - (dk_u8)imm); dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, 0); }
        else if (size == 1) { dk_u16 dst = dk_native_read_bus_word(state, ea_addr); dk_u16 result = (dk_u16)(dst - (dk_u16)imm); dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, 1); }
        else { dk_u32 dst = dk_native_read_bus_long(state, ea_addr); dk_u32 result = dst - imm; dk_native_set_sub_flags(state, (int)imm, (int)dst, (int)result, 2); }
        state->current_pc = (pc + (dk_u32)pc_inc) & 0xFFFFFFFFu;
        return (ea_mode >= 5) ? 12 : 8;
    }

    return 0;
}

int dk_native_try_execute_slice(dk_native_system_t *state, dk_u32 max_cycles)
{
    int total_cycles = 0;
    int consumed;
    if (state == 0 || max_cycles == 0u) {
        return 0;
    }
    while ((dk_u32)total_cycles < max_cycles) {
        dk_u32 instruction_pc = state->current_pc;
        dk_u16 opcode = dk_native_fetch_opcode_fast(state, instruction_pc);
        /* Sonic 1's fast player synthesizes VBlank instead of spinning in
         * WaitForVBla. Match the Python reference before TST.B changes CCR. */
        if ((state->current_pc == 0x000029A4u || state->current_pc == 0x000029A8u)
            && state->wram[0xF62Au] != 0u) {
            state->wram[0xF62Au] = 0u;
            state->current_pc = 0x000029AAu;
            total_cycles += 1;
            break;
        }
        /* Match the Python fast path's treatment of true DBF timing loops.
         * A displacement of -2 branches back to the DBF itself and performs
         * no useful work, unlike the game's short copy/fill loops. */
        if (
            (opcode & 0xFFF8u) == 0x51C8u
            && dk_native_read_bus_word(state, state->current_pc + 2u) == 0xFFFEu
        ) {
            int reg = opcode & 0x7u;
            state->d_regs[reg] = 0xFFFFFFFFu;
            state->current_pc = (state->current_pc + 4u) & 0xFFFFFFFFu;
            total_cycles += 14;
            continue;
        }
        consumed = dk_native_try_execute_current(state);
        if (consumed <= 0) {
            break;
        }
        consumed = dk_native_reference_cycles(
            state,
            instruction_pc,
            opcode,
            consumed
        );
        total_cycles += consumed;
        if (state->stopped || state->last_illegal_pc != 0u) {
            break;
        }
    }
    return total_cycles;
}

int dk_native_run_z80_slice(dk_native_system_t *state, dk_u32 max_cycles)
{
    if (state == 0 || max_cycles == 0u) {
        return 0;
    }
    return dk_z80_execute(&state->z80, (int)max_cycles);
}

int dk_native_execute_frame(
    dk_native_system_t *state,
    dk_u32 main_cycles,
    dk_u32 z80_cycles,
    dk_u32 max_vblank_instructions,
    dk_u32 flags,
    dk_native_frame_result_t *out_result
)
{
    dk_native_frame_result_t result;
    dk_native_interrupt_result_t interrupt_result;
    int consumed;

    memset(&result, 0, sizeof(result));
    if (state == 0 || (main_cycles == 0u
                       && (flags & DK_NATIVE_FRAME_SKIP_MAIN) == 0u)) {
        if (out_result != 0) {
            *out_result = result;
        }
        return (int)DK_NATIVE_FRAME_INVALID;
    }

    if ((flags & DK_NATIVE_FRAME_ADVANCE_CLOCK) != 0u) {
        state->frame_count += 1u;
        state->master_clock += DK_NATIVE_MASTER_CYCLES_PER_FRAME;
    }

    if ((flags & DK_NATIVE_FRAME_SKIP_MAIN) == 0u) {
        consumed = dk_native_try_execute_slice(state, main_cycles);
        result.main_cycles = (dk_u32)(consumed > 0 ? consumed : 0);
        result.stop_pc = state->current_pc;
        result.stop_opcode = dk_native_read_bus_word(state, state->current_pc);
        if (
            consumed < (int)main_cycles
            && !state->stopped
            && state->last_illegal_pc == 0u
        ) {
            result.status = DK_NATIVE_FRAME_MAIN_UNSUPPORTED;
            if (out_result != 0) {
                *out_result = result;
            }
            return (int)result.status;
        }
        if (state->stopped || state->last_illegal_pc != 0u) {
            result.status = DK_NATIVE_FRAME_MAIN_STOPPED;
            if (out_result != 0) {
                *out_result = result;
            }
            return (int)result.status;
        }
    }

    if ((flags & DK_NATIVE_FRAME_RUN_VBLANK) != 0u) {
        if ((flags & DK_NATIVE_FRAME_FORCE_VBLANK) != 0u) {
            dk_vdp_vblank(&state->vdp);
        }
        if (max_vblank_instructions == 0u) {
            result.status = DK_NATIVE_FRAME_VBLANK_FAILED;
            if (out_result != 0) {
                *out_result = result;
            }
            return (int)result.status;
        }
        result.vblank_status = (dk_u32)dk_native_run_interrupt(
            state,
            6u,
            max_vblank_instructions,
            &interrupt_result
        );
        result.vblank_instructions = interrupt_result.instructions;
        result.vblank_cycles = interrupt_result.cycles;
        result.stop_pc = interrupt_result.stop_pc;
        result.stop_opcode = interrupt_result.stop_opcode;
        if (result.vblank_status != DK_NATIVE_INTERRUPT_COMPLETED) {
            result.status = DK_NATIVE_FRAME_VBLANK_FAILED;
            if (out_result != 0) {
                *out_result = result;
            }
            return (int)result.status;
        }
    }

    if ((flags & DK_NATIVE_FRAME_RUN_Z80) != 0u) {
        result.z80_cycles = (dk_u32)dk_native_run_z80_slice(state, z80_cycles);
        if (z80_cycles != 0u && result.z80_cycles == 0u
            && !state->z80.reset_flag && !state->z80.busreq) {
            result.status = DK_NATIVE_FRAME_Z80_STOPPED;
            if (out_result != 0) {
                *out_result = result;
            }
            return (int)result.status;
        }
    }

    dk_native_build_sonic_sat(state);

    result.status = DK_NATIVE_FRAME_COMPLETED;
    result.frame_count = state->frame_count;
    if (out_result != 0) {
        *out_result = result;
    }
    return (int)result.status;
}

int dk_native_run_interrupt(
    dk_native_system_t *state,
    dk_u8 level,
    dk_u32 max_instructions,
    dk_native_interrupt_result_t *out_result
)
{
    dk_native_interrupt_result_t result;
    dk_u32 instruction_pc;
    dk_u16 opcode;
    int consumed;

    memset(&result, 0, sizeof(result));
    if (state == 0 || level == 0u || level > 7u || max_instructions == 0u) {
        if (out_result != 0) {
            *out_result = result;
        }
        return (int)DK_NATIVE_INTERRUPT_INVALID;
    }

    result.return_pc = state->current_pc;
    result.return_sp = state->a_regs[7];
    state->pending_irq = level;
    if (!dk_native_should_service_interrupt(state)) {
        result.status = DK_NATIVE_INTERRUPT_MASKED;
        result.stop_pc = state->current_pc;
        result.stop_opcode = dk_native_read_bus_word(state, state->current_pc);
        if (out_result != 0) {
            *out_result = result;
        }
        return (int)result.status;
    }

    consumed = dk_native_try_execute_current(state);
    if (consumed <= 0) {
        result.status = DK_NATIVE_INTERRUPT_STOPPED;
        result.stop_pc = state->current_pc;
        result.stop_opcode = dk_native_read_bus_word(state, state->current_pc);
        if (out_result != 0) {
            *out_result = result;
        }
        return (int)result.status;
    }
    result.cycles = (dk_u32)consumed;

    while (result.instructions < max_instructions) {
        instruction_pc = state->current_pc;
        opcode = dk_native_read_bus_word(state, instruction_pc);
        consumed = dk_native_try_execute_current(state);
        if (consumed <= 0) {
            result.status = state->stopped
                ? DK_NATIVE_INTERRUPT_STOPPED
                : DK_NATIVE_INTERRUPT_UNSUPPORTED;
            result.stop_pc = instruction_pc;
            result.stop_opcode = opcode;
            break;
        }
        consumed = dk_native_reference_cycles(
            state,
            instruction_pc,
            opcode,
            consumed
        );
        result.instructions += 1u;
        result.cycles += (dk_u32)consumed;
        if (
            state->current_pc == result.return_pc
            && state->a_regs[7] == result.return_sp
        ) {
            result.status = DK_NATIVE_INTERRUPT_COMPLETED;
            result.stop_pc = state->current_pc;
            result.stop_opcode = dk_native_read_bus_word(state, state->current_pc);
            break;
        }
        if (state->stopped || state->last_illegal_pc != 0u) {
            result.status = DK_NATIVE_INTERRUPT_STOPPED;
            result.stop_pc = state->current_pc;
            result.stop_opcode = dk_native_read_bus_word(state, state->current_pc);
            break;
        }
    }

    if (result.status == DK_NATIVE_INTERRUPT_INVALID) {
        result.status = DK_NATIVE_INTERRUPT_LIMIT;
        result.stop_pc = state->current_pc;
        result.stop_opcode = dk_native_read_bus_word(state, state->current_pc);
    }
    if (out_result != 0) {
        *out_result = result;
    }
    return (int)result.status;
}

const char *dk_native_error_text(const dk_native_system_t *state)
{
    if (state == 0) {
        return "native state unavailable";
    }
    return state->last_error_text;
}
