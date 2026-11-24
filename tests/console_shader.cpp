#include "runtime/modules/console_shader.hpp"
#include "runtime/modules/console_shader_rdna.hpp"
#include <catch2/catch.hpp>
#include <cstring>
#include <d3dcompiler.h>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
using namespace xv::runtime::modules;
constexpr std::uint32_t v1 = 0x101, v2 = 0x102, v3 = 0x103, end_program = 0xbf810000;
// the translation of a compute shader of these words, which the host's compiler must take
std::string translated(std::initializer_list<std::uint32_t> words) {
    std::vector<std::uint32_t> code(words);
    code.push_back(end_program);
    std::string source;
    const auto why = console_shader_probe(code, true, &source);
    INFO(source);
    REQUIRE(why.empty());
    return source;
}
std::uint32_t shared(std::uint32_t operation) { return 0x36u << 26 | operation << 18; }
constexpr std::uint32_t shared_registers = 1 | 2 << 8 | 3 << 16 | 4 << 24;
std::size_t count(const std::string &text, const std::string &what) {
    std::size_t found = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
        ++found;
    return found;
}

std::optional<std::string> pixel_move(std::uint32_t move, std::uint32_t selectors, std::string &why,
                                      bool &needs_sm6) {
    // LLVM gfx1013: v_mov_b32 v20, 1.0; v_mov_b32_dpp v24, v20; exp mrt0, v24, off, off, off.
    const std::uint32_t code[] = {0x7e2802f2, move, selectors, 0xf8001801, 24, end_program};
    constexpr std::size_t region = 0x140, header = 0x28;
    std::vector<std::uint8_t> file(region + header + sizeof code);
    const auto put = [&](std::size_t offset, std::uint32_t value) {
        std::memcpy(file.data() + offset, &value, sizeof value);
    };
    std::memcpy(file.data(), "FSSM", 4);
    put(0xc0, region);
    put(0xc8, header + sizeof code);
    std::memcpy(file.data() + region + header, code, sizeof code);
    const std::uint32_t root[] = {1, 0, 24, 0, 0, 0};
    const rdna::Shader shader{file,
                              {reinterpret_cast<const std::uint8_t *>(root), sizeof root},
                              {},
                              {{"SV_Target", 0, 3, 4, true, 0, 15, 0}}};
    return rdna::translate(ShaderStage::pixel, shader, &why, nullptr, &needs_sm6);
}
} // namespace

TEST_CASE("a group's threads wait for each other between writing and reading its memory") {
    // ds_write_b32, ds_read_b32, ds_read_b32, ds_add_rtn_u32
    const auto source =
        translated({shared(0xd), shared_registers, shared(0x36), shared_registers, shared(0x36),
                    shared_registers, shared(0x20), shared_registers});
    CHECK(source.find("groupshared uint lds[8192];") != std::string::npos);
    CHECK(count(source, "GroupMemoryBarrierWithGroupSync();") == 2);
    CHECK(source.find("InterlockedAdd(lds[") != std::string::npos);
}

TEST_CASE("the shader's own barrier is the host's") {
    // ds_write_b32, s_barrier, ds_read_b32: one wait, not two
    const auto source =
        translated({shared(0xd), shared_registers, 0xbf8a0000, shared(0x36), shared_registers});
    CHECK(count(source, "GroupMemoryBarrierWithGroupSync();") == 1);
}

TEST_CASE("instructions on parts of memory and registers compile") {
    // ds_write_b8, ds_cmpst_rtn_b32, ds_read2_b32
    translated({shared(0x1e), shared_registers, shared(0x30), shared_registers, shared(0x37),
                shared_registers});
    // s_flbit_i32_i64 s1, s[2:3]; s_quadmask_b64 s[4:5], s[2:3]
    translated({0x17du << 23 | 1 << 16 | 0x18 << 8 | 2, 0x17du << 23 | 4 << 16 | 0x2d << 8 | 2});
    // v_pk_add_i16 v1, v1, v2; v_sad_u8 v1, v1, v2, v3; v_alignbit_b32 v1, v1, v2, v3
    translated({0x33u << 26 | 2 << 16 | 1, v1 | v2 << 9, 0x35u << 26 | 0x15a << 16 | 1,
                v1 | v2 << 9 | v3 << 18, 0x35u << 26 | 0x14e << 16 | 1, v1 | v2 << 9 | v3 << 18});
}

