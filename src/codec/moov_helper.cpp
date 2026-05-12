#include "moov_helper.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>

namespace vp {

struct FrameInfo {
    TimeMs start_ms = 0;
    TimeMs end_ms = 0;
    TimeMs dts_ms = 0;
    int64_t file_offset = 0;
    int32_t byte_size = 0;
    int64_t gop_size = 0;
    int gop_index = 0;
    int gop_start_idx = 0;
    int frame_in_gop = 0;
    int gop_frame_count = 0;
    int display_index = 0;
};

struct MoovHelper::Impl {
    std::vector<FrameInfo> frames;
    std::vector<int> display_order;
    std::vector<uint8_t> codec_config;
    int width = 0;
    int height = 0;
    TimeMs duration_ms = 0;
    double frame_rate = 0.0;
    bool has_video = false;
    mutable int last_index = 0;
};

static uint32_t u32be(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

static uint64_t u64be(const uint8_t *p) {
    return (uint64_t(u32be(p)) << 32) | u32be(p + 4);
}

struct BoxHeader {
    char type[5] = {};
    uint64_t total_size = 0;
    size_t header_size = 0;
};

static bool read_box_header(const uint8_t *buf, size_t avail, BoxHeader &out) {
    if (avail < 8) {
        return false;
    }
    const uint32_t s32 = u32be(buf);
    std::memcpy(out.type, buf + 4, 4);
    out.type[4] = 0;
    if (s32 == 1) {
        if (avail < 16) {
            return false;
        }
        out.total_size = u64be(buf + 8);
        out.header_size = 16;
    } else if (s32 == 0) {
        out.total_size = avail;
        out.header_size = 8;
    } else {
        out.total_size = s32;
        out.header_size = 8;
    }
    return out.total_size >= out.header_size;
}

template <typename Callback>
static void iter_boxes(const uint8_t *buf, size_t size, const Callback &cb) {
    size_t pos = 0;
    while (pos + 8 <= size) {
        BoxHeader hdr;
        if (!read_box_header(buf + pos, size - pos, hdr)) {
            break;
        }
        if (hdr.total_size > size - pos) {
            break;
        }
        const uint8_t *payload = buf + pos + hdr.header_size;
        const size_t payload_size = hdr.total_size - hdr.header_size;
        if (!cb(hdr.type, payload, payload_size)) {
            break;
        }
        pos += hdr.total_size;
    }
}

struct SttsEntry {
    uint32_t count = 0;
    uint32_t delta = 0;
};

struct CttsEntry {
    uint32_t count = 0;
    int64_t offset = 0;
};

struct StscEntry {
    uint32_t first_chunk_0 = 0;
    uint32_t samples_per_chunk = 0;
};

static std::vector<SttsEntry> parse_stts(const uint8_t *p, size_t sz) {
    if (sz < 8) {
        return {};
    }
    const uint32_t n = u32be(p + 4);
    if (sz < 8 + uint64_t(n) * 8) {
        return {};
    }
    std::vector<SttsEntry> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        out[i].count = u32be(p + 8 + i * 8);
        out[i].delta = u32be(p + 8 + i * 8 + 4);
    }
    return out;
}

static std::vector<CttsEntry> parse_ctts(const uint8_t *p, size_t sz) {
    if (sz < 8) {
        return {};
    }
    const uint8_t version = p[0];
    const uint32_t n = u32be(p + 4);
    if (sz < 8 + uint64_t(n) * 8) {
        return {};
    }
    std::vector<CttsEntry> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        out[i].count = u32be(p + 8 + i * 8);
        const uint32_t raw = u32be(p + 8 + i * 8 + 4);
        out[i].offset = (version == 1) ? static_cast<int32_t>(raw) : static_cast<int64_t>(raw);
    }
    return out;
}

static std::set<uint32_t> parse_stss(const uint8_t *p, size_t sz) {
    if (sz < 8) {
        return {};
    }
    const uint32_t n = u32be(p + 4);
    if (sz < 8 + uint64_t(n) * 4) {
        return {};
    }
    std::set<uint32_t> out;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t sample_number = u32be(p + 8 + i * 4);
        if (sample_number > 0) {
            out.insert(sample_number - 1);
        }
    }
    return out;
}

static std::vector<StscEntry> parse_stsc(const uint8_t *p, size_t sz) {
    if (sz < 8) {
        return {};
    }
    const uint32_t n = u32be(p + 4);
    if (sz < 8 + uint64_t(n) * 12) {
        return {};
    }
    std::vector<StscEntry> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t first_chunk = u32be(p + 8 + i * 12);
        out[i].first_chunk_0 = first_chunk > 0 ? first_chunk - 1 : 0;
        out[i].samples_per_chunk = u32be(p + 8 + i * 12 + 4);
    }
    return out;
}

