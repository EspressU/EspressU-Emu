#pragma once

#include <cstddef>
#include <string>

namespace WindowSystem
{
void ShowNativeKeyboard(std::u16string text, size_t maxLength, size_t cursor);
void HideNativeKeyboard();
bool PollNativeKeyboard(std::u16string& text, bool& accepted);
}
