// omnilite-standalone: stands in for Litecoin Core's clientversion.h, with the
// version of the Litecoin Core OmniLite 0.18.1 is built on.

#ifndef OMNILITE_STANDALONE_CORESHIM_CLIENTVERSION_H
#define OMNILITE_STANDALONE_CORESHIM_CLIENTVERSION_H

#define CLIENT_VERSION_MAJOR 0
#define CLIENT_VERSION_MINOR 18
#define CLIENT_VERSION_REVISION 1
#define CLIENT_VERSION_BUILD 0

static const int CLIENT_VERSION =
                           1000000 * CLIENT_VERSION_MAJOR
                         +   10000 * CLIENT_VERSION_MINOR
                         +     100 * CLIENT_VERSION_REVISION
                         +       1 * CLIENT_VERSION_BUILD;

#endif // OMNILITE_STANDALONE_CORESHIM_CLIENTVERSION_H
