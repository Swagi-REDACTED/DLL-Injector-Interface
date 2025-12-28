#pragma once
#include <string>
#include <vector>
#include <map>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"

float Lerp(float a, float b, float t);
ImVec4 ColorLerp(const ImVec4& a, const ImVec4& b, float t);
float LengthSq(const ImVec2& a);

bool GlowButton(const char* label, const ImVec2& size_arg = ImVec2(0, 0));
bool SmoothCheckbox(const char* label, bool* v);
void UpdateSmoothScroll(const char* str_id);

bool CustomMarqueeEditor(const char* label, char* buf, size_t buf_size, const ImVec2& size_arg, bool isPathEditor);
