// omnilite-standalone: stands in for Litecoin Core's ui_interface.h. There is
// no GUI; OmniLite's notifications do nothing.

#ifndef OMNILITE_STANDALONE_CORESHIM_UI_INTERFACE_H
#define OMNILITE_STANDALONE_CORESHIM_UI_INTERFACE_H

#include <string>

class CClientUIInterface
{
public:
    void OmniStateChanged() {}
    void OmniPendingChanged(bool) {}
    void OmniBalanceChanged() {}
    void OmniStateInvalidated() {}
    void InitMessage(const std::string&) {}
    void ShowProgress(const std::string&, int, bool) {}
};

extern CClientUIInterface uiInterface;

#endif // OMNILITE_STANDALONE_CORESHIM_UI_INTERFACE_H
