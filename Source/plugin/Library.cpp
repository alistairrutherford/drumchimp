#include "Library.h"

namespace drumchimp
{
    Library::Library() : juce::Thread ("DrumChimp library scan")
    {
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
            const auto roots = detectRoots (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString());
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
                lastStatus = { false, index->size(), index->drums(), roots.size(), stats.milliseconds };
                current = std::move (index);
            }
            ++versions;
        }
        while (again && ! threadShouldExit());
    }
}