TEST_CASE("operations on a group's memory the host has no function for retry until they hold") {
    // ds_dec_u32, ds_min_f32, ds_mskor_rtn_b32, ds_wrap_rtn_b32
    const auto source =
        translated({shared(0x4), shared_registers, shared(0x12), shared_registers, shared(0x2c),
                    shared_registers, shared(0x34), shared_registers});
    CHECK(count(source, "do { X = lds[") == 4);
    CHECK(count(source, "InterlockedCompareExchange(lds[") == 4);
    // ds_wrxchg2_rtn_b32, ds_read_b96, ds_write_b8_d16_hi, ds_read_u16_d16_hi
    const auto parts = translated({shared(0x2e), shared_registers, shared(0xfe), shared_registers,
                                   shared(0xa0), shared_registers, shared(0xa7), shared_registers});
    CHECK(count(parts, "InterlockedExchange(lds[") == 2);
    CHECK(parts.find("(v2 >> 16) & 0xffu") != std::string::npos);
    CHECK(parts.find("v4 = (v4 & 0xffffu) | (") != std::string::npos);
}

TEST_CASE("64-bit values are doubles of the host's and pairs of words") {
    // v_cmp_lt_f64 vcc, 1.0, v[2:3]; v_cvt_i32_f64 v1, v[2:3]; v_cmp_class_f64 vcc, v[1:2], v2
    const auto compared =
        translated({0x3eu << 25 | 0x21 << 17 | 2 << 9 | 242, 0x3fu << 25 | 1 << 17 | 0x3 << 9 | v2,
                    0x3eu << 25 | 0xa8 << 17 | 2 << 9 | v1});
    CHECK(compared.find("asdouble(0x00000000u, 0x3ff00000u) < asdouble(v2, v3)") !=
          std::string::npos);
    CHECK(compared.find("double_class(v1, v2, v2)") != std::string::npos);
    // v_fma_f64 v[4:5], |v[1:2]|, v[2:3], literal; v_cmp_ge_u64 s106, v[1:2], -1
    const auto computed =
        translated({0x35u << 26 | 0x14c << 16 | 1 << 8 | 4, v1 | v2 << 9 | 255 << 18, 0x40080000,
                    0x35u << 26 | 0xe6 << 16 | 106, v1 | 193 << 9});
    CHECK(computed.find("fma(asdouble(v1, (v2 & 0x7fffffffu)), asdouble(v2, v3), asdouble(0u, "
                        "0x40080000u))") != std::string::npos);
    CHECK(computed.find("!(v2 < 0xffffffffu || (v2 == 0xffffffffu && v1 < 0xffffffffu))") !=
          std::string::npos);
}

TEST_CASE("a wave is this one lane where its lanes are counted") {
    // s_mov_b64 s[8:9], exec; s_bcnt1_i32_b64 s4, s[8:9]; v_mbcnt_lo_u32_b32 v1, -1, v2
    const auto source = translated({0xbe88047e, 0xbe841008, 0xd7650001, 0x000204c1});
    CHECK(source.find("s4 = (s8 != 0u ? 1u : 0u);") != std::string::npos);
    CHECK(source.find("v1 = v2;") != std::string::npos);
    // s_mov_b32 s8, 7; s_bcnt1_i32_b64 s4, s[8:9]: a number's bits are counted
    const auto number = translated({0xbe880387, 0xbe841008});
    CHECK(number.find("countbits(s8)") != std::string::npos);
}

