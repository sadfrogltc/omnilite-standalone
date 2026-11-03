// omnilite-standalone: stands in for Litecoin Core's wallet/coincontrol.h: the
// inputs chosen for a transaction and where change goes.

#ifndef OMNILITE_STANDALONE_CORESHIM_WALLET_COINCONTROL_H
#define OMNILITE_STANDALONE_CORESHIM_WALLET_COINCONTROL_H

#include <primitives/transaction.h>
#include <script/standard.h>

#include <set>
#include <vector>

class CCoinControl
{
public:
    CTxDestination destChange = CNoDestination();
    //! If false, only selected inputs are used
    bool fAllowOtherInputs = false;

    bool HasSelected() const { return !setSelected.empty(); }
    bool IsSelected(const COutPoint& output) const { return setSelected.count(output) > 0; }
    void Select(const COutPoint& output) { setSelected.insert(output); }
    void UnSelect(const COutPoint& output) { setSelected.erase(output); }
    void UnSelectAll() { setSelected.clear(); }
    void ListSelected(std::vector<COutPoint>& vOutpoints) const { vOutpoints.assign(setSelected.begin(), setSelected.end()); }

private:
    std::set<COutPoint> setSelected;
};

#endif // OMNILITE_STANDALONE_CORESHIM_WALLET_COINCONTROL_H
