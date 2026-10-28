// Hands an object from the message thread to the audio thread whole, without locks or allocation
// on the audio thread, and frees replaced ones on the message thread. (DrumEngine's kit banks do
// the same, with the extra wait for voices; this is the plain case, used for the pattern.)
//
//   publish (object)   message thread: the next object. One published earlier and not yet taken
//                      is dropped.
//   current()          audio thread: the latest object (null until the first). Taking a new one
//                      hands the old one back, once the previous old one has been collected.
//   collectGarbage()   message thread: frees what the audio thread handed back. Call regularly.
#pragma once

#include <atomic>
#include <memory>

namespace drumchimp
{
    template <typename T>
    class Handoff
    {
    public:
        Handoff() = default;
        Handoff (const Handoff&) = delete;
        Handoff& operator= (const Handoff&) = delete;

        ~Handoff()
        {
            delete incoming.exchange (nullptr);
            delete outgoing.exchange (nullptr);
            delete active;
        }

        void publish (std::unique_ptr<T> object) { delete incoming.exchange (object.release()); }
        void collectGarbage() { delete outgoing.exchange (nullptr); }

        const T* current() noexcept
        {
            if (outgoing.load (std::memory_order_acquire) == nullptr)
                if (auto* next = incoming.exchange (nullptr))
                {
                    outgoing.store (active, std::memory_order_release);
                    active = next;
                }
            return active;
        }

    private:
        std::atomic<T*> incoming { nullptr }, outgoing { nullptr };
        T* active = nullptr;              // audio thread only
    };
}