static std::vector<int64_t> parse_stco(const uint8_t *p, size_t sz) {
    if (sz < 8) {
        return {};
    }
    const uint32_t n = u32be(p + 4);
    if (sz < 8 + uint64_t(n) * 4) {
        return {};
    }
    std::vector<int64_t> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = u32be(p + 8 + i * 4);
    }
    return out;
}

static std::vector<int64_t> parse_co64(const uint8_t *p, size_t sz) {
    if (sz < 8) {
        return {};
    }
    const uint32_t n = u32be(p + 4);
    if (sz < 8 + uint64_t(n) * 8) {
        return {};
    }
    std::vector<int64_t> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = static_cast<int64_t>(u64be(p + 8 + i * 8));
    }
    return out;
}

static std::vector<int32_t> parse_stsz(const uint8_t *p, size_t sz) {
    if (sz < 12) {
        return {};
    }
    const uint32_t default_size = u32be(p + 4);
    const uint32_t n = u32be(p + 8);
    std::vector<int32_t> out(n);
    if (default_size > 0) {
        std::fill(out.begin(), out.end(), static_cast<int32_t>(default_size));
        return out;
    }
    if (sz < 12 + uint64_t(n) * 4) {
        return {};
    }
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = static_cast<int32_t>(u32be(p + 12 + i * 4));
    }
    return out;
}

static bool build_frames(uint32_t timescale,
                         const std::vector<SttsEntry> &stts,
                         const std::vector<CttsEntry> &ctts,
                         const std::set<uint32_t> &stss,
                         const std::vector<StscEntry> &stsc,
                         const std::vector<int64_t> &chunk_offsets,
                         const std::vector<int32_t> &sample_sizes,
                         std::vector<FrameInfo> &out,
                         std::vector<int> &display_order) {
    const size_t n = sample_sizes.size();
    if (n == 0 || timescale == 0 || stts.empty() || stsc.empty() || chunk_offsets.empty()) {
        return false;
    }

    std::vector<uint64_t> dts(n);
    size_t sample_idx = 0;
    uint64_t dts_acc = 0;
    for (const auto &entry : stts) {
        for (uint32_t i = 0; i < entry.count && sample_idx < n; ++i, ++sample_idx) {
            dts[sample_idx] = dts_acc;
            dts_acc += entry.delta;
        }
    }
    if (sample_idx != n) {
        return false;
    }

    std::vector<int64_t> cto(n, 0);
    if (!ctts.empty()) {
        sample_idx = 0;
        for (const auto &entry : ctts) {
            for (uint32_t i = 0; i < entry.count && sample_idx < n; ++i, ++sample_idx) {
                cto[sample_idx] = entry.offset;
            }
        }
        if (sample_idx != n) {
            return false;
        }
    }

    std::vector<int64_t> file_offsets(n);
    sample_idx = 0;
    for (size_t chunk = 0; chunk < chunk_offsets.size() && sample_idx < n; ++chunk) {
        uint32_t samples_per_chunk = 1;
        for (int i = static_cast<int>(stsc.size()) - 1; i >= 0; --i) {
            if (stsc[i].first_chunk_0 <= chunk) {
                samples_per_chunk = stsc[i].samples_per_chunk;
                break;
            }
        }
        int64_t offset = chunk_offsets[chunk];
        for (uint32_t s = 0; s < samples_per_chunk && sample_idx < n; ++s, ++sample_idx) {
            file_offsets[sample_idx] = offset;
            offset += sample_sizes[sample_idx];
        }
    }
    if (sample_idx != n) {
        return false;
    }

    out.resize(n);
    std::vector<int64_t> pts_ticks(n);
    int gop_index = -1;
    int gop_start = 0;
    for (size_t i = 0; i < n; ++i) {
        const bool is_key = i == 0 || stss.empty() || stss.count(static_cast<uint32_t>(i)) > 0;
        if (is_key) {
            ++gop_index;
            gop_start = static_cast<int>(i);
        }
        pts_ticks[i] = static_cast<int64_t>(dts[i]) + cto[i];
        out[i].dts_ms = static_cast<TimeMs>(dts[i] * 1000 / timescale);
        out[i].file_offset = file_offsets[i];
        out[i].byte_size = sample_sizes[i];
        out[i].gop_index = gop_index;
        out[i].gop_start_idx = gop_start;
        out[i].frame_in_gop = static_cast<int>(i) - gop_start;
    }

    const int64_t first_pts_ticks = *std::min_element(pts_ticks.begin(), pts_ticks.end());
    for (size_t i = 0; i < n; ++i) {
        out[i].start_ms = static_cast<TimeMs>((pts_ticks[i] - first_pts_ticks) * 1000 / timescale);
    }

    display_order.resize(n);
    for (size_t i = 0; i < n; ++i) {
        display_order[i] = static_cast<int>(i);
    }
    std::stable_sort(display_order.begin(), display_order.end(), [&](int a, int b) {
        if (out[a].start_ms != out[b].start_ms) {
            return out[a].start_ms < out[b].start_ms;
        }
        return out[a].dts_ms < out[b].dts_ms;
    });

    const TimeMs default_delta_ms = static_cast<TimeMs>(stts.back().delta * 1000 / timescale);
    for (size_t pos = 0; pos < n; ++pos) {
        const int idx = display_order[pos];
        out[idx].display_index = static_cast<int>(pos);
        out[idx].end_ms = (pos + 1 < n) ? out[display_order[pos + 1]].start_ms : out[idx].start_ms + default_delta_ms;
    }

    size_t gop_begin = 0;
    while (gop_begin < n) {
        size_t gop_end = gop_begin + 1;
        while (gop_end < n && out[gop_end].gop_start_idx == static_cast<int>(gop_begin)) {
            ++gop_end;
        }
        int64_t bytes = 0;
        for (size_t i = gop_begin; i < gop_end; ++i) {
            bytes += out[i].byte_size;
        }
        const int count = static_cast<int>(gop_end - gop_begin);
        for (size_t i = gop_begin; i < gop_end; ++i) {
            out[i].gop_size = bytes;
            out[i].gop_frame_count = count;
        }
        gop_begin = gop_end;
    }
    return true;
}

