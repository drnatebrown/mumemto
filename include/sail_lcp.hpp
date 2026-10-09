/*
 * File: sail_lcp.hpp
 * Description: SAIL backend. grlBWT builds the RLBWT of a streamed .grl_text
 *              (not RefBuilder::text). SAIL streams BWT/LCP/SA/DA into mem_finder.
 *              Document ids come from SAIL DA. The SDSL doc_ends bitvector is
 *              not used here; PFP, gsacak, and --arrays-in still rank that bitvector.
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
#include <stdexcept>
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

        // -R reuses PREFIX.sail (like PFP -p reuses .parse/.dict). Otherwise build.
        if (!own_rlbwt) {
            if (!is_file(rlbwt_base + ".sail")) {
                FATAL_ERROR(("SAIL index missing at " + rlbwt_base + ".sail").c_str());
            }
        } else {
            build_rlbwt_with_grlbwt();
        }
    }

    void construct() {
        if (!own_rlbwt) {
            std::ifstream in(rlbwt_base + ".sail", std::ios::binary);
            if (!in) {
                FATAL_ERROR(("failed to open " + rlbwt_base + ".sail").c_str());
            }
            stream.emplace();
            try {
                stream->load(in);
            } catch (const std::exception& ex) {
                FATAL_ERROR(ex.what());
            }
            return;
        }

        stream.emplace(sail::make_stream(heads, lengths)
                           .with_bwt()
                           .with_lcp()
                           .with_da(da_offsets)
                           .threads(threads)
                           .fast()
                           .forward());
        heads.clear();
        lengths.clear();
        heads.shrink_to_fit();
        lengths.shrink_to_fit();

        if (!keep_temp) {
            return;
        }
        const std::string sail_path = prefix + ".sail";
        std::ofstream out(sail_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            FATAL_ERROR(("failed to write " + sail_path).c_str());
        }
        try {
            stream->serialize(out);
        } catch (const std::exception& ex) {
            FATAL_ERROR(ex.what());
        }
        if (!out) {
            FATAL_ERROR(("failed while writing " + sail_path).c_str());
        }
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
            // The EOF symbol (sa == total_length) is outside every document.
            const size_t doc = (sa < doc_span)
                                   ? static_cast<size_t>(it.da())
                                   : ref_build->num_docs;
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
        // .grl_text is written by RefBuilder::build_sail_grl_text (seq$ [rc$] ... \0).
        const std::string text_path = prefix + ".grl_text";
        if (!is_file(text_path)) {
            FATAL_ERROR(("SAIL backend: missing " + text_path).c_str());
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
        load_rl_bwt(rl_path);
        std::filesystem::remove(rl_path);
        std::filesystem::remove(text_path);
        DONE_LOG((std::chrono::system_clock::now() - grl_start));
        rlbwt_base = prefix;
    }

    void load_rl_bwt(const std::string& rl_path) {
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

        std::vector<unsigned char> rec(static_cast<size_t>(sb + fb));
        heads.clear();
        lengths.clear();
        size_t domain = 0;
        while (in.read(reinterpret_cast<char*>(rec.data()),
                       static_cast<std::streamsize>(rec.size()))) {
            uint64_t sym = 0;
            uint64_t len = 0;
            std::memcpy(&sym, rec.data(), static_cast<size_t>(sb));
            std::memcpy(&len, rec.data() + sb, static_cast<size_t>(fb));
            if (len == 0 || sym > 255) {
                FATAL_ERROR("invalid run in .rl_bwt");
            }
            heads.push_back(static_cast<sail::symbol_type>(sym));
            lengths.push_back(static_cast<sail::length_type>(len));
            domain += static_cast<size_t>(len);
        }
        if (heads.empty()) {
            FATAL_ERROR(".rl_bwt contained no runs");
        }
        const size_t expect = ref_build->total_length + 1;
        if (domain != expect) {
            FATAL_ERROR("RLBWT domain does not match RefBuilder::total_length + EOF");
        }
    }
};

#endif
