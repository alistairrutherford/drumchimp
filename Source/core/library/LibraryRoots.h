// Finds Ableton's sample library on disk (PLAN.md §2, "Where the samples are"). Plain C++
// (std::filesystem), no JUCE. Message or background thread only.
//
//   Core Library   inside the Live 12 app bundle (/Applications or ~/Applications; Suite is
//                  preferred over Standard over the rest when more than one is installed)
//   Packs, User Library and user folders ("Places")
//                  read from the newest Live 12 Library.cfg in ~/Library/Preferences/Ableton,
//                  which lists every installed pack wherever Live put it
//
//   parseLibraryCfg   Library.cfg text → roots (for the tests)
//   detectRoots       the roots on this Mac that exist
#pragma once

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace drumchimp
{
    struct LibraryRoot
    {
        enum class Kind { coreLibrary, pack, userLibrary, userFolder };
        Kind kind;
        std::string name;
        std::string path;
    };

    inline const char* kindName (LibraryRoot::Kind k)
    {
        switch (k)
        {
            case LibraryRoot::Kind::coreLibrary: return "Core Library";
            case LibraryRoot::Kind::pack:        return "Pack";
            case LibraryRoot::Kind::userLibrary: return "User Library";
            case LibraryRoot::Kind::userFolder:  return "User folder";
        }
        return "";
    }

    namespace libraryroots
    {
        inline std::string unescapeXml (std::string s)
        {
            static const std::pair<const char*, const char*> entities[] {
                { "&quot;", "\"" }, { "&apos;", "'" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&amp;", "&" } };
            for (const auto& [from, to] : entities)
                for (size_t at = s.find (from); at != std::string::npos; at = s.find (from, at + 1))
                    s.replace (at, std::strlen (from), to);
            return s;
        }

        // The value of `attribute="…"` in the element starting at `from`, or "".
        inline std::string attributeIn (const std::string& text, size_t from, const std::string& attribute)
        {
            const auto end = text.find ('>', from);
            const auto at = text.find (" " + attribute + "=\"", from);
            if (at == std::string::npos || at > end)
                return {};
            const auto start = at + attribute.size() + 3;
            const auto close = text.find ('"', start);
            return close == std::string::npos ? std::string() : unescapeXml (text.substr (start, close - start));
        }

        inline std::vector<int> versionParts (const std::string& folderName)
        {
            std::vector<int> parts;
            int value = -1;
            for (char c : folderName)
            {
                if (c >= '0' && c <= '9')
                    value = (value < 0 ? 0 : value * 10) + (c - '0');
                else if (value >= 0)
                {
                    parts.push_back (value);
                    value = -1;
                }
            }
            if (value >= 0)
                parts.push_back (value);
            return parts;
        }

        inline int editionRank (const std::string& appName)
        {
            if (appName.find ("Suite") != std::string::npos) return 3;
            if (appName.find ("Standard") != std::string::npos) return 2;
            if (appName.find ("Beta") != std::string::npos) return 0;
            return 1;
        }
    }

    inline std::vector<LibraryRoot> parseLibraryCfg (const std::string& text)
    {
        using namespace libraryroots;
        std::vector<LibraryRoot> roots;

        // <UserLibrary> … <ProjectPath Value="~/Music/Ableton" /> → that folder's "User Library".
        if (const auto user = text.find ("<UserLibrary>"); user != std::string::npos)
            if (const auto pp = text.find ("<ProjectPath ", user); pp != std::string::npos)
                if (const auto base = attributeIn (text, pp, "Value"); ! base.empty())
                    roots.push_back ({ LibraryRoot::Kind::userLibrary, "User Library", base + "/User Library" });

        const auto collect = [&] (const std::string& element, LibraryRoot::Kind kind)
        {
            for (size_t at = text.find ("<" + element + " "); at != std::string::npos; at = text.find ("<" + element + " ", at + 1))
            {
                auto path = attributeIn (text, at, "Path");
                while (path.size() > 1 && path.back() == '/')
                    path.pop_back();
                auto name = attributeIn (text, at, "DisplayName");
                if (! path.empty())
                    roots.push_back ({ kind, name.empty() ? std::filesystem::path (path).filename().string() : name, path });
            }
        };
        collect ("LibrarySliceInfo", LibraryRoot::Kind::pack);
        collect ("UserFolderInfo", LibraryRoot::Kind::userFolder);
        return roots;
    }

    inline std::vector<LibraryRoot> detectRoots (const std::string& home)
    {
        namespace fs = std::filesystem;
        using namespace libraryroots;
        std::vector<LibraryRoot> roots;
        std::error_code ec;

        // Core Library: the best-ranked Live 12 app.
        std::string bestApp;
        for (const auto& dir : { std::string ("/Applications"), home + "/Applications" })
            for (const auto& e : fs::directory_iterator (dir, fs::directory_options::skip_permission_denied, ec))
            {
                const auto name = e.path().filename().string();
                if (name.rfind ("Ableton Live 12", 0) == 0 && e.path().extension() == ".app"
                    && fs::is_directory (e.path() / "Contents/App-Resources/Core Library", ec)
                    && (bestApp.empty() || editionRank (name) > editionRank (fs::path (bestApp).filename().string())))
                    bestApp = e.path().string();
            }
        if (! bestApp.empty())
            roots.push_back ({ LibraryRoot::Kind::coreLibrary, "Core Library", bestApp + "/Contents/App-Resources/Core Library" });

        // The newest Live 12 preferences folder that has a Library.cfg.
        fs::path bestPrefs;
        std::vector<int> bestVersion;
        for (const auto& e : fs::directory_iterator (home + "/Library/Preferences/Ableton", fs::directory_options::skip_permission_denied, ec))
        {
            const auto name = e.path().filename().string();
            const auto version = versionParts (name);
            if (name.rfind ("Live 12", 0) == 0 && fs::exists (e.path() / "Library.cfg", ec) && version > bestVersion)
            {
                bestVersion = version;
                bestPrefs = e.path();
            }
        }
        if (! bestPrefs.empty())
        {
            std::ifstream in (bestPrefs / "Library.cfg");
            std::stringstream text;
            text << in.rdbuf();
            for (auto& r : parseLibraryCfg (text.str()))
                roots.push_back (std::move (r));
        }
        else if (fs::is_directory (home + "/Music/Ableton/User Library", ec))
        {
            roots.push_back ({ LibraryRoot::Kind::userLibrary, "User Library", home + "/Music/Ableton/User Library" });
        }

        roots.erase (std::remove_if (roots.begin(), roots.end(), [&] (const LibraryRoot& r) { return ! fs::is_directory (r.path, ec); }),
                     roots.end());
        return roots;
    }
}
