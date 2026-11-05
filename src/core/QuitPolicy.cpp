// SPDX-License-Identifier: GPL-3.0-or-later
#include "QuitPolicy.h"

namespace QuitPolicy
{
bool needsConfirmation(Origin origin, bool phaseActive)
{
    return origin == Origin::External && phaseActive;
}
} // namespace QuitPolicy