static void parse_stbl(const uint8_t *buf,
                       size_t sz,
                       uint32_t timescale,
                       std::vector<uint8_t> &codec_config,
                       std::vector<FrameInfo> &frames,
                       std::vector<int> &display_order) {
    std::vector<SttsEntry> stts;
    std::vector<CttsEntry> ctts;
    std::set<uint32_t> stss;
    std::vector<StscEntry> stsc;
    std::vector<int64_t> chunk_offsets;
    std::vector<int32_t> sample_sizes;

    iter_boxes(buf, sz, [&](const char *type, const uint8_t *p, size_t ps) {
        if (!std::strcmp(type, "stts")) {
            stts = parse_stts(p, ps);
        } else if (!std::strcmp(type, "ctts")) {
            ctts = parse_ctts(p, ps);
        } else if (!std::strcmp(type, "stss")) {
            stss = parse_stss(p, ps);
        } else if (!std::strcmp(type, "stsc")) {
            stsc = parse_stsc(p, ps);
        } else if (!std::strcmp(type, "stco")) {
            chunk_offsets = parse_stco(p, ps);
        } else if (!std::strcmp(type, "co64")) {
            chunk_offsets = parse_co64(p, ps);
        } else if (!std::strcmp(type, "stsz")) {
            sample_sizes = parse_stsz(p, ps);
        } else if (!std::strcmp(type, "stsd") && ps >= 8) {
            iter_boxes(p + 8, ps - 8, [&](const char *entry_type, const uint8_t *entry, size_t entry_size) {
                constexpr size_t kVisualSampleEntryHeader = 78;
                const bool is_avc = !std::strcmp(entry_type, "avc1") || !std::strcmp(entry_type, "avc3");
                const bool is_hvc = !std::strcmp(entry_type, "hvc1") || !std::strcmp(entry_type, "hev1");
                if ((!is_avc && !is_hvc) || entry_size < kVisualSampleEntryHeader) {
                    return true;
                }
                const char *config_box = is_avc ? "avcC" : "hvcC";
                iter_boxes(entry + kVisualSampleEntryHeader, entry_size - kVisualSampleEntryHeader,
                           [&](const char *config_type, const uint8_t *config, size_t config_size) {
                               if (!std::strcmp(config_type, config_box)) {
                                   codec_config.assign(config, config + config_size);
                               }
                               return true;
                           });
                return true;
            });
        }
        return true;
    });

    build_frames(timescale, stts, ctts, stss, stsc, chunk_offsets, sample_sizes, frames, display_order);
}

