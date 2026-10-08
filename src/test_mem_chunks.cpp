#include <mem_chunks.hpp>
#include <pfp_lcp_mum.hpp>
#include <ref_builder.hpp>
#include <sail_lcp.hpp>

#include <sail.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int is_file(std::string path) { return std::filesystem::is_regular_file(path); }

int is_dir(std::string path) { return std::filesystem::exists(path); }

std::vector<std::string> split(std::string input, char delim) {
    std::vector<std::string> word_list;
    std::string curr_word;
    for (char ch : input) {
        if (ch == delim && !curr_word.empty()) {
            word_list.push_back(curr_word);
            curr_word.clear();
        } else if (ch != delim) {
            curr_word += ch;
        }
    }
    if (!curr_word.empty())
        word_list.push_back(curr_word);
    return word_list;
}

namespace {

struct vec_part {
    const std::vector<stream_row>* rows = nullptr;
    size_t b = 0;
    size_t e = 0;
    size_t start_index() const { return b; }
    size_t end_index() const { return e; }

    struct iterator {
        const std::vector<stream_row>* rows = nullptr;
        size_t i = 0;
        size_t index() const { return i; }
        stream_row row() const { return (*rows)[i]; }
        iterator& operator++() {
            ++i;
            return *this;
        }
        bool operator==(const iterator& other) const { return i == other.i; }
        bool operator!=(const iterator& other) const { return !(*this == other); }
    };

