#ifndef __CPU_PRED_WHISPER_HH__
#define __CPU_PRED_WHISPER_HH__

#include <bitset>
#include <list>
#include <map>

#include "base/types.hh"
#include "cpu/pred/bpred_unit.hh"
#include "params/WhisperBP.hh"

namespace gem5::branch_prediction
{

class WhisperBP : public BPredUnit
{
  public:
    WhisperBP(const WhisperBPParams &params);

    bool lookup(ThreadID tid, Addr pc, void * &bp_history) override;

    void updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target,  void * &bp_history) override;

    void update(ThreadID tid, Addr pc, bool taken,
                void * &bp_history, bool squashed,
                const StaticInstPtr & inst, Addr target) override;

    void squash(ThreadID tid, void * &bp_history) override;

    void insert(Addr pc, uint32_t hint);

  private:
    struct HintBufferEntry
    {
        Addr addr;
        uint32_t hint;

        bool operator==(const HintBufferEntry& other) const noexcept
        {
            return addr == other.addr;
        }
    };

    struct Hint
    {
        uint8_t history : 4;
        uint16_t bool_formula : 15;
        uint8_t bias : 2;
        uint16_t pc_offset : 12;

        /**
         * Constructs a \c Hint object from a 32-bit hint integer.
         */
        static Hint fromUInt(uint32_t hint);

        [[nodiscard]] std::bitset<4> history_bits() const;
        [[nodiscard]] std::bitset<15> bool_formula_bits() const;
        [[nodiscard]] std::bitset<2> bias_bits() const;
        [[nodiscard]] std::bitset<12> pc_offset_bits() const;

        /**
         * @return The branch history length encoded by this \c Hint.
         */
        [[nodiscard]] unsigned histLength() const;
    };

    /**
     * Looks up whether the hint buffer contains an entry with the given PC.
     *
     * @param pc Program Counter value of branch.
     * @return An iterator to the \c HintBufferEntry if found.
     */
    std::list<HintBufferEntry>::iterator lookupBuffer(Addr pc);

    /**
     * Marks the entry pointed by \c it as used.
     *
     * @param it The iterator pointing to the used entry.
     */
    void markUsed(std::list<HintBufferEntry>::const_iterator it);

    /**
     * Updates the global history for this thread.
     *
     * @param tid Thread ID.
     * @param taken Whether the branch is taken.
     */
    void updateGlobalHistory(ThreadID tid, bool taken);

    unsigned hintBufferSize;

    /**
     * Storage for hint buffer, sorted in LRU (i.e. LRU entry at the front, MRU
     * entry at the back).
     */
    std::list<HintBufferEntry> hintBuffer;
    std::map<ThreadID, std::bitset<1024>> globalHistory;
    BPredUnit *fallbackPredictor;
};

}  // namespace gem5::branch_prediction

#endif // __CPU_PRED_WHISPER_HH__
