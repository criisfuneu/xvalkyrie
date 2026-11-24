#include "runtime/modules/console_shader.hpp"
#include "host/files.hpp"
#include "runtime/common/runtime.hpp"
#include "runtime/modules/console_shader_rdna.hpp"
#include "runtime/modules/inflate.hpp"
#include "runtime/modules/system.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <xmmintrin.h>

#include <objbase.h>

#include <dxcapi.h>
#include <wrl/client.h>

#include <dxcapi.h>

namespace xv::runtime::modules {
namespace {
// Bumped whenever what is made for a shader changes: the cache keeps one file per version.
constexpr int translator_version = 18;

using Bytes = std::span<const std::uint8_t>;
std::uint32_t word(Bytes bytes, std::size_t at) {
    std::uint32_t value = 0;
    if (at + 4 <= bytes.size())
        std::memcpy(&value, bytes.data() + at, 4);
    return value;
}
// a part of a DXBC container, by its four letters; empty if it has none
Bytes part(Bytes container, const char *name) {
    if (container.size() < 32 || std::memcmp(container.data(), "DXBC", 4))
        return {};
    const auto count = word(container, 28);
    for (std::uint32_t i = 0; i < count && 36 + 4 * i <= container.size(); ++i) {
        const auto offset = word(container, 32 + 4 * i);
        if (offset + 8 > container.size() || std::memcmp(container.data() + offset, name, 4))
            continue;
        const auto size = word(container, offset + 4);
        if (size > container.size() - offset - 8)
            return {};
        return container.subspan(offset + 8, size);
    }
    return {};
}

// Inflate (RFC 1951) of a zlib stream (RFC 1950), which is how the code is stored
class Inflate {
  public:
    explicit Inflate(Bytes input) : in_(input) {}
    bool run(std::vector<std::uint8_t> &out) {
        if (in_.size() < 2 || (in_[0] & 0x0f) != 8)
            return false;
        at_ = 2;
        for (bool last = false; !last;) {
            last = bits(1);
            const auto type = bits(2);
            if (type == 0) {
                count_ = 0;
                if (at_ + 4 > in_.size())
                    return false;
                const std::size_t length = in_[at_] | in_[at_ + 1] << 8;
                at_ += 4;
                if (at_ + length > in_.size())
                    return false;
                out.insert(out.end(), in_.begin() + at_, in_.begin() + at_ + length);
                at_ += length;
            } else if (type == 1) {
                fixed();
                if (!codes(out))
                    return false;
            } else if (type == 2) {
                if (!dynamic() || !codes(out))
                    return false;
            } else {
                return false;
            }
            if (failed_ || out.size() > 64u << 20)
                return false;
        }
        return true;
    }

