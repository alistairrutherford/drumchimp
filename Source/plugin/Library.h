// The sample library inside the plug-in (PLAN.md §3.2, G2): one background scan shared by every
// DrumChimp in the process (hold it with juce::SharedResourcePointer<Library>). It finds the roots
// (LibraryRoots.h), scans and classifies them (LibraryIndex.h), and keeps the header cache in
// ~/Library/Application Support/DrumChimp/library-cache.tsv, so later starts read almost no files.
//
//   index()    the latest finished index (null until the first scan ends); any thread
//   status()   for the editor
//   rescan()   scan again (Phase 3's Settings)
#pragma once

#include <juce_core/juce_core.h>
#include "../core/library/LibraryIndex.h"

namespace drumchimp
{
    class Library : private juce::Thread
    {
    public:
        Library();
        ~Library() override;

        struct Status
        {
            bool scanning = false;
            size_t samples = 0, drums = 0, roots = 0;
            double milliseconds = 0.0;
        };

        std::shared_ptr<const LibraryIndex> index() const;
        Status status() const;
        juce::uint32 version() const noexcept { return versions.load(); }   // changes with each finished scan
        void rescan();

        static juce::File cacheFile();

    private:
        void run() override;

        mutable juce::CriticalSection lock;
        std::shared_ptr<const LibraryIndex> current;
        Status lastStatus;
        std::atomic<bool> cancel { false }, again { false };
        std::atomic<juce::uint32> versions { 0 };
    };
}
