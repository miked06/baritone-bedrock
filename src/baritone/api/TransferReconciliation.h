#pragma once
namespace baritone {
// A server response can update either model before the player's next tick.
inline bool transferSideCompatible(bool empty, bool sameItem, int count, int before, int after) {
    return empty ? before == 0 || after == 0 : sameItem && (count == before || count == after);
}
inline bool transferTargetReceiptValid(unsigned networkName, int expected, int received) {
    const bool consumableSlot=networkName==24 || networkName==25 || networkName==45 || networkName==46;
    return consumableSlot ? received>=0 && received<=expected : received==expected;
}
}