  private:
    struct Table {
        std::array<std::uint16_t, 16> count{};
        std::array<std::uint16_t, 288> symbol{};
    };
    std::uint32_t bits(int need) {
        while (count_ < need) {
            if (at_ >= in_.size()) {
                failed_ = true;
                return 0;
            }
            hold_ |= std::uint32_t{in_[at_++]} << count_;
            count_ += 8;
        }
        const auto value = hold_ & ((1u << need) - 1);
        hold_ >>= need;
        count_ -= need;
        return value;
    }
    static void build(Table &table, const std::uint8_t *lengths, int n) {
        table.count.fill(0);
        for (int i = 0; i < n; ++i)
            ++table.count[lengths[i]];
        table.count[0] = 0;
        std::array<std::uint16_t, 16> offsets{};
        for (int length = 1; length < 16; ++length)
            offsets[length] = offsets[length - 1] + table.count[length - 1];
        for (int i = 0; i < n; ++i)
            if (lengths[i])
                table.symbol[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
    }
    int decode(const Table &table) {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length < 16; ++length) {
            code |= static_cast<int>(bits(1));
            const int count = table.count[length];
            if (code - count < first)
                return table.symbol[index + (code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        failed_ = true;
        return -1;
    }
    void fixed() {
        std::uint8_t lengths[288];
        for (int i = 0; i < 288; ++i)
            lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        build(literals_, lengths, 288);
        std::fill(lengths, lengths + 30, std::uint8_t{5});
        build(distances_, lengths, 30);
    }
    bool dynamic() {
        const int literal_count = static_cast<int>(bits(5)) + 257;
        const int distance_count = static_cast<int>(bits(5)) + 1;
        const int length_count = static_cast<int>(bits(4)) + 4;
        static constexpr std::uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                   11, 4,  12, 3, 13, 2, 14, 1, 15};
        std::uint8_t lengths[320] = {};
        for (int i = 0; i < length_count; ++i)
            lengths[order[i]] = static_cast<std::uint8_t>(bits(3));
        Table table;
        build(table, lengths, 19);
        std::fill(lengths, lengths + 320, std::uint8_t{0});
        for (int i = 0; i < literal_count + distance_count;) {
            const int symbol = decode(table);
            if (symbol < 0)
                return false;
            if (symbol < 16) {
                lengths[i++] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            std::uint8_t value = 0;
            int repeat;
            if (symbol == 16) {
                if (!i)
                    return false;
                value = lengths[i - 1];
                repeat = 3 + static_cast<int>(bits(2));
            } else {
                repeat =
                    symbol == 17 ? 3 + static_cast<int>(bits(3)) : 11 + static_cast<int>(bits(7));
            }
            if (i + repeat > literal_count + distance_count)
                return false;
            while (repeat--)
                lengths[i++] = value;
        }
        build(literals_, lengths, literal_count);
        build(distances_, lengths + literal_count, distance_count);
        return !failed_;
    }
    bool codes(std::vector<std::uint8_t> &out) {
        static constexpr std::uint16_t length_base[29] = {
            3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static constexpr std::uint8_t length_extra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static constexpr std::uint16_t distance_base[30] = {
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static constexpr std::uint8_t distance_extra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                            4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                            9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            const int symbol = decode(literals_);
            if (symbol < 0 || failed_)
                return false;
            if (symbol < 256) {
                out.push_back(static_cast<std::uint8_t>(symbol));
            } else if (symbol == 256) {
                return true;
            } else {
                if (symbol > 285)
                    return false;
                const std::size_t length =
                    length_base[symbol - 257] + bits(length_extra[symbol - 257]);
                const int code = decode(distances_);
                if (code < 0 || code > 29)
                    return false;
                const std::size_t distance = distance_base[code] + bits(distance_extra[code]);
                if (distance > out.size())
                    return false;
                for (std::size_t i = 0; i < length; ++i)
                    out.push_back(out[out.size() - distance]);
            }
        }
    }
    Bytes in_;
    std::size_t at_ = 0;
    std::uint32_t hold_ = 0;
    int count_ = 0;
    bool failed_ = false;
    Table literals_, distances_;
};

// The XDAT block of the DXIL part: after the program header, a table of sections as (type,
// offset, size). Section 0x0d is the zlib stream with the code. An Xbox One shader has it in
// section 0, after 8 bytes: what is in it starts with its size and has 5 at 0x18.
std::vector<std::uint8_t> code_container(Bytes container) {
    const auto dxil = part(container, "DXIL");
    constexpr std::size_t xdat = 24;
    if (dxil.size() < xdat + 16 || std::memcmp(dxil.data() + xdat, "XDAT", 4))
        return {};
    const auto block = dxil.subspan(xdat);
    // Accept the versioned XBCH envelope as well as the original bare zlib section.
    // Some XDAT tables use offsets from the DXIL program header rather than XDAT.
    const auto unpack_fssm = [](Bytes section) {
        std::vector<std::uint8_t> out;
        if (section.size() >= 24 && !std::memcmp(section.data(), "XBCH", 4)) {
            if (word(section, 4) != 3)
                return out;
            const auto packed_size = word(section, 8), unpacked_size = word(section, 16);
            if (!packed_size || packed_size > section.size() - 24 ||
                unpacked_size > (64u << 20))
                return out;
            if (!Inflate(section.subspan(24, packed_size)).run(out) ||
                out.size() != unpacked_size)
                return std::vector<std::uint8_t>{};
        } else if (!Inflate(section).run(out)) {
            return std::vector<std::uint8_t>{};
        }
        if (out.size() < 16 || std::memcmp(out.data(), "FSSM", 4))
            return std::vector<std::uint8_t>{};
        return out;
    };
    // Header-relative table location, when explicitly specified by the container.
    const std::size_t count = word(block, 4), table = word(block, 8);
    if (count && count <= 256 && table >= xdat + 12 && table <= dxil.size() &&
        count <= (dxil.size() - table) / 12) {
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t at = table + i * 12;
            const auto type = word(dxil, at), offset = word(dxil, at + 4),
                       size = word(dxil, at + 8);
            if (type != 0x0d || offset > dxil.size() || size > dxil.size() - offset)
                continue;
            if (auto out = unpack_fssm(dxil.subspan(offset, size)); !out.empty())
                return out;
        }
    }
    for (std::size_t entry = 12; entry + 12 <= std::min<std::size_t>(block.size(), 12 + 16 * 12);
         entry += 12) {
        const auto type = word(block, entry), offset = word(block, entry + 4),
                   size = word(block, entry + 8);
        if (type > 0xff)
            break;
        if (type == 0x0d && offset <= dxil.size() && size <= dxil.size() - offset) {
            if (auto out = unpack_fssm(dxil.subspan(offset, size)); !out.empty())
                return out;
        }
        if (offset > block.size() || size > block.size() - offset)
            break;
        // that one's place is counted from the start of the part
        if (!type && size > 8 && offset <= dxil.size() && size <= dxil.size() - offset) {
            std::vector<std::uint8_t> out;
            if (Inflate(dxil.subspan(offset + 8, size - 8)).run(out) && out.size() >= 0x1e0 &&
                word(out, 0) == out.size() && word(out, 0x18) == 5)
                return out;
            return {};
        }
        if (!type)
            break;
        if (type != 0x0d)
            continue;
        if (auto out = unpack_fssm(block.subspan(offset, size)); !out.empty())
            return out;
    }
    return {};
}

// Section 3 of an Xbox One shader's XDAT block, placed from the start of the part like its
// code: 2, a count, and 56 bytes for each thing the shader binds.
Bytes bindings_section(Bytes container) {
    const auto dxil = part(container, "DXIL");
    constexpr std::size_t xdat = 24;
    if (dxil.size() < xdat + 16 || std::memcmp(dxil.data() + xdat, "XDAT", 4))
        return {};
    const auto count = std::min<std::size_t>(word(dxil, xdat + 4), 16);
    for (std::size_t entry = xdat + 12; entry + 12 <= dxil.size() && entry < xdat + 12 + 12 * count;
         entry += 12) {
        const auto offset = word(dxil, entry + 4), size = word(dxil, entry + 8);
        if (word(dxil, entry) == 3 && offset <= dxil.size() && size <= dxil.size() - offset)
            return dxil.subspan(offset, size);
    }
    return {};
}

using rdna::Element;
// An ISG1 or OSG1 signature, in the order of its registers; or an ISGN or OSGN one, which
// the host's older compiler writes: the same elements without their first and last words.
std::vector<Element> signature(Bytes container, const char *name) {
    const auto whole = part(container, name);
    const bool compact = name[3] == 'N';
    const std::size_t size = compact ? 24 : 32, lead = compact ? 0 : 4;
    struct Read {
        Element element;
        std::uint32_t reg;
        std::uint8_t mask;
    };
    std::vector<Read> read;
    const auto count = word(whole, 0);
    for (std::uint32_t i = 0; i < count && i < 64; ++i) {
        const std::size_t entry = 8 + size * std::size_t{i};
        if (entry + size > whole.size())
            return {};
        const auto data = whole.subspan(entry + lead);
        const auto text = word(data, 0);
        if (text >= whole.size())
            return {};
        const auto *const start = reinterpret_cast<const char *>(whole.data()) + text;
        Element element;
        element.name.assign(start, strnlen(start, whole.size() - text));
        element.index = word(data, 4);
        element.type = word(data, 12);
        element.width = static_cast<std::uint32_t>(std::max(std::popcount(data[20]), 1));
        element.system_value = word(data, 8) != 0 || !_strnicmp(element.name.c_str(), "SV_", 3);
        element.reg = word(data, 16);
        element.mask = data[20];
        element.read = data[21];
        read.push_back({std::move(element), word(data, 16), data[20]});
    }
    std::stable_sort(read.begin(), read.end(), [](const Read &a, const Read &b) {
        return a.reg != b.reg ? a.reg < b.reg : a.mask < b.mask;
    });
    std::vector<Element> elements;
    for (auto &one : read)
        elements.push_back(std::move(one.element));
    return elements;
}
// The fields of a signature as an HLSL struct: element n is the field fn. The values the
// pipeline makes up itself are declared after everything else, as the compiler wants.
std::string hlsl_struct(const char *name, const std::vector<Element> &elements, bool interpolated) {
    const auto generated = [](const Element &element) {
        for (const auto *made : {"SV_VertexID", "SV_InstanceID", "SV_PrimitiveID", "SV_IsFrontFace",
                                 "SV_SampleIndex", "SV_Coverage"})
            if (!_stricmp(element.name.c_str(), made))
                return true;
        return false;
    };
    std::string text = std::string("struct ") + name + " {\n";
    for (const bool last : {false, true})
        for (std::size_t field = 0; field < elements.size(); ++field) {
            const auto &element = elements[field];
            if (generated(element) != last)
                continue;
            const bool integer = element.type == 1 || element.type == 2;
            const bool named = !_strnicmp(element.name.c_str(), "SV_", 3);
            text += "    ";
            if (interpolated && integer && !named)
                text += "nointerpolation ";
            text += std::string(element.type == 1   ? "uint"
                                : element.type == 2 ? "int"
                                                    : "float") +
                    (element.width > 1 ? std::to_string(element.width) : "") + " f" +
                    std::to_string(field) + " : " + element.name;
            // SV_Position and the like take no index; the others always may
            if (!named || element.index || !_stricmp(element.name.c_str(), "SV_Target") ||
                !_stricmp(element.name.c_str(), "SV_ClipDistance") ||
                !_stricmp(element.name.c_str(), "SV_CullDistance"))
                text += std::to_string(element.index);
            text += ";\n";
        }
    return text + "};\n";
}

// A stage without inputs takes none: `main` has no parameter, and no struct is made. (A
// made-up input would have to match what the stage before writes.)
std::string without_input(std::string source) {
    const std::string parameter = "main(Input input)";
    if (const auto at = source.find(parameter); at != std::string::npos)
        source.replace(at, parameter.size(), "main()");
    return source;
}
// A shader with the original's signature that does nothing: a vertex shader that puts every
// vertex at the origin (so nothing is drawn), a pixel shader that writes zero, an empty
// compute shader. It stands in for a shader that is not translated, so that the title's
// pipelines exist and it goes on.
std::string stand_in(ShaderStage stage, Bytes container) {
    if (stage == ShaderStage::compute)
        return "[numthreads(1, 1, 1)] void main() {}\n";
    const auto inputs = signature(container, "ISG1"), outputs = signature(container, "OSG1");
    // a geometry shader that draws nothing
    if (stage >= ShaderStage::geometry_points) {
        if (inputs.empty() || outputs.empty())
            return {};
        const char *const primitive = stage == ShaderStage::geometry_points ? "point Input input[1]"
                                      : stage == ShaderStage::geometry_lines
                                          ? "line Input input[2]"
                                          : "triangle Input input[3]";
        return hlsl_struct("Input", inputs, false) + hlsl_struct("Output", outputs, false) +
               "[maxvertexcount(1)] void main(" + primitive +
               ", inout TriangleStream<Output> stream) {\n"
               // one vertex makes no triangle, and gives the shader its output signature
               "    Output output = (Output)0;\n    stream.Append(output);\n}\n";
    }
    std::string text =
        inputs.empty() ? "" : hlsl_struct("Input", inputs, stage == ShaderStage::pixel);
    if (outputs.empty())
        text += "void main(Input input) {}\n";
    else
        text +=
            hlsl_struct("Output", outputs, false) +
            "Output main(Input input) {\n    Output output = (Output)0;\n    return output;\n}\n";
    return inputs.empty() ? without_input(std::move(text)) : text;
}
// the translation of the code; nothing if the code is not one that is handled, and then
// `why` says what stopped it
std::optional<std::string> translated_source(ShaderStage stage, Bytes container,
                                             std::string *why = nullptr,
                                             std::uint32_t *at = nullptr, bool *needs_sm6 = nullptr,
                                             std::uint32_t signed_inputs = 0,
                                             Bytes root_signature = {}) {
    if (stage != ShaderStage::vertex && stage != ShaderStage::pixel &&
        stage != ShaderStage::compute)
        return std::nullopt;
    const auto file = code_container(container);
    rdna::Shader shader;
    shader.file = file;
    shader.root_signature = part(container, "RTS0");
    if (shader.root_signature.empty())
        shader.root_signature = root_signature;
    shader.bindings = bindings_section(container);
    shader.inputs = signature(container, "ISG1");
    shader.outputs = signature(container, "OSG1");
    shader.signed_inputs = signed_inputs;
    return rdna::translate(stage, shader, why, at, needs_sm6);
}

using Compile = HRESULT(WINAPI *)(const void *, SIZE_T, const char *, const void *, void *,
                                  const char *, const char *, UINT, UINT, IUnknown **, IUnknown **);
struct Blob : IUnknown {
    virtual void *STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
// The newer of the host's two compilers, which takes seconds over shaders the older one
// takes minutes over. It is not part of Windows: it is used when dxcompiler.dll and the
// dxil.dll that signs its shaders are next to xvalkyrie or in a Windows SDK. XV_SHADER_FXC
// keeps to the older one.
DxcCreateInstanceProc newer_compiler() {
    static const auto create = []() -> DxcCreateInstanceProc {
        if (std::getenv("XV_SHADER_FXC"))
            return nullptr;
        std::vector<std::filesystem::path> folders{host::executable_directory()};
        wchar_t programs[MAX_PATH];
        if (GetEnvironmentVariableW(L"ProgramFiles(x86)", programs, MAX_PATH)) {
            std::error_code error;
            std::vector<std::filesystem::path> kits;
            for (const auto &entry : std::filesystem::directory_iterator(
                     std::filesystem::path(programs) / "Windows Kits" / "10" / "bin", error))
                kits.push_back(entry.path() / "x64");
            std::sort(kits.rbegin(), kits.rend());
            folders.insert(folders.end(), kits.begin(), kits.end());
        }
        for (const auto &folder : folders) {
            std::error_code error;
            if (!std::filesystem::exists(folder / "dxcompiler.dll", error) ||
                !std::filesystem::exists(folder / "dxil.dll", error))
                continue;
            // the compiler asks for the signer by name: it has to be loaded already
            if (!LoadLibraryExW((folder / "dxil.dll").c_str(), nullptr,
                                LOAD_WITH_ALTERED_SEARCH_PATH))
                continue;
            const auto library = LoadLibraryExW((folder / "dxcompiler.dll").c_str(), nullptr,
                                                LOAD_WITH_ALTERED_SEARCH_PATH);
            if (!library)
                continue;
            if (const auto found = reinterpret_cast<DxcCreateInstanceProc>(
                    GetProcAddress(library, "DxcCreateInstance")))
                return found;
        }
        return nullptr;
    }();
    return create;
}
std::vector<std::uint8_t> compile_newer(ShaderStage stage, const std::string &source,
                                        std::string *message) {
    IDxcCompiler3 *compiler = nullptr;
    if (!newer_compiler() || FAILED(newer_compiler()(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))) ||
        !compiler)
        return {};
    // The language as the older compiler has it, which the shaders are written in. The
    // largest are not optimized here: that takes several times as long, and a title waits.
    const wchar_t *arguments[] = {L"-E",
                                  L"main",
                                  L"-T",
                                  stage == ShaderStage::vertex    ? L"vs_6_0"
                                  : stage == ShaderStage::pixel   ? L"ps_6_0"
                                  : stage == ShaderStage::compute ? L"cs_6_0"
                                                                  : L"gs_6_0",
                                  L"-HV",
                                  L"2018",
                                  source.size() > 200'000 ? L"-Od" : L"-O3"};
    const DxcBuffer text{source.data(), source.size(), DXC_CP_UTF8};
    IDxcResult *result = nullptr;
    std::vector<std::uint8_t> bytes;
    HRESULT status = E_FAIL;
    if (SUCCEEDED(compiler->Compile(&text, arguments, static_cast<UINT32>(std::size(arguments)),
                                    nullptr, IID_PPV_ARGS(&result))) &&
        result && SUCCEEDED(result->GetStatus(&status)) && SUCCEEDED(status)) {
        IDxcBlob *code = nullptr;
        if (SUCCEEDED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), nullptr)) && code) {
            const auto *const data = static_cast<const std::uint8_t *>(code->GetBufferPointer());
            bytes.assign(data, data + code->GetBufferSize());
            code->Release();
        }
    } else if (message) {
        IDxcBlobUtf8 *errors = nullptr;
        if (result &&
            SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) &&
            errors) {
            message->assign(errors->GetStringPointer(), errors->GetStringLength());
            errors->Release();
        }
        if (message->empty())
            *message = "no message";
    }
    if (result)
        result->Release();
    compiler->Release();
    return bytes;
}
// `older`: whether the older compiler is asked when the newer one is not there or turns the
// shader down. `newer`: whether the newer one is asked at all. The shaders of one pipeline
// are all made by the same compiler: the host turns down a pipeline of both kinds.
std::vector<std::uint8_t> compile_now(ShaderStage stage, const std::string &source, bool report,
                                      std::string *message = nullptr, bool older = true,
                                      bool newer = true) {
    if (source.empty())
        return {};
    if (newer && newer_compiler()) {
        const auto title_state = _mm_getcsr();
        _mm_setcsr(0x1f80);
        std::string said;
        auto bytes = compile_newer(stage, source, &said);
        _mm_setcsr(title_state);
        if (!bytes.empty())
            return bytes;
        if (!older) {
            if (message)
                *message = said;
            return {};
        }
        if (static std::atomic<int> shown{0}; report && shown++ < 4)
            std::fprintf(stderr,
                         "[Runtime] the newer compiler turned a host shader down, and the older "
                         "one is asked: %s\n",
                         said.c_str());
    }
    static const auto function = [] {
        const auto library =
            LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return library ? reinterpret_cast<Compile>(GetProcAddress(library, "D3DCompile")) : nullptr;
    }();
    if (!function)
        return {};
    const char *const target = stage == ShaderStage::vertex    ? "vs_5_1"
                               : stage == ShaderStage::pixel   ? "ps_5_1"
                               : stage == ShaderStage::compute ? "cs_5_1"
                                                               : "gs_5_1";
    constexpr UINT unbounded_tables = 1u << 20; // D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES
    IUnknown *code = nullptr, *errors = nullptr;
    // the compiler is run with the floating point state a host program has, not the
    // title's thread's
    const auto title_state = _mm_getcsr();
    _mm_setcsr(0x1f80);
    const auto result = function(source.data(), source.size(), "console shader", nullptr, nullptr,
                                 "main", target, unbounded_tables, 0, &code, &errors);
    _mm_setcsr(title_state);
    std::vector<std::uint8_t> bytes;
    if (SUCCEEDED(result) && code) {
        auto *const blob = static_cast<Blob *>(code);
        const auto *const data = static_cast<const std::uint8_t *>(blob->GetBufferPointer());
        bytes.assign(data, data + blob->GetBufferSize());
    } else if (message) {
        *message = errors
                       ? static_cast<const char *>(static_cast<Blob *>(errors)->GetBufferPointer())
                       : "no message";
    } else if (static std::atomic<int> shown{0}; report && shown++ < 4) {
        std::fprintf(
            stderr, "[Runtime] a host shader for a console one did not compile: %s\n%s\n",
            errors ? static_cast<const char *>(static_cast<Blob *>(errors)->GetBufferPointer())
                   : "no message",
            source.c_str());
        std::fflush(stderr);
    }
    if (code)
        code->Release();
    if (errors)
        errors->Release();
    return bytes;
}
std::vector<std::uint8_t> compile_sm6(ShaderStage stage, const std::string &source) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDxcCompiler3> compiler;
    if (FAILED(host_dxc_create_instance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
        return {};
    const wchar_t *const profile = stage == ShaderStage::vertex ? L"vs_6_0" : L"ps_6_6";
    const wchar_t *args[] = {L"-E", L"main", L"-T", profile};
    const DxcBuffer input{source.data(), source.size(), DXC_CP_UTF8};
    ComPtr<IDxcResult> result;
    if (FAILED(compiler->Compile(&input, args, static_cast<UINT32>(std::size(args)), nullptr,
                                 IID_PPV_ARGS(&result))))
        return {};
    HRESULT status = E_FAIL;
    ComPtr<IDxcBlob> code;
    if (FAILED(result->GetStatus(&status)) || FAILED(status) ||
        FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&code), nullptr)) || !code) {
        ComPtr<IDxcBlobUtf8> errors;
        if (static std::atomic<int> shown{0};
            shown++ < 4 &&
            SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors)
            std::fprintf(stderr, "[Runtime] a Shader Model 6 console shader did not compile: %s\n",
                         errors->GetStringPointer());
        return {};
    }
    const auto *bytes = static_cast<const std::uint8_t *>(code->GetBufferPointer());
    return {bytes, bytes + code->GetBufferSize()};
}