static bool parse_trak(const uint8_t *buf, size_t sz, MoovHelper::Impl &impl) {
    int width = 0;
    int height = 0;
    uint32_t timescale = 0;
    TimeMs duration_ms = 0;
    bool is_video = false;
    const uint8_t *minf_buf = nullptr;
    size_t minf_size = 0;

    iter_boxes(buf, sz, [&](const char *type, const uint8_t *p, size_t ps) {
        if (!std::strcmp(type, "tkhd") && ps >= 1) {
            const size_t wh_off = (p[0] == 1) ? (4 + 32 + 52) : (4 + 20 + 52);
            if (ps >= wh_off + 8) {
                width = static_cast<int>(u32be(p + wh_off) >> 16);
                height = static_cast<int>(u32be(p + wh_off + 4) >> 16);
            }
        } else if (!std::strcmp(type, "mdia")) {
            iter_boxes(p, ps, [&](const char *mdia_type, const uint8_t *mp, size_t mps) {
                if (!std::strcmp(mdia_type, "mdhd") && mps >= 1) {
                    if (mp[0] == 1) {
                        if (mps >= 32) {
                            timescale = u32be(mp + 20);
                            if (timescale) {
                                duration_ms = static_cast<TimeMs>(u64be(mp + 24) * 1000 / timescale);
                            }
                        }
                    } else if (mps >= 20) {
                        timescale = u32be(mp + 12);
                        if (timescale) {
                            duration_ms = static_cast<TimeMs>(uint64_t(u32be(mp + 16)) * 1000 / timescale);
                        }
                    }
                } else if (!std::strcmp(mdia_type, "hdlr") && mps >= 12) {
                    char handler[5] = {};
                    std::memcpy(handler, mp + 8, 4);
                    is_video = std::strcmp(handler, "vide") == 0;
                } else if (!std::strcmp(mdia_type, "minf")) {
                    minf_buf = mp;
                    minf_size = mps;
                }
                return true;
            });
        }
        return true;
    });

    if (!is_video || timescale == 0 || !minf_buf) {
        return false;
    }

    std::vector<FrameInfo> frames;
    std::vector<int> display_order;
    std::vector<uint8_t> codec_config;
    iter_boxes(minf_buf, minf_size, [&](const char *type, const uint8_t *p, size_t ps) {
        if (!std::strcmp(type, "stbl")) {
            parse_stbl(p, ps, timescale, codec_config, frames, display_order);
        }
        return true;
    });

    if (frames.empty() || display_order.empty()) {
        return false;
    }

    impl.frames = std::move(frames);
    impl.display_order = std::move(display_order);
    impl.codec_config = std::move(codec_config);
    impl.width = width;
    impl.height = height;
    impl.duration_ms = duration_ms;
    impl.frame_rate = duration_ms > 0 ? impl.frames.size() * 1000.0 / static_cast<double>(duration_ms) : 0.0;
    impl.has_video = true;
    impl.last_index = 0;
    return true;
}

static bool parse_moov(const uint8_t *buf, size_t sz, MoovHelper::Impl &impl) {
    BoxHeader hdr;
    if (!read_box_header(buf, sz, hdr) || std::strcmp(hdr.type, "moov") != 0) {
        return false;
    }

    bool found = false;
    iter_boxes(buf + hdr.header_size, sz - hdr.header_size, [&](const char *type, const uint8_t *p, size_t ps) {
        if (!std::strcmp(type, "trak")) {
            found = parse_trak(p, ps, impl);
            return !found;
        }
        return true;
    });
    return found;
}

static bool read_exact(std::ifstream &file, int64_t offset, int64_t size, std::vector<uint8_t> &out) {
    if (offset < 0 || size <= 0) {
        return false;
    }
    out.resize(static_cast<size_t>(size));
    file.seekg(offset);
    file.read(reinterpret_cast<char *>(out.data()), size);
    return file.good() || file.gcount() == size;
}

static bool load_moov_from_file(const std::string &path, MoovHelper::Impl &impl) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const int64_t file_size = static_cast<int64_t>(file.tellg());
    file.seekg(0);

    int64_t pos = 0;
    while (pos + 8 <= file_size) {
        std::vector<uint8_t> header;
        const int64_t header_read = std::min<int64_t>(16, file_size - pos);
        if (!read_exact(file, pos, header_read, header) || header.size() < 8) {
            return false;
        }

        BoxHeader hdr;
        if (!read_box_header(header.data(), header.size(), hdr)) {
            return false;
        }
        if (hdr.total_size == 0 || pos + static_cast<int64_t>(hdr.total_size) > file_size) {
            return false;
        }

        if (!std::strcmp(hdr.type, "moov")) {
            std::vector<uint8_t> moov;
            if (!read_exact(file, pos, static_cast<int64_t>(hdr.total_size), moov)) {
                return false;
            }
            return parse_moov(moov.data(), moov.size(), impl);
        }
        pos += static_cast<int64_t>(hdr.total_size);
    }
    return false;
}