TEST_CASE("a sum over a wave's lanes is this lane's own value") {
    // v_add_nc_u32_dpp8 v2, v2, v2 from the lane beside; v_add_nc_u32_dpp v3, v3, v3 from the
    // lane before, with zero for none, and from two before, with nothing for none;
    // v_permlanex16_b32 v4, v3, -1, -1; v_readlane_b32 s3, v3, 31; s_mov_b32 exec_lo, 0xffff0000
    const auto source =
        translated({0x4a0404e9, 0xde54c102, 0x4a0606fa, 0xff091103, 0x4a0606fa, 0xff011203,
                    0xd7784804, 0x03058303, 0xd7600003, 0x00013f03, 0xbefe03ff, 0xffff0000});
    CHECK(source.find("v2 = 0u + v2;") != std::string::npos);
    CHECK(count(source, "v3 = 0u + v3;") == 1);
    CHECK(source.find("v4 = 0u;") != std::string::npos);
    CHECK(source.find("s3 = 0u;") != std::string::npos);
    CHECK(source.find("s126 = 0u;") != std::string::npos);
}

TEST_CASE("a register picked by M0 and bytes picked by a selector") {
    // v_movreld_b32 v18, v55; v_perm_b32 v1, v1, v2, v3
    const auto source =
        translated({0x7e248537, 0x35u << 26 | 0x344 << 16 | 1, v1 | v2 << 9 | v3 << 18});
    CHECK(source.find("switch (s124) {") != std::string::npos);
    CHECK(source.find("v19 = v55;") != std::string::npos);
    CHECK(source.find("v1 = perm_bytes(v1, v2, v3);") != std::string::npos);
}

TEST_CASE("a register that holds one of two descriptors is used as each") {
    // s_load_dwordx8 s[12:19], s[4:5], 0x20; s_cbranch_scc0 over the next;
    // s_load_dwordx8 s[12:19], s[4:5], 0x40; image_load v3, v[0:1], s[12:19]
    const auto source = translated(
        {0xf40c0302, 0xfa000020, 0xbf840002, 0xf40c0302, 0xfa000040, 0xf0001108, 0x00030300});
    CHECK(count(source, "if (c5 == ") == 2);
    CHECK(source.find("c5 = 5u;") != std::string::npos);
    CHECK(source.find("c5 = 8u;") != std::string::npos);
    CHECK(source.find("t1_0.Load(") != std::string::npos);
    CHECK(source.find("t2_0.Load(") != std::string::npos);
}

TEST_CASE("a rectangle list's fourth corner comes from a geometry shader") {
    const char vertex[] = "struct Out { float4 position : SV_Position; float2 uv : TEXCOORD0; "
                          "nointerpolation uint layer : LAYER0; };\n"
                          "Out main(uint id : SV_VertexID) { Out o; o.position = float4(id & 1, "
                          "id >> 1, 0, 1); o.uv = o.position.xy; o.layer = id; return o; }\n";
    ID3DBlob *code = nullptr;
    REQUIRE(D3DCompile(vertex, sizeof vertex - 1, nullptr, nullptr, nullptr, "main", "vs_5_1", 0, 0,
                       &code, nullptr) == S_OK);
    const auto made = rectangle_shader(
        {static_cast<const std::uint8_t *>(code->GetBufferPointer()), code->GetBufferSize()});
    REQUIRE(made.size() > 4);
    CHECK(!std::memcmp(made.data(), "DXBC", 4));
    // its inputs are the vertex shader's outputs
    ID3D12ShaderReflection *reflection = nullptr;
    REQUIRE(D3DReflect(made.data(), made.size(), IID_PPV_ARGS(&reflection)) == S_OK);
    D3D12_SHADER_DESC description{};
    REQUIRE(reflection->GetDesc(&description) == S_OK);
    CHECK(description.InputParameters == 3);
    CHECK(description.OutputParameters == 3);
    CHECK(description.GSMaxOutputVertexCount == 4);
    reflection->Release();
    code->Release();
    const std::uint8_t nothing[16] = {};
    CHECK(rectangle_shader(nothing).empty());
}