void write_file(const std::filesystem::path &path, Bytes bytes);
// The host's compiler takes minutes over some of the largest shaders, and the title waits
// for each of its shaders. So it is given `seconds`: a shader that takes longer is left to
// finish on its own and goes to `keep`, the shader cache, for the next run; this run gets
// nothing for it. XV_SHADER_SECONDS changes how long that is.
std::vector<std::uint8_t> compile(ShaderStage stage, const std::string &source,
                                  const std::filesystem::path &keep = {}, bool sm6 = false) {
    static const int seconds = [] {
        const auto *const text = std::getenv("XV_SHADER_SECONDS");
        return text ? std::max(std::atoi(text), 1) : 20;
    }();
    struct Job {
        std::mutex mutex;
        std::condition_variable finished;
        bool done = false, abandoned = false;
        std::vector<std::uint8_t> bytes;
    };
    const auto job = std::make_shared<Job>();
    std::thread([job, stage, source, keep, sm6] {
        auto bytes = sm6 ? compile_sm6(stage, source) : compile_now(stage, source, true);
        const std::lock_guard lock(job->mutex);
        if (job->abandoned && !bytes.empty() && !keep.empty())
            write_file(keep, bytes);
        job->bytes = std::move(bytes);
        job->done = true;
        job->finished.notify_all();
    }).detach();
    std::unique_lock lock(job->mutex);
    if (job->finished.wait_for(lock, std::chrono::seconds(seconds), [&] { return job->done; }))
        return std::move(job->bytes);
    job->abandoned = true;
    static std::atomic<int> shown{0};
    if (shown++ < 4)
        std::fprintf(stderr,
                     "[Runtime] the host's compiler has had %d s for a console shader of %zu "
                     "characters: it draws nothing in this run%s\n",
                     seconds, source.size(), keep.empty() ? "" : ", and is kept for the next");
    return {};
}

