#include "Library.h"
#include "../core/kit/DrumRack.h"

namespace drumchimp
{
    Library::Library() : juce::Thread ("DrumChimp library scan")
    {
        if (const auto json = juce::JSON::parse (settingsFile().loadFileAsString()); auto* folders = json["extraFolders"].getArray())
            for (const auto& f : *folders)
                extras.addIfNotAlreadyThere (f.toString());
        lastStatus.scanning = true;
        startThread (juce::Thread::Priority::low);
    }

    Library::~Library()
    {
        cancel = true;
        stopThread (10000);
    }

    juce::File Library::cacheFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support/DrumChimp/library-cache.tsv");
    }

    // Drum Rack presets in the Core Library's Racks/Drum Racks and the User Library's Presets
    // (Packs' racks use Packs' samples, which are protected). A pad is kept when its sample is in
    // the index: below the Core Library or the User Library by its relative path, or at its
    // absolute path; a rack is kept when it has a kick and a snare DrumChimp can play.
    std::vector<DrumRack> Library::findRacks (const LibraryIndex& index, const std::vector<LibraryRoot>& roots)
    {
        std::vector<juce::File> folders, bases;
        for (const auto& r : roots)
        {
            if (r.kind == LibraryRoot::Kind::coreLibrary)
                folders.push_back (juce::File (r.path).getChildFile ("Racks/Drum Racks"));
            if (r.kind == LibraryRoot::Kind::userLibrary)
                folders.push_back (juce::File (r.path).getChildFile ("Presets"));
            if (r.kind == LibraryRoot::Kind::coreLibrary || r.kind == LibraryRoot::Kind::userLibrary)
                bases.push_back (juce::File (r.path));
        }

        std::vector<DrumRack> racks;
        for (const auto& folder : folders)
            for (const auto& file : folder.findChildFiles (juce::File::findFiles, true, "*.adg"))
            {
                if (threadShouldExit())
                    return {};
                juce::FileInputStream raw (file);
                if (! raw.openedOk())
                    continue;
                juce::GZIPDecompressorInputStream unzipped (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
                const auto xml = unzipped.readEntireStreamAsString().toStdString();
                if (xml.find ("<DrumGroupDevice") == std::string::npos)
                    continue;
                DrumRack rack { file.getFileNameWithoutExtension().toStdString(), file.getParentDirectory().getFileName().toStdString(),
                                file.getFullPathName().toStdString(), {} };
                bool kick = false, snare = false;
                for (auto pad : parseDrumRack (xml))
                {
                    const SampleInfo* found = nullptr;
                    for (const auto& base : bases)
                        if (found == nullptr)
                            found = index.find (base.getChildFile (pad.relativePath).getFullPathName().toStdString());
                    if (found == nullptr && ! pad.path.empty())
                        found = index.find (pad.path);
                    if (found == nullptr)
                        continue;
                    pad.resolved = found->path;
                    kick = kick || found->classification.role == Role::kick;
                    snare = snare || found->classification.role == Role::snare;
                    rack.pads.push_back (pad);
                }
                if (kick && snare)
                    racks.push_back (std::move (rack));
            }
        std::sort (racks.begin(), racks.end(), [] (const DrumRack& a, const DrumRack& b) { return a.group != b.group ? a.group < b.group : a.name < b.name; });
        return racks;
    }

    std::vector<LibraryRoot> Library::liveRoots()
    {
        if (juce::SystemStats::getEnvironmentVariable ("DRUMCHIMP_LIBRARY", {}) == "none")
            return {};
        return detectRoots (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString());
    }

    juce::File Library::settingsFile()
    {
        return cacheFile().getSiblingFile ("settings.json");
    }

    juce::StringArray Library::extraFolders() const
    {
        const juce::ScopedLock sl (lock);
        return extras;
    }

    void Library::addFolder (const juce::File& folder)
    {
        {
            const juce::ScopedLock sl (lock);
            if (! folder.isDirectory() || extras.contains (folder.getFullPathName()))
                return;
            extras.add (folder.getFullPathName());
        }
        juce::DynamicObject::Ptr root (new juce::DynamicObject());
        root->setProperty ("extraFolders", juce::var (extraFolders()));
        settingsFile().getParentDirectory().createDirectory();
        settingsFile().replaceWithText (juce::JSON::toString (juce::var (root.get())));
        rescan();
    }

    void Library::removeFolder (const juce::String& path)
    {
        {
            const juce::ScopedLock sl (lock);
            if (! extras.contains (path))
                return;
            extras.removeString (path);
        }
        juce::DynamicObject::Ptr root (new juce::DynamicObject());
        root->setProperty ("extraFolders", juce::var (extraFolders()));
        settingsFile().replaceWithText (juce::JSON::toString (juce::var (root.get())));
        rescan();
    }

    std::shared_ptr<const LibraryIndex> Library::index() const
    {
        const juce::ScopedLock sl (lock);
        return current;
    }

    Library::Status Library::status() const
    {
        const juce::ScopedLock sl (lock);
        return lastStatus;
    }

    void Library::rescan()
    {
        {
            const juce::ScopedLock sl (lock);
            lastStatus.scanning = true;
        }
        again = true;
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    void Library::run()
    {
        do
        {
            again = false;
            auto roots = liveRoots();
            for (const auto& path : extraFolders())
                if (std::none_of (roots.begin(), roots.end(), [&] (const LibraryRoot& r) { return r.path == path.toStdString(); })
                    && juce::File (path).isDirectory())
                    roots.push_back ({ LibraryRoot::Kind::userFolder, juce::File (path).getFileName().toStdString(), path.toStdString() });
            LibraryCache cache;
            const auto cachePath = cacheFile().getFullPathName().toStdString();
            cache.read (cachePath);
            ScanStats stats;
            auto samples = scanLibrary (roots, cache, stats, &cancel);
            if (stats.cancelled || threadShouldExit())
                return;
            cache.write (cachePath);

            auto building = std::make_shared<LibraryIndex> (std::move (samples), roots);
            building->setRacks (std::make_shared<const std::vector<DrumRack>> (findRacks (*building, roots)));
            std::shared_ptr<const LibraryIndex> index = building;
            {
                const juce::ScopedLock sl (lock);
                lastStatus = { false, index->size(), index->drums(), roots.size(), stats.protectedFiles, stats.milliseconds, index->racks().size() };
                current = std::move (index);
            }
            ++versions;
        }
        while (again && ! threadShouldExit());
    }
}
