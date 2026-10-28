#include "Library.h"

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

            auto index = std::make_shared<const LibraryIndex> (std::move (samples), roots);
            {
                const juce::ScopedLock sl (lock);
                lastStatus = { false, index->size(), index->drums(), roots.size(), stats.protectedFiles, stats.milliseconds };
                current = std::move (index);
            }
            ++versions;
        }
        while (again && ! threadShouldExit());
    }
}
