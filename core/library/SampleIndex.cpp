// SPDX-License-Identifier: AGPL-3.0-only
// LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
#include "core/library/SampleIndex.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <thread>

namespace klay::library
{

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{
    constexpr int kIndexVersion = 1;

    constexpr std::array<model::Role, 6> kRoleFolders{ model::Role::kick,  model::Role::rumble,    model::Role::drive,
                                                       model::Role::crunch, model::Role::transient, model::Role::texture };

    std::string lower(std::string_view s)
    {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool isAudioFile(const fs::path& path)
    {
        const auto ext = lower(toUtf8(path.extension()));
        return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
    }

    std::optional<model::Role> roleFromString(const std::string& s)
    {
        for (const auto role : kRoleFolders)
            if (s == model::toString(role))
                return role;
        if (s == model::toString(model::Role::any))
            return model::Role::any;
        return std::nullopt;
    }
} // namespace

std::string toUtf8(const fs::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

fs::path fromUtf8(const std::string& utf8)
{
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::optional<model::Role> roleFromFolderName(std::string_view name)
{
    const auto l = lower(name);
    for (const auto role : kRoleFolders)
        if (l == model::toString(role))
            return role;
    return std::nullopt;
}

std::vector<FoundFile> scanLibrary(const fs::path& root)
{
    std::vector<FoundFile> found;
    std::error_code ec;
    if (! fs::is_directory(root, ec))
        return found;

    for (const auto& top : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec))
    {
        if (! top.is_directory(ec))
            continue;
        const auto role = roleFromFolderName(toUtf8(top.path().filename()));
        if (! role)
            continue;
        for (auto it = fs::recursive_directory_iterator(top.path(), fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            const auto& entry = *it;
            if (! entry.is_regular_file(ec) || ! isAudioFile(entry.path()))
                continue;
            FoundFile f;
            f.path = entry.path();
            f.role = *role;
            const auto parent = entry.path().parent_path().lexically_relative(top.path());
            f.folder = parent == "." ? std::string() : toUtf8(parent.generic_u8string());
            f.size = entry.file_size(ec);
            f.mtime = static_cast<std::int64_t>(entry.last_write_time(ec).time_since_epoch().count());
            found.push_back(std::move(f));
        }
    }
    std::sort(found.begin(), found.end(), [](const FoundFile& a, const FoundFile& b) { return a.path < b.path; });
    return found;
}

SampleIndex buildIndex(const fs::path& root, const Analyzer& analyzer, const SampleIndex* previous, BuildStats* stats,
                       unsigned threads, const std::atomic<bool>* cancel)
{
    BuildStats local;
    auto& st = stats ? *stats : local;
    st = {};

    SampleIndex index;
    index.root = toUtf8(root);
    const auto files = scanLibrary(root);

    std::map<std::string, const SampleInfo*> known;
    if (previous && previous->root == index.root)
        for (const auto& s : previous->samples)
            known[s.path] = &s;

    std::vector<SampleInfo> entries(files.size());
    std::vector<size_t> toAnalyse;
    for (size_t i = 0; i < files.size(); ++i)
    {
        auto& e = entries[i];
        e.path = toUtf8(files[i].path);
        e.role = files[i].role;
        e.folder = files[i].folder;
        e.mtime = files[i].mtime;
        e.size = files[i].size;
        if (const auto it = known.find(e.path); it != known.end() && it->second->mtime == e.mtime && it->second->size == e.size)
        {
            e.meta = it->second->meta;
            ++st.reused;
            known.erase(it);
        }
        else
        {
            if (it != known.end())
                known.erase(it); // changed file: analysed again, not counted as removed
            toAnalyse.push_back(i);
        }
    }
    st.removed = static_cast<int>(known.size());

    // Analyse new and changed files in parallel; each worker takes the next file. state: 0 still to analyse,
    // 1 analysed, 2 reused; a cancelled build keeps only states 1 and 2.
    std::vector<char> state(entries.size(), 2);
    for (const auto i : toAnalyse)
        state[i] = 0;
    std::atomic<size_t> next{ 0 };
    auto work = [&] {
        for (size_t k = next.fetch_add(1); k < toAnalyse.size(); k = next.fetch_add(1))
        {
            if (cancel && cancel->load(std::memory_order_relaxed))
                return;
            auto& e = entries[toAnalyse[k]];
            e.meta = analyzer ? analyzer(e.path) : Analysis{};
            state[toAnalyse[k]] = 1;
        }
    };
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const unsigned n = static_cast<unsigned>(std::min<size_t>(threads ? threads : hw, std::max<size_t>(1, toAnalyse.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < n; ++t)
        pool.emplace_back(work);
    work();
    for (auto& t : pool)
        t.join();

    st.cancelled = cancel && cancel->load(std::memory_order_relaxed);

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (state[i] == 0)
            continue;
        if (state[i] == 1)
        {
            if (! entries[i].meta.ok)
            {
                ++st.failed;
                continue;
            }
            ++st.analysed;
        }
        index.samples.push_back(std::move(entries[i]));
    }
    return index;
}

std::string indexToJson(const SampleIndex& index)
{
    json samples = json::array();
    for (const auto& s : index.samples)
        samples.push_back({ { "path", s.path },
                            { "role", model::toString(s.role) },
                            { "folder", s.folder },
                            { "mtime", s.mtime },
                            { "size", s.size },
                            { "lengthSeconds", s.meta.lengthSeconds },
                            { "peak", s.meta.peak },
                            { "rootDetected", s.meta.rootDetected },
                            { "rootNote", s.meta.rootNote },
                            { "rootHz", s.meta.rootHz },
                            { "rootUncertain", s.meta.rootUncertain } });
    const json root{ { "indexVersion", kIndexVersion }, { "root", index.root }, { "samples", samples } };
    return root.dump(1);
}

std::optional<SampleIndex> indexFromJson(std::string_view text)
{
    const auto root = json::parse(text, nullptr, false);
    if (root.is_discarded() || ! root.is_object() || root.value("indexVersion", 0) != kIndexVersion)
        return std::nullopt;
    SampleIndex index;
    index.root = root.value("root", std::string());
    const auto it = root.find("samples");
    if (it == root.end() || ! it->is_array())
        return std::nullopt;
    try
    {
        for (const auto& j : *it)
        {
            SampleInfo s;
            s.path = j.at("path").get<std::string>();
            const auto role = roleFromString(j.at("role").get<std::string>());
            if (! role)
                return std::nullopt;
            s.role = *role;
            s.folder = j.at("folder").get<std::string>();
            s.mtime = j.at("mtime").get<std::int64_t>();
            s.size = j.at("size").get<std::uint64_t>();
            s.meta.ok = true;
            s.meta.lengthSeconds = j.at("lengthSeconds").get<double>();
            s.meta.peak = j.at("peak").get<float>();
            s.meta.rootDetected = j.at("rootDetected").get<bool>();
            s.meta.rootNote = j.at("rootNote").get<int>();
            s.meta.rootHz = j.at("rootHz").get<double>();
            s.meta.rootUncertain = j.at("rootUncertain").get<bool>();
            index.samples.push_back(std::move(s));
        }
    }
    catch (const json::exception&)
    {
        return std::nullopt;
    }
    return index;
}

bool saveIndex(const fs::path& file, const SampleIndex& index)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    auto tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (! out)
            return false;
        out << indexToJson(index);
        if (! out)
            return false;
    }
    fs::rename(tmp, file, ec); // replaces atomically, so a reader never sees half a file
    return ! ec;
}

std::optional<SampleIndex> loadIndex(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (! in)
        return std::nullopt;
    std::stringstream buffer;
    buffer << in.rdbuf();
    return indexFromJson(buffer.str());
}

} // namespace klay::library
