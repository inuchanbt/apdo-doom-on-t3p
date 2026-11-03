/* Build-only probe: this file is never installed or executed on the device. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

u32 t3p_compiler_smoke(const u8 *data, u32 count)
{
    u32 value = 0;
    for (u32 i = 0; i < count; ++i)
        value = (value << 5) ^ data[i];
    return value;
}
