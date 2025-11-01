#ifndef DUMMYBACKEND_H
#define DUMMYBACKEND_H

#include "IBackendInterface.h"
#include <QRandomGenerator>

class DummyBackend : public IBackendInterface
{
public:
    DataPacket getNextFrame() override;
};

#endif // DUMMYBACKEND_H