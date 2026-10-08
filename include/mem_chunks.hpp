/*
 * Chunked MUM/MEM finding over a forward SA/LCP/BWT/DA stream.
 *
 * Finite max frequency uses an overlap of width W so every accepted SA-interval
 * is rebuilt inside the chunk that owns its right endpoint. Unbounded frequency
 * emits intervals that sit strictly inside a chunk, then a serial stitch closes
 * intervals that cross chunk cuts. After a chunk's LCP stack falls idle, later
 * state no longer depends on the incoming stack, so the stitch can adopt the
 * fresh chunk's end snapshot instead of replaying the rest of the chunk.
 */
#ifndef MEM_CHUNKS_HH
#define MEM_CHUNKS_HH

#include <pfp_mum.hpp>
#include <mem_finder.hpp>

#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

struct stream_row {
    uint8_t bwt = 0;
    size_t doc = 0;
    size_t sa = 0;
    size_t lcp = 0;
};

template <class Parts, class Fn>
void for_each_stream_index(Parts& parts, size_t first, size_t last_inclusive, Fn&& fn) {
    if (first > last_inclusive)
        return;
    size_t p = 0;
    const size_t nparts = parts.size();
    while (p + 1 < nparts && static_cast<size_t>(parts[p].end_index()) <= first)
        ++p;
    if (p >= nparts)
        FATAL_ERROR("chunk range starts past the stream");
    auto it = parts[p].begin();
    for (size_t index = first; index <= last_inclusive; ++index) {
        while (it == parts[p].end() || static_cast<size_t>(it.index()) < index) {
            if (it == parts[p].end()) {
                ++p;
                if (p >= nparts)
                    FATAL_ERROR("chunk range walks off the end of the stream");
                it = parts[p].begin();
            } else {
                ++it;
            }
        }
        if (static_cast<size_t>(it.index()) != index)
            FATAL_ERROR("chunk iterator missed a stream index");
        fn(index, it);
    }
}

inline std::vector<mem_finder::binary_record> merge_binary_records(
    std::vector<std::vector<mem_finder::binary_record>>& interiors,
    const std::vector<mem_finder::binary_record>& stitch) {
    std::vector<mem_finder::binary_record> out;
    size_t part = 0;
    size_t ii = 0;
    size_t si = 0;
    auto interior = [&]() -> mem_finder::binary_record* {
        while (part < interiors.size()) {
            if (ii < interiors[part].size())
                return &interiors[part][ii];
            ++part;
            ii = 0;
        }
        return nullptr;
    };
    while (true) {
        auto* left = interior();
        const bool right = si < stitch.size();
        if (left == nullptr && !right)
            break;
        if (left != nullptr && (!right || left->close_j <= stitch[si].close_j)) {
            out.push_back(std::move(*left));
            ++ii;
        } else {
            out.push_back(stitch[si]);
            ++si;
        }
    }
    return out;
}

