/*
 * File: sail_lcp.hpp
 * Description: SAIL backend. grlBWT builds the RLBWT of RefBuilder::text;
 *              SAIL streams BWT/LCP/SA/DA into mem_finder.
 */
#ifndef SAIL_LCP_HH
#define SAIL_LCP_HH

#include <sail.hpp>

#include <ref_builder.hpp>
#include <pfp_mum.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifndef MUMEMTO_GRLBWT_BIN
#define MUMEMTO_GRLBWT_BIN "grlbwt-cli"
#endif

class sail_lcp {
public:
    using stream_type = decltype(sail::make_stream(
                                     std::declval<std::vector<sail::symbol_type>&>(),
                                     std::declval<std::vector<sail::length_type>&>())
                                     .with_bwt()
                                     .with_lcp()
                                     .with_da(std::declval<std::vector<sail::offsets_type>&>())
                                     .threads(1)
                                     .fast()
                                     .forward());

    RefBuilder* ref_build;
    std::string prefix;
    std::string rlbwt_base;
    size_t threads;
    bool keep_temp;
    bool own_rlbwt;
    std::vector<sail::symbol_type> heads;
    std::vector<sail::length_type> lengths;
    std::vector<sail::offsets_type> da_offsets;
    std::optional<stream_type> stream;

    sail_lcp(std::string output_prefix, RefBuilder* ref, std::string rlbwt_prefix,
             size_t n_threads, bool keep_temp_files)
        : ref_build(ref),
          prefix(std::move(output_prefix)),
          threads(n_threads == 0 ? 1 : n_threads),
          keep_temp(keep_temp_files),
          own_rlbwt(rlbwt_prefix.empty()) {
        rlbwt_base = rlbwt_prefix.empty() ? prefix : std::move(rlbwt_prefix);
        build_da_offsets();

        // Like PFP's -p: only reuse sidecars when -R is passed explicitly.
        if (!own_rlbwt) {
            if (!is_file(rlbwt_base + ".bwt.heads") || !is_file(rlbwt_base + ".bwt.len")) {
                FATAL_ERROR(("RLBWT sidecars missing at " + rlbwt_base).c_str());
            }
            load_rlbwt(rlbwt_base);
        } else {
            build_rlbwt_with_grlbwt();
            load_rlbwt(rlbwt_base);
            if (!keep_temp) {
                std::filesystem::remove(rlbwt_base + ".bwt.heads");
                std::filesystem::remove(rlbwt_base + ".bwt.len");
            }
        }
    }

    void construct() {
        stream.emplace(sail::make_stream(heads, lengths)
                           .with_bwt()
                           .with_lcp()
                           .with_da(da_offsets)
                           .threads(threads)
                           .fast()
                           .forward());
    }

    template <class T>
    size_t process(T& match_finder) {
        if (!stream) {
            FATAL_ERROR("SAIL stream was not constructed");
        }
        size_t count = 0;
        size_t j = 0;
        const size_t doc_span = ref_build->total_length;
        const size_t text_size = doc_span + 1;
        size_t pb_inc = text_size / PBWIDTH;
        if (pb_inc == 0) pb_inc = 1;
        for (auto it = stream->begin(); it != stream->end(); ++it, ++j) {
            if (j % pb_inc == 0) {
                printProgress(static_cast<double>(j) / static_cast<double>(text_size));
            }
            const size_t sa = static_cast<size_t>(it.sa());
            const size_t doc = ref_build->doc_ends_rank(sa);
#ifndef NDEBUG
            if (sa < doc_span) {
                assert(static_cast<size_t>(it.da()) == doc);
            }
#else
            (void)doc_span;
#endif
            count += match_finder.update(j, static_cast<uint8_t>(it.bwt()), doc, sa,
                                          static_cast<size_t>(it.lcp()));
        }
        printProgress(1.0);
        return count;
    }

private:
    void build_da_offsets() {
        size_t curr = 0;
        da_offsets.clear();
        da_offsets.reserve(ref_build->seq_lengths.size());
        for (size_t len : ref_build->seq_lengths) {
            da_offsets.push_back(static_cast<sail::offsets_type>(curr));
            curr += len;
        }
        if (da_offsets.empty()) {
            FATAL_ERROR("SAIL backend: no document lengths");
        }
        if (curr != ref_build->total_length) {
            FATAL_ERROR("SAIL backend: document lengths do not sum to total_length");
        }
    }