MoovHelper::MoovHelper() :
    impl_(std::make_unique<Impl>()) {
}

MoovHelper::~MoovHelper() = default;

bool MoovHelper::load(const std::string &path) {
    impl_ = std::make_unique<Impl>();
    return load_moov_from_file(path, *impl_);
}

bool MoovHelper::hasVideoTrack() const {
    return impl_->has_video;
}

int MoovHelper::width() const {
    return impl_->width;
}

int MoovHelper::height() const {
    return impl_->height;
}

TimeMs MoovHelper::durationMs() const {
    return impl_->duration_ms;
}

double MoovHelper::frameRate() const {
    return impl_->frame_rate;
}

const std::vector<uint8_t> &MoovHelper::codecConfig() const {
    return impl_->codec_config;
}

bool MoovHelper::queryFrame(TimeMs time_ms, FrameLocation &out) const {
    const auto &frames = impl_->frames;
    const auto &display_order = impl_->display_order;
    if (frames.empty() || display_order.empty()) {
        return false;
    }

    const int n = static_cast<int>(frames.size());
    const int last_display_sample = display_order.back();
    if (time_ms >= frames[last_display_sample].end_ms) {
        return false;
    }

    const int last = std::min(std::max(impl_->last_index, 0), n - 1);
    const bool back = time_ms < frames[display_order[last]].start_ms;
    const int lo = back ? std::max(0, last - 5) : last;
    const int hi = back ? last : std::min(n - 1, last + 5);

    int found = -1;
    for (int i = back ? hi : lo; lo <= i && i <= hi; i += back ? -1 : 1) {
        const FrameInfo &frame = frames[display_order[i]];
        if (frame.start_ms <= time_ms && time_ms < frame.end_ms) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        int l = 0;
        int r = n - 1;
        while (l < r) {
            const int m = (l + r) / 2;
            if (frames[display_order[m]].end_ms <= time_ms) {
                l = m + 1;
            } else {
                r = m;
            }
        }
        const FrameInfo &frame = frames[display_order[l]];
        if (frame.start_ms <= time_ms && time_ms < frame.end_ms) {
            found = l;
        }
    }
    if (found < 0) {
        return false;
    }

    impl_->last_index = found;
    const int sample_idx = display_order[found];
    const FrameInfo &frame = frames[sample_idx];
    const FrameInfo &gop_start = frames[frame.gop_start_idx];

    out.pts_ms = frame.start_ms;
    out.dts_ms = frame.dts_ms;
    out.start_ms = frame.start_ms;
    out.end_ms = frame.end_ms;
    out.gop_pts_ms = gop_start.start_ms;
    out.sample_index = sample_idx;
    out.display_index = frame.display_index;
    out.gop_index = frame.gop_index;
    out.gop_offset = gop_start.file_offset;
    out.gop_size = frame.gop_size;
    out.sample_offset = frame.file_offset;
    out.sample_size = frame.byte_size;
    out.frame_in_gop = frame.frame_in_gop;
    out.gop_frame_count = frame.gop_frame_count;
    out.total_frames = n;
    return true;
}

std::vector<FrameLocation> MoovHelper::getAllFrameLocations() const {
    const int n = static_cast<int>(impl_->frames.size());
    std::vector<FrameLocation> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        FrameLocation loc;
        const FrameInfo &frame = impl_->frames[i];
        const FrameInfo &gop_start = impl_->frames[frame.gop_start_idx];
        loc.pts_ms = frame.start_ms;
        loc.dts_ms = frame.dts_ms;
        loc.start_ms = frame.start_ms;
        loc.end_ms = frame.end_ms;
        loc.gop_pts_ms = gop_start.start_ms;
        loc.sample_index = i;
        loc.display_index = frame.display_index;
        loc.gop_index = frame.gop_index;
        loc.gop_offset = gop_start.file_offset;
        loc.gop_size = frame.gop_size;
        loc.sample_offset = frame.file_offset;
        loc.sample_size = frame.byte_size;
        loc.frame_in_gop = frame.frame_in_gop;
        loc.gop_frame_count = frame.gop_frame_count;
        loc.total_frames = n;
        out.push_back(loc);
    }
    return out;
}

} // namespace vp