// parts cover [0, domain) in order. overlap selects the width-W path;
// otherwise the unbounded stitch path is used. make_part(id) builds a private
// finder (id >= 0) that does not write side files.
template <class Parts, class Decode, class Factory>
size_t run_chunked_match_finding(mem_finder& owner, Parts& parts, size_t domain,
                                 bool overlap, size_t width, Decode decode, Factory make_part) {
    const size_t k = parts.size();
    if (k == 0 || domain == 0)
        return 0;
    const size_t none = std::numeric_limits<size_t>::max();
    std::vector<size_t> counts(k, 0);
    std::vector<std::unique_ptr<mem_finder>> workers(k);
    std::vector<mem_finder::frontier> frontiers(k);
    std::vector<size_t> sync_at(k, none);

    const int threads = static_cast<int>(k > static_cast<size_t>(std::numeric_limits<int>::max())
                                              ? std::numeric_limits<int>::max()
                                              : k);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(threads)
#endif
    for (int ii = 0; ii < static_cast<int>(k); ++ii) {
        const size_t i = static_cast<size_t>(ii);
        const size_t own_lo = static_cast<size_t>(parts[i].start_index());
        const size_t own_hi = static_cast<size_t>(parts[i].end_index());
        if (own_lo >= own_hi)
            continue;
        workers[i] = make_part(static_cast<int>(i));
        mem_finder& finder = *workers[i];
        const size_t last = (own_hi < domain) ? own_hi : domain - 1;
        size_t count = 0;

        if (!overlap && k > 1)
            finder.enable_close_index_prefix();

        if (overlap) {
            size_t halo = 0;
            if (width < own_lo)
                halo = own_lo - width;
            finder.set_keep_right_range(own_lo, own_hi);
            bool seeded = false;
            for_each_stream_index(parts, halo, last, [&](size_t j, auto& it) {
                const stream_row row = decode(it);
                if (!seeded) {
                    seeded = true;
                    if (halo == 0)
                        count += finder.update(j, row.bwt, row.doc, row.sa, row.lcp);
                    else
                        finder.prime(j, row.bwt, row.doc, row.sa, row.lcp);
                    return;
                }
                count += finder.update(j, row.bwt, row.doc, row.sa, row.lcp);
            });
        } else {
            finder.enable_close_index_prefix();
            if (i == 0) {
                finder.set_keep_all();
                for_each_stream_index(parts, 0, last, [&](size_t j, auto& it) {
                    const stream_row row = decode(it);
                    count += finder.update(j, row.bwt, row.doc, row.sa, row.lcp);
                });
                frontiers[i] = finder.capture_frontier();
            } else {
                finder.set_keep_interior(own_lo, own_hi);
                bool seeded = false;
                for_each_stream_index(parts, own_lo, last, [&](size_t j, auto& it) {
                    const stream_row row = decode(it);
                    if (!seeded) {
                        seeded = true;
                        finder.prime(j, row.bwt, row.doc, row.sa, row.lcp);
                        return;
                    }
                    count += finder.update(j, row.bwt, row.doc, row.sa, row.lcp);
                    if (sync_at[i] == none && finder.stack_idle())
                        sync_at[i] = j;
                });
                frontiers[i] = finder.capture_frontier();
            }
        }
        counts[i] = count;
    }

    size_t total = 0;
    for (size_t c : counts)
        total += c;

    std::unique_ptr<mem_finder> stitch;
    if (!overlap && k > 1) {
        stitch = make_part(static_cast<int>(k));
        stitch->enable_close_index_prefix();
        mem_finder::frontier state = frontiers[0];
        for (size_t i = 1; i < k; ++i) {
            const size_t own_lo = static_cast<size_t>(parts[i].start_index());
            const size_t own_hi = static_cast<size_t>(parts[i].end_index());
            if (own_lo >= own_hi)
                continue;
            const size_t incoming_bwt = state.last_bwt_change;
            const size_t last = (own_hi < domain) ? own_hi : domain - 1;
            const size_t replay_until = sync_at[i] == none ? last : sync_at[i];
            stitch->restore_frontier(state);
            stitch->set_keep_not_interior(own_lo, own_hi);
            if (own_lo + 1 <= replay_until) {
                for_each_stream_index(parts, own_lo + 1, replay_until, [&](size_t j, auto& it) {
                    const stream_row row = decode(it);
                    total += stitch->update(j, row.bwt, row.doc, row.sa, row.lcp);
                });
            }
            if (sync_at[i] != none) {
                state = frontiers[i];
                if (!state.saw_real_bwt_change)
                    state.last_bwt_change = incoming_bwt;
            } else {
                state = stitch->capture_frontier();
            }
        }
    }

    std::vector<std::string> interior_paths;
    std::vector<std::vector<mem_finder::binary_record>> interior_bins;
    if (owner.binary)
        interior_bins.resize(k);
    for (size_t i = 0; i < k; ++i) {
        if (!workers[i])
            continue;
        owner.absorb_merge_state(*workers[i]);
        workers[i]->close();
        if (owner.binary) {
            interior_bins[i] = workers[i]->release_binary();
        } else if (!workers[i]->part_text_path().empty()) {
            interior_paths.push_back(workers[i]->part_text_path());
        }
    }

    std::string stitch_path;
    std::vector<mem_finder::binary_record> stitch_bins;
    if (stitch) {
        owner.absorb_merge_state(*stitch);
        stitch->close();
        if (owner.binary)
            stitch_bins = stitch->release_binary();
        else
            stitch_path = stitch->part_text_path();
    }

    if (owner.binary) {
        owner.adopt_binary_records(merge_binary_records(interior_bins, stitch_bins));
    } else {
        owner.write_merged_text(interior_paths, stitch_path, !overlap && k > 1);
        for (const auto& path : interior_paths)
            std::filesystem::remove(path);
        if (!stitch_path.empty())
            std::filesystem::remove(stitch_path);
    }
    return total;
}

#endif