    void build_rlbwt_with_grlbwt() {
        if (ref_build->text.empty() || ref_build->text.back() != static_cast<uint8_t>('$')) {
            FATAL_ERROR("SAIL backend: concatenated text must be non-empty and end with '$'");
        }
        const std::string text_path = prefix + ".grl_text";
        {
            std::ofstream out(text_path, std::ios::binary | std::ios::trunc);
            if (!out) {
                FATAL_ERROR(("failed to write " + text_path).c_str());
            }
            out.write(reinterpret_cast<const char*>(ref_build->text.data()),
                      static_cast<std::streamsize>(ref_build->text.size()));
            // EOF byte. PFP's BWT is this text plus one trailing 0, and that 0
            // is grlBWT's only separator, so the file is a single string.
            out.put('\0');
        }

        std::filesystem::path tmp_dir = std::filesystem::path(prefix).parent_path();
        if (tmp_dir.empty()) tmp_dir = ".";
        const std::string cmd = std::string("\"") + MUMEMTO_GRLBWT_BIN + "\" \"" +
                                text_path + "\" -o \"" + prefix + "\" -t " +
                                std::to_string(threads) + " -T \"" +
                                tmp_dir.string() + "\" >/dev/null 2>&1";
        STATUS_LOG("grlbwt", "computing RLBWT");
        const auto grl_start = std::chrono::system_clock::now();
        const int rc = std::system(cmd.c_str());
        if (rc != 0) {
            FATAL_ERROR("grlBWT failed");
        }

        const std::string rl_path = prefix + ".rl_bwt";
        if (!is_file(rl_path)) {
            FATAL_ERROR(("grlBWT did not write " + rl_path).c_str());
        }
        rl_bwt_to_heads_len(rl_path, prefix);
        std::filesystem::remove(rl_path);
        std::filesystem::remove(text_path);
        DONE_LOG((std::chrono::system_clock::now() - grl_start));
        rlbwt_base = prefix;
    }

    static void rl_bwt_to_heads_len(const std::string& rl_path, const std::string& out_prefix) {
        std::ifstream in(rl_path, std::ios::binary);
        if (!in) {
            FATAL_ERROR(("failed to open " + rl_path).c_str());
        }
        size_t sb = 0;
        size_t fb = 0;
        in.read(reinterpret_cast<char*>(&sb), sizeof(size_t));
        in.read(reinterpret_cast<char*>(&fb), sizeof(size_t));
        if (!in || sb == 0 || sb > 8 || fb == 0 || fb > 8) {
            FATAL_ERROR("invalid grlBWT .rl_bwt header");
        }

        std::ofstream heads(out_prefix + ".bwt.heads", std::ios::binary | std::ios::trunc);
        std::ofstream lens(out_prefix + ".bwt.len", std::ios::binary | std::ios::trunc);
        if (!heads || !lens) {
            FATAL_ERROR("failed to open RLBWT sidecar outputs");
        }

        std::vector<unsigned char> rec(static_cast<size_t>(sb + fb));
        uint64_t runs = 0;
        while (in.read(reinterpret_cast<char*>(rec.data()),
                       static_cast<std::streamsize>(rec.size()))) {
            uint64_t sym = 0;
            uint64_t len = 0;
            std::memcpy(&sym, rec.data(), static_cast<size_t>(sb));
            std::memcpy(&len, rec.data() + sb, static_cast<size_t>(fb));
            if (len == 0 || sym > 255) {
                FATAL_ERROR("invalid run in .rl_bwt");
            }
            heads.put(static_cast<char>(sym));
            unsigned char buf[5];
            for (size_t b = 0; b < 5; ++b) {
                buf[b] = static_cast<unsigned char>((len >> (8 * b)) & 0xffu);
            }
            lens.write(reinterpret_cast<char*>(buf), 5);
            ++runs;
        }
        if (runs == 0) {
            FATAL_ERROR(".rl_bwt contained no runs");
        }
    }

    void load_rlbwt(const std::string& base) {
        const std::string heads_path = base + ".bwt.heads";
        const std::string lens_path = base + ".bwt.len";
        std::ifstream hin(heads_path, std::ios::binary);
        std::ifstream lin(lens_path, std::ios::binary);
        if (!hin || !lin) {
            FATAL_ERROR(("failed to open RLBWT sidecars at " + base).c_str());
        }
        hin.seekg(0, std::ios::end);
        const auto nruns = static_cast<size_t>(hin.tellg());
        hin.seekg(0, std::ios::beg);
        lin.seekg(0, std::ios::end);
        const auto len_bytes = static_cast<size_t>(lin.tellg());
        lin.seekg(0, std::ios::beg);
        if (len_bytes != nruns * 5) {
            FATAL_ERROR("RLBWT .bwt.len size does not match heads");
        }
        heads.resize(nruns);
        lengths.resize(nruns);
        if (nruns != 0 &&
            !hin.read(reinterpret_cast<char*>(heads.data()),
                      static_cast<std::streamsize>(nruns))) {
            FATAL_ERROR("failed reading heads");
        }
        unsigned char buf[5];
        size_t domain = 0;
        for (size_t i = 0; i < nruns; ++i) {
            if (!lin.read(reinterpret_cast<char*>(buf), 5)) {
                FATAL_ERROR("failed reading lengths");
            }
            sail::length_type value = 0;
            for (size_t b = 0; b < 5; ++b) {
                value |= static_cast<sail::length_type>(buf[b]) << (8 * b);
            }
            if (value == 0) {
                FATAL_ERROR("zero-length BWT run");
            }
            lengths[i] = value;
            domain += static_cast<size_t>(value);
        }
        const size_t expect = ref_build->total_length + 1;
        if (!ref_build->text.empty() && domain != ref_build->text.size() + 1) {
            FATAL_ERROR("RLBWT domain does not match concatenated text length + EOF");
        }
        if (domain != expect) {
            FATAL_ERROR("RLBWT domain does not match RefBuilder::total_length + EOF");
        }
    }
};

#endif