    iterator begin() const { return iterator{rows, b}; }
    iterator end() const { return iterator{rows, e}; }
};

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void expect_eq(const std::string& what, const std::string& a, const std::string& b) {
    if (a == b)
        return;
    std::cerr << what << " mismatch\n--- serial ---\n" << a << "--- chunked ---\n" << b;
    std::exit(1);
}

mem_finder make_finder(const std::string& prefix, RefBuilder& ref, size_t min_len, size_t distinct,
                       int doc_freq, int total_freq, bool binary, bool merge) {
    return mem_finder(prefix, ref, min_len, distinct, doc_freq, total_freq, binary, merge, false);
}

size_t run_serial(mem_finder& finder, const std::vector<stream_row>& rows) {
    size_t count = 0;
    for (size_t j = 0; j < rows.size(); ++j) {
        const auto& row = rows[j];
        count += finder.update(j, row.bwt, row.doc, row.sa, row.lcp);
    }
    return count;
}

size_t run_parts(mem_finder& owner, const std::vector<stream_row>& rows,
                 const std::vector<std::pair<size_t, size_t>>& cuts, bool overlap, size_t width,
                 RefBuilder& ref, size_t min_len, size_t distinct, int doc_freq, int total_freq,
                 bool binary, bool merge) {
    std::vector<vec_part> parts;
    parts.reserve(cuts.size());
    for (const auto& cut : cuts)
        parts.push_back(vec_part{&rows, cut.first, cut.second});
    auto decode = [](auto& it) { return it.row(); };
    auto make_part = [&](int id) {
        return std::make_unique<mem_finder>(owner.filename, ref, min_len, distinct, doc_freq,
                                            total_freq, binary, merge, false, id);
    };
    return run_chunked_match_finding(owner, parts, rows.size(), overlap, width, decode, make_part);
}

std::vector<std::pair<size_t, size_t>> equal_cuts(size_t n, size_t k) {
    std::vector<std::pair<size_t, size_t>> cuts;
    for (size_t i = 0; i < k; ++i) {
        const size_t b = (i * n) / k;
        const size_t e = ((i + 1) * n) / k;
        cuts.push_back({b, e});
    }
    return cuts;
}

void check_case(const std::string& dir, const std::vector<stream_row>& rows, RefBuilder& ref,
                const std::vector<std::pair<size_t, size_t>>& cuts, size_t min_len, size_t distinct,
                int doc_freq, int total_freq, bool binary, bool merge, const std::string& tag) {
    std::filesystem::create_directories(dir);
    const std::string serial_prefix = dir + "/serial_" + tag;
    const std::string chunk_prefix = dir + "/chunk_" + tag;
    const bool overlap = total_freq > 0;
    const size_t width = overlap ? static_cast<size_t>(total_freq) : 0;
    const std::string ext = binary ? ".bumbl" : (doc_freq == 1 ? ".mums" : ".mems");

    mem_finder serial = make_finder(serial_prefix, ref, min_len, distinct, doc_freq, total_freq, binary, merge);
    const size_t serial_count = run_serial(serial, rows);
    serial.close();

    mem_finder chunked = make_finder(chunk_prefix, ref, min_len, distinct, doc_freq, total_freq, binary, merge);
    const size_t chunk_count = run_parts(chunked, rows, cuts, overlap, width, ref, min_len, distinct,
                                          doc_freq, total_freq, binary, merge);
    chunked.close();

    if (serial_count != chunk_count) {
        std::cerr << tag << " count " << serial_count << " vs " << chunk_count << "\n";
        std::exit(1);
    }
    if (serial_count == 0) {
        std::cerr << tag << " produced no matches; fixture is too weak\n";
        std::exit(1);
    }
    expect_eq(tag + " output", slurp(serial_prefix + ext), slurp(chunk_prefix + ext));
    if (merge) {
        expect_eq(tag + " thresh", slurp(serial_prefix + ".thresh"), slurp(chunk_prefix + ".thresh"));
        expect_eq(tag + " thresh_rev", slurp(serial_prefix + ".thresh_rev"),
                  slurp(chunk_prefix + ".thresh_rev"));
    }
}

std::vector<stream_row> synthetic_rows() {
    // Three documents, lengths 8 + terminator => RefBuilder lengths 9.
    const size_t offs[3] = {0, 9, 18};
    const size_t n = 27;
    std::vector<stream_row> rows(n);
    for (size_t j = 0; j < n; ++j) {
        rows[j].doc = j % 3;
        rows[j].sa = offs[rows[j].doc] + ((j / 3) % 8);
        rows[j].bwt = static_cast<uint8_t>("ACGT"[j % 4]);
        if (j == 0)
            rows[j].lcp = 0;
        else if (j >= 8 && j <= 20)
            rows[j].lcp = 15;
        else if (j % 5 == 0)
            rows[j].lcp = 0;
        else
            rows[j].lcp = 3 + (j % 4);
    }
    return rows;
}

void check_array_fixtures(const std::string& dir) {
    const std::vector<std::vector<size_t>> lens = {{8}, {8}, {8}};
    RefBuilder ref(lens, false);
    const auto rows = synthetic_rows();
    const auto uneven = std::vector<std::pair<size_t, size_t>>{{0, 5}, {5, 6}, {6, 15}, {15, rows.size()}};
    const auto quarters = equal_cuts(rows.size(), 4);

    check_case(dir, rows, ref, uneven, 4, 2, 1, 3, false, false, "mum_overlap_uneven");
    check_case(dir, rows, ref, quarters, 4, 2, 1, 3, false, false, "mum_overlap_quarters");
    check_case(dir, rows, ref, uneven, 4, 2, 3, 0, false, false, "mem_stitch_uneven");
    check_case(dir, rows, ref, quarters, 4, 2, 3, 0, false, false, "mem_stitch_quarters");
    check_case(dir, rows, ref, uneven, 4, 2, 1, 3, true, false, "mum_binary");
    check_case(dir, rows, ref, quarters, 4, 2, 1, 3, false, true, "mum_merge");
}

void check_sail(const std::string& dir) {
    std::filesystem::create_directories(dir);
    const std::string sail_prefix = dir + "/idx";
    // Known-valid RLBWT from SAIL's smoke test (domain 27). Lengths file stores
    // sequence length excluding the terminator; RefBuilder adds it back so the
    // document spans are 4, 4, and 19 (offsets 0, 4, 8).
    {
        std::ofstream lengths(sail_prefix + ".lengths");
        lengths << "a 3\nb 3\nc 18\n";
    }
    const std::vector<sail::symbol_type> heads{'T', 'C', 'G', 'A', 'T', 1, 'A', 'T', 'A'};
    const std::vector<sail::length_type> lengths{5, 3, 3, 3, 1, 1, 1, 4, 6};
    std::vector<sail::offsets_type> offsets = {0, 4, 8};
    {
        auto stream = sail::make_stream(heads, lengths)
                          .with_bwt()
                          .with_lcp()
                          .with_da(offsets)
                          .threads(1)
                          .max_chunks(8)
                          .fast()
                          .forward();
        std::ofstream out(sail_prefix + ".sail", std::ios::binary);
        stream.serialize(out);
    }

    RefBuilder ref(sail_prefix, false);
    ref.set_total_length();

    auto run = [&](const std::string& prefix, size_t threads, int doc_freq, int total_freq) {
        sail_lcp sail(prefix, &ref, sail_prefix, threads, true);
        sail.construct();
        mem_finder finder(prefix, ref, 1, 1, doc_freq, total_freq, false, false, false);
        const size_t count = sail.process(finder);
        finder.close();
        return count;
    };

    const std::string ext_mum = ".mems";
    const std::string ext_mem = ".mems";
    const size_t mum1 = run(dir + "/mum1", 1, 2, 8);
    const size_t mum4 = run(dir + "/mum4", 4, 2, 8);
    if (mum1 == 0 || mum1 != mum4) {
        std::cerr << "sail MUM counts " << mum1 << " vs " << mum4 << "\n";
        std::exit(1);
    }
    expect_eq("sail mum", slurp(dir + "/mum1" + ext_mum), slurp(dir + "/mum4" + ext_mum));

    const size_t mem1 = run(dir + "/mem1", 1, 4, 0);
    const size_t mem4 = run(dir + "/mem4", 4, 4, 0);
    if (mem1 == 0 || mem1 != mem4) {
        std::cerr << "sail MEM counts " << mem1 << " vs " << mem4 << "\n";
        std::exit(1);
    }
    expect_eq("sail mem", slurp(dir + "/mem1" + ext_mem), slurp(dir + "/mem4" + ext_mem));
}

}  // namespace

int main() {
    mumemto_set_progress_enabled(false);
    const std::string dir = "test_mem_chunks_tmp";
    std::filesystem::remove_all(dir);
    check_array_fixtures(dir + "/array");
    check_sail(dir + "/sail");
    std::filesystem::remove_all(dir);
    std::cout << "mem chunk tests passed\n";
    return 0;
}