TEST_CASE("a pixel DPP8 move can rearrange the four lanes of each quad") {
    bool needs_sm6 = false;
    std::string why;
    // LLVM gfx1013: dpp8:[1,2,3,0,5,6,7,4].
    const auto source = pixel_move(0x7e3002e9, 0x9f50d114, why, needs_sm6);
    INFO(why);
    REQUIRE(source);
    CHECK(needs_sm6);
    CHECK(count(*source, "QuadReadLaneAt(") == 4);
    CHECK(source->find("WaveGetLaneIndex()") != std::string::npos);
    CHECK(source->find("IsHelperLane() ? 0u : v20") != std::string::npos);
    CHECK(source->find("src, 1u)") != std::string::npos);
    CHECK(source->find("src, 0u)") != std::string::npos);

    CHECK_FALSE(pixel_move(0x7e3002e9, 0x9f50d514, why, needs_sm6)); // lane 0 reads lane 5
    CHECK_FALSE(needs_sm6);
    CHECK_FALSE(pixel_move(0x7e3002ea, 0x9f50d114, why, needs_sm6)); // fetch inactive lanes
    CHECK_FALSE(needs_sm6);
}

TEST_CASE("native instruction extraction recognizes an XBCH envelope with DXIL-relative offsets") {
    // Synthetic container; no title data. One FSSM instruction ends the program.
    constexpr std::size_t region = 0x140;
    std::vector<std::uint8_t> fssm(region + 0x2c);
    const auto put = [](std::vector<std::uint8_t> &v, std::size_t at, std::uint32_t value) {
        std::memcpy(v.data() + at, &value, sizeof value);
    };
    std::memcpy(fssm.data(), "FSSM", 4);
    put(fssm, 0xc0, region);
    put(fssm, 0xc8, 0x2c);
    put(fssm, region + 0x28, 0xbf810000);

    // RFC 1950/1951, single final uncompressed block.
    const auto len = static_cast<std::uint16_t>(fssm.size());
    const auto nlen = static_cast<std::uint16_t>(~len);
    std::vector<std::uint8_t> zlib{0x78, 0x01, 0x01, static_cast<std::uint8_t>(len),
                                  static_cast<std::uint8_t>(len >> 8),
                                  static_cast<std::uint8_t>(nlen),
                                  static_cast<std::uint8_t>(nlen >> 8)};
    zlib.insert(zlib.end(), fssm.begin(), fssm.end());
    std::uint32_t a = 1, b = 0;
    for (auto byte : fssm) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    const auto adler = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8)
        zlib.push_back(static_cast<std::uint8_t>(adler >> shift));

    constexpr std::size_t part_offset = 36, header = part_offset + 8;
    constexpr std::size_t table = 36, section = 48;
    std::vector<std::uint8_t> container(header + section + 24 + zlib.size());
    std::memcpy(container.data(), "DXBC", 4);
    put(container, 24, static_cast<std::uint32_t>(container.size()));
    put(container, 28, 1);
    put(container, 32, part_offset);
    std::memcpy(container.data() + part_offset, "DXIL", 4);
    put(container, part_offset + 4, static_cast<std::uint32_t>(container.size() - header));
    std::memcpy(container.data() + header + 24, "XDAT", 4);
    put(container, header + 28, 1);
    put(container, header + 32, table);
    put(container, header + table, 0x0d);
    put(container, header + table + 4, section);
    put(container, header + table + 8, static_cast<std::uint32_t>(24 + zlib.size()));
    std::memcpy(container.data() + header + section, "XBCH", 4);
    put(container, header + section + 4, 3);
    put(container, header + section + 8, static_cast<std::uint32_t>(zlib.size()));
    put(container, header + section + 16, static_cast<std::uint32_t>(fssm.size()));
    std::memcpy(container.data() + header + section + 24, zlib.data(), zlib.size());
    const auto code = xv::runtime::modules::console_shader_code(container);
    REQUIRE(code.size() == 1);
    CHECK(code.front() == 0xbf810000);

    // A declared compressed size outside its envelope must not be accepted.
    put(container, header + section + 8, 0xffffffffu);
    CHECK(xv::runtime::modules::console_shader_code(container).empty());
}
