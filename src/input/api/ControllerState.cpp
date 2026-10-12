#include "input/api/ControllerState.h"

bool ControllerState::operator==(const ControllerState& other) const
{
	return buttons == other.buttons;

}