std::string hash_name(Bytes bytes) {
    std::uint64_t a = 0xcbf29ce484222325ull, b = 0x84222325cbf29ce4ull;
    for (const auto byte : bytes) {
        a = (a ^ byte) * 0x100000001b3ull;
        b = (b ^ byte) * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
    }
    char text[40];
    std::snprintf(text, sizeof text, "%016llx%016llx", static_cast<unsigned long long>(a),
                  static_cast<unsigned long long>(b));
    return text;
}
std::vector<std::uint8_t> read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write_file(const std::filesystem::path &path, Bytes bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    // written whole or not at all: another title process may be writing the same shader
    auto partial = path;
    partial += "." + std::to_string(GetCurrentProcessId());
    {
        std::ofstream file(partial, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    std::filesystem::rename(partial, path, error);
    if (error)
        std::filesystem::remove(partial, error);
}

std::mutex mutex;
std::condition_variable made_one;
std::map<std::string, std::vector<std::uint8_t>> made; // by cache name; never erased
std::set<std::string> making;                          // by cache name, on some thread
std::atomic<unsigned> translated{0}, stood_in{0};
} // namespace

bool inflate_zlib(Bytes stream, std::vector<std::uint8_t> &out) { return Inflate(stream).run(out); }

bool console_only_shader(Bytes container);
std::span<const std::uint8_t> rectangle_shader(Bytes vertex) {
    auto outputs = signature(vertex, "OSG1");
    if (outputs.empty())
        outputs = signature(vertex, "OSGN");
    if (outputs.empty())
        return {};
    // what varies across a rectangle does so evenly: the fourth corner is as far from the
    // second as the third is from the first. What does not vary is the first corner's.
    std::string source = hlsl_struct("Vertex", outputs, false) +
                         "[maxvertexcount(4)] void main(triangle Vertex input[3], inout "
                         "TriangleStream<Vertex> stream) {\n    Vertex fourth = input[0];\n";
    for (std::size_t field = 0; field < outputs.size(); ++field)
        if (outputs[field].type == 3) {
            const auto name = ".f" + std::to_string(field);
            source += "    fourth" + name + " = input[1]" + name + " + input[2]" + name +
                      " - input[0]" + name + ";\n";
        }
    source += "    stream.Append(input[0]);\n    stream.Append(input[1]);\n    "
              "stream.Append(input[2]);\n    stream.Append(fourth);\n}\n";
    static std::mutex rectangles_mutex;
    static std::map<std::string, std::vector<std::uint8_t>> rectangles; // by source; never erased
    const std::lock_guard lock(rectangles_mutex);
    // by the compiler that made the vertex shader it goes with
    const bool newer = !part(vertex, "DXIL").empty();
    const auto key = (newer ? "6" : "5") + source;
    const auto found = rectangles.find(key);
    if (found != rectangles.end())
        return found->second;
    return rectangles
        .emplace(key,
                 compile_now(ShaderStage::geometry_triangles, source, true, nullptr, true, newer))
        .first->second;
}

std::string shader_signature(Bytes container) {
    std::string text = console_only_shader(container) ? "(console code only) " : "";
    for (const auto *name : {"ISG1", "OSG1"}) {
        text += name == std::string("ISG1") ? "in:" : " out:";
        for (const auto &element : signature(container, name))
            text += " " + element.name + std::to_string(element.index) + "/" +
                    std::to_string(element.type) + "x" + std::to_string(element.width) + "@r" +
                    std::to_string(element.reg) + "m" + std::to_string(element.mask);
    }
    return text;
}

std::optional<std::string> console_shader_source(ShaderStage stage, Bytes container,
                                                 std::string *why, std::uint32_t *at,
                                                 Bytes root_signature) {
    return translated_source(stage, container, why, at, nullptr, 0, root_signature);
}

std::string console_shader_errors(ShaderStage stage, const std::string &source) {
    std::string message;
    return compile_now(stage, source, false, &message, false).empty() ? message : std::string();
}

std::string console_shader_probe(std::span<const std::uint32_t> words, bool compiled,
                                 std::string *text) {
    // Before the words, what gives them something to work on: a descriptor of the first
    // table in s[12:19] and a sampler of the second in s[24:27].
    static constexpr std::uint32_t before[] = {
        0xbe940305,             // s_mov_b32 s20, s5
        0xf40c0302, 0xfa000000, // s_load_dwordx8 s[12:19], s[4:5], 0
        0xf408060a, 0xfa000000, // s_load_dwordx4 s[24:27], s[20:21], 0
    };
    std::vector<std::uint32_t> code(std::begin(before), std::end(before));
    code.insert(code.end(), words.begin(), words.end());
    // the least of a code file: where the code is and how long, four words of thread group
    // sizes, a hash, the code, and the record that ends the records
    constexpr std::size_t region = 0x140, start = region + 0x28;
    std::vector<std::uint8_t> file(start + 4 * code.size() + 20, 0);
    const auto put = [&](std::size_t at, std::uint32_t value) {
        std::memcpy(file.data() + at, &value, 4);
    };
    std::memcpy(file.data(), "FSSM", 4);
    put(0xc0, static_cast<std::uint32_t>(region));
    put(0xc8, static_cast<std::uint32_t>(0x28 + 4 * code.size()));
    for (std::size_t n = 0; n < 3; ++n)
        put(region + 4 + 4 * n, 1);
    for (std::size_t n = 0; n < code.size(); ++n)
        put(start + 4 * n, code[n]);
    put(start + 4 * code.size() + 8, ~0u);
    // A root signature of two tables: resources without end, and samplers. Version 1: the
    // header, two parameters (type, visibility, offset), each table (ranges, offset) and
    // its ranges (type, count, register, space, offset in the table).
    static constexpr std::uint32_t signature[] = {
        1, 2,  24, 0,   48, 0,  // version, parameters and where, static samplers and where, flags
        0, 0,  48, 0,   0,  76, // the two tables
        1, 56, 0,  ~0u, 0,  0,  0, // resources: one range of shader resources from t0
        1, 84, 3,  16,  0,  0,  0, // samplers: sixteen from s0
    };
    rdna::Shader shader;
    shader.file = file;
    shader.root_signature = {reinterpret_cast<const std::uint8_t *>(signature), sizeof signature};
    std::string why;
    const auto source = rdna::translate(ShaderStage::compute, shader, &why);
    if (source && text)
        *text = *source;
    if (source && compiled && compile_now(ShaderStage::compute, *source, false, &why).empty())
        why = "the host's compiler: " + why;
    return why;
}

bool console_only_shader(Bytes container) {
    // The program header says where its bitcode is, from the "DXIL" at offset 8. A shader
    // with console code may have its bitcode as well, and then the host runs that.
    const auto dxil = part(container, "DXIL");
    if (dxil.size() < 28 || std::memcmp(dxil.data() + 24, "XDAT", 4))
        return false;
    return !shader_has_dxil(container);
}

bool shader_has_dxil(Bytes container) {
    const auto dxil = part(container, "DXIL");
    if (dxil.size() < 24)
        return false;
    const std::size_t bitcode = 8 + std::size_t{word(dxil, 16)};
    static constexpr std::uint8_t magic[] = {0x42, 0x43, 0xc0, 0xde};
    return bitcode + 4 <= dxil.size() && !std::memcmp(dxil.data() + bitcode, magic, 4);
}

std::vector<std::uint32_t> console_shader_code(Bytes container) {
    const auto file = code_container(container);
    const Bytes bytes(file);
    if (bytes.size() >= 0x140 && !std::memcmp(bytes.data(), "FSSM", 4)) {
        // Use the same region and instruction start as the FSSM translator.
        const std::size_t region = word(bytes, 0xc0), size = word(bytes, 0xc8);
        if (region <= bytes.size() && size <= bytes.size() - region && size >= 0x2c &&
            !(size % 4)) {
            std::vector<std::uint32_t> code;
            for (std::size_t at = region + 0x28; at + 4 <= region + size; at += 4)
                code.push_back(word(bytes, at));
            return code;
        }
        return {};
    }
    // The header has one slot per kind of shader, unused ones filled with a marker; the one
    // in use leads to a record that has where the code is and how long, padding included.
    std::uint32_t record = 0;
    for (std::size_t slot = 0x18; slot < 0x78 && !record; slot += 8)
        if (word(bytes, slot) != 0xdeadbeef)
            record = word(bytes, slot);
    auto start = word(bytes, record + 0x10);
    const auto size = word(bytes, record + 0x18);
    if (!record || start % 4 || start >= bytes.size() || size > bytes.size() - start)
        return {};
    std::vector<std::uint32_t> code;
    constexpr std::uint32_t end_program = 0xbf810000;
    for (auto at = start; at + 4 <= start + size; at += 4) {
        const auto value = word(bytes, at);
        if (code.empty() && !value) // zero words lead in
            continue;
        code.push_back(value);
        if (value == end_program)
            return code;
    }
    return {};
}

namespace {
std::span<const std::uint8_t> make_host_shader(ShaderStage stage, Bytes container,
                                               HostShaderModel model,
                                               std::span<const PackedInput> packed,
                                               Bytes root_signature) {
    // only a shader with no root signature of its own is made for the pipeline's
    if (!part(container, "RTS0").empty())
        root_signature = {};
    const char letter = "vpc123"[static_cast<int>(stage)];
    const auto hash = hash_name(container);
    // a vertex shader for a layout with packed signed inputs is another shader
    std::uint32_t signed_inputs = 0;
    if (stage == ShaderStage::vertex && !packed.empty()) {
        const auto inputs = signature(container, "ISG1");
        for (std::size_t i = 0; i < inputs.size() && i < 32; ++i)
            for (const auto &one : packed)
                if (one.index == inputs[i].index &&
                    !_stricmp(one.name.c_str(), inputs[i].name.c_str()))
                    signed_inputs |= 1u << i;
    }
    // XV_SHADER_STAND_INS: no translation, to tell what a translated shader breaks; with a
    // value, the letters of the stages that are still translated (v, p, c)
    static const char *const stand_ins = std::getenv("XV_SHADER_STAND_INS");
    const char stage_letter = "vpc123"[static_cast<int>(stage)];
    const bool stand_ins_only = stand_ins && !std::strchr(stand_ins, stage_letter);
    const char *const model_name = model == HostShaderModel::sm6         ? "-sm6"
                                   : model == HostShaderModel::automatic ? "-auto"
                                                                         : "";
    const auto name =
        hash + "-" + letter + "-v" + std::to_string(translator_version) + model_name +
        (signed_inputs ? "-s" + std::to_string(signed_inputs) : "") +
        (root_signature.empty() ? "" : "-r" + hash_name(root_signature).substr(0, 12)) +
        (stand_ins_only ? "-stand-in" : "") + ".dxbc";
    // one thread makes a shader, and the others that want the same one wait for it: titles
    // make their pipelines on many threads, and the host's compiler is slow
    {
        std::unique_lock lock(mutex);
        made_one.wait(lock, [&] { return !making.count(name); });
        if (const auto found = made.find(name); found != made.end())
            return found->second;
        making.insert(name);
    }
    const auto &folder = shader_cache_directory();
    std::vector<std::uint8_t> shader;
    // XV_SHADER_WHY: every shader made anew, and what stopped the ones with a stand-in
    static const bool asking = std::getenv("XV_SHADER_WHY") != nullptr;
    static std::map<std::string, unsigned> reasons;
    if (!folder.empty() && !asking)
        shader = read_file(folder / name);
    const auto code = console_shader_code(container);
    // XV_SHADER_CODE=<folder>: of every console shader met, the machine code (.bin), the
    // whole of what it was stored in (.fssm) and the shader itself (.dxbc)
    if (const auto *const dump = std::getenv("XV_SHADER_CODE")) {
        const auto base = std::filesystem::path(dump) / (hash + "-" + letter);
        if (!code.empty())
            write_file(base.wstring() + L".bin",
                       {reinterpret_cast<const std::uint8_t *>(code.data()), code.size() * 4});
        write_file(base.wstring() + L".fssm", code_container(container));
        write_file(base.wstring() + L".dxbc", container); // the shader as the title has it
        if (!root_signature.empty())
            write_file(base.wstring() + L".rts0", root_signature);
    }
    if (shader.empty()) {
        std::string why = "stand-ins asked for";
        bool needs_sm6 = false;
        auto source = stand_ins_only ? std::nullopt
                                     : translated_source(stage, container, &why, nullptr,
                                                         &needs_sm6, signed_inputs, root_signature);
        const bool sm6 =
            model == HostShaderModel::sm6 || (model == HostShaderModel::automatic && needs_sm6);
        if (needs_sm6 && !sm6) {
            source.reset();
            why = "the pipeline needs Shader Model 5 shaders";
        }
        // XV_SHADER_SKIP=<text>[,<text>...]: a stand-in for the shaders whose HLSL has any
        // of these in it, to find which kind of shader breaks something
        if (const auto *const skip = std::getenv("XV_SHADER_SKIP"); skip && source) {
            const std::string list = skip;
            for (std::size_t at = 0; at <= list.size() && source;) {
                const auto end = std::min(list.find(',', at), list.size());
                if (end > at && source->find(list.substr(at, end - at)) != std::string::npos)
                    source.reset();
                at = end + 1;
            }
        }
        // XV_SHADER_CODE: the HLSL made for it as well
        if (const auto *const dump = std::getenv("XV_SHADER_CODE"); dump && source) {
            const auto text = std::filesystem::path(dump) / (hash + "-" + letter + ".hlsl");
            write_file(text,
                       {reinterpret_cast<const std::uint8_t *>(source->data()), source->size()});
        }
        const bool kept = !folder.empty() && !asking;
        if (source)
            shader = compile(stage, *source, kept ? folder / name : std::filesystem::path{}, sm6);
        const bool done = !shader.empty();
        if (done || model != HostShaderModel::sm6)
            ++(done ? translated : stood_in);
        const std::lock_guard lock(mutex);
        if (!done && model != HostShaderModel::sm6) {
            if (asking)
                ++reasons[std::string(1, letter) + ": " + (source ? "the host's compiler" : why)];
            const auto text = stand_in(stage, container);
            if (const auto *const dump = std::getenv("XV_SHADER_CODE"))
                write_file(std::filesystem::path(dump) / (hash + "-" + letter + ".stand-in.hlsl"),
                           {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
            shader = compile(stage, text);
        } else if (done && kept) {
            // only what was translated is kept: a stand-in is made again, in case the next
            // version translates the shader
            write_file(folder / name, shader);
        }
        const auto total = translated + stood_in;
        if (std::has_single_bit(total))
            std::fprintf(stderr,
                         "[Runtime] console shaders made for the host so far: %u translated, %u "
                         "with a stand-in that draws nothing%s\n",
                         translated.load(), stood_in.load(),
                         folder.empty() ? "" : " (kept in the shader cache)");
        if (asking && std::has_single_bit(total) && total >= 256)
            for (const auto &[reason, count] : reasons)
                std::fprintf(stderr, "[Runtime]   %u stopped by %s\n", count, reason.c_str());
    }
    const std::lock_guard lock(mutex);
    making.erase(name);
    made_one.notify_all();
    return made.emplace(name, std::move(shader)).first->second;
}
} // namespace

std::span<const std::uint8_t> empty_compute_shader() {
    static const auto shader = compile(ShaderStage::compute, stand_in(ShaderStage::compute, {}));
    return shader;
}

std::span<const std::uint8_t> host_shader(ShaderStage stage, Bytes container, HostShaderModel model,
                                          std::span<const PackedInput> packed,
                                          Bytes root_signature) {
    if (model != HostShaderModel::sm5 && stage != ShaderStage::vertex &&
        stage != ShaderStage::pixel)
        return {};
    return make_host_shader(stage, container, model, packed, root_signature);
}
} // namespace xv::runtime::modules
