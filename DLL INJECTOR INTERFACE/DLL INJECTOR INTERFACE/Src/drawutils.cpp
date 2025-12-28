#include "drawutils.hpp"
#include "fa_icons.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

ImVec4 ColorLerp(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

float LengthSq(const ImVec2& a) {
    return a.x * a.x + a.y * a.y;
}

bool GlowButton(const char* label, const ImVec2& size_arg) {
    ImGuiStyle& style = ImGui::GetStyle();

    const char* text_end = ImGui::FindRenderedTextEnd(label);

    ImVec2 label_size = ImGui::CalcTextSize(label, text_end);

    ImVec2 size = size_arg;
    if (size.x == 0) size.x = label_size.x + style.FramePadding.x * 2;
    if (size.y == 0) size.y = label_size.y + style.FramePadding.y * 2;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

    bool pressed = ImGui::InvisibleButton(label, size);

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();

    static std::map<ImGuiID, float> animState;
    ImGuiID id = ImGui::GetID(label);
    float& t = animState[id];

    float target_t = (hovered && !active) ? 1.0f : 0.0f;

    float dt = ImGui::GetIO().DeltaTime;
    float speed = 8.0f;
    t = Lerp(t, target_t, dt * speed);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec2 vpMin = ImGui::GetMainViewport()->Pos;
    ImVec2 vpMax = ImVec2(vpMin.x + ImGui::GetMainViewport()->Size.x, vpMin.y + ImGui::GetMainViewport()->Size.y);
    drawList->PushClipRect(vpMin, vpMax, false);

    ImVec4 baseColor = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    ImVec4 hoverColor = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    hoverColor.w = 0.60f;

    if (t > 0.01f) {
        for (int i = 5; i >= 1; i--) {
            float expansion = (float)i * 2.0f;
            float alphaFactor = (0.2f / (float)i);
            float currentAlpha = alphaFactor * (hoverColor.w / 0.4f) * t;

            drawList->AddRectFilled(
                ImVec2(pos.x - expansion, pos.y - expansion),
                ImVec2(pos.x + size.x + expansion, pos.y + size.y + expansion),
                ImGui::GetColorU32(ImVec4(hoverColor.x, hoverColor.y, hoverColor.z, currentAlpha)),
                12.0f
            );
        }
    }

    ImVec4 finalColor = ColorLerp(baseColor, hoverColor, t);
    drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(finalColor), style.FrameRounding);

    if (text_end > label) {
        ImVec2 textPos = ImVec2(center.x - label_size.x * 0.5f, center.y - label_size.y * 0.5f);

        drawList->AddText(textPos, ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), label, text_end);
    }

    drawList->PopClipRect();

    return pressed;
}

bool SmoothCheckbox(const char* label, bool* v) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

    const float square_sz = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;

    const ImRect total_bb(pos, ImVec2(pos.x + square_sz + (label_size.x > 0.0f ? style.ItemSpacing.x + label_size.x : 0.0f), pos.y + square_sz));

    ImGui::ItemSize(total_bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(total_bb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
    if (pressed) {
        *v = !(*v);
        ImGui::MarkItemEdited(id);
    }

    static std::map<ImGuiID, float> anims;
    float& t = anims[id];
    float target = *v ? 1.0f : 0.0f;

    t = Lerp(t, target, ImGui::GetIO().DeltaTime * 10.0f);

    ImU32 col_bg = ImGui::GetColorU32((held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

    float box_pad = 2.0f;
    ImVec2 box_min = ImVec2(pos.x + box_pad, pos.y + box_pad);
    ImVec2 box_max = ImVec2(pos.x + square_sz - box_pad, pos.y + square_sz - box_pad);

    window->DrawList->AddRectFilled(box_min, box_max, col_bg, 4.0f);

    if (t > 0.01f) {
        ImU32 check_col = IM_COL32(50, 150, 250, (int)(255 * t));

        const char* icon = ICON_FA_CHECK;

        ImVec2 icon_size = ImGui::CalcTextSize(icon);
        ImVec2 icon_pos = ImVec2(
            box_min.x + (box_max.x - box_min.x - icon_size.x) * 0.5f,
            box_min.y + (box_max.y - box_min.y - icon_size.y) * 0.5f + 1.5f
        );

        window->DrawList->AddText(ImVec2(icon_pos.x + 0.75f, icon_pos.y), check_col, icon);
        window->DrawList->AddText(icon_pos, check_col, icon);
    }

    if (label_size.x > 0.0f)
        ImGui::RenderText(ImVec2(pos.x + square_sz + style.ItemSpacing.x, pos.y + style.FramePadding.y), label);

    return pressed;
}

void UpdateSmoothScroll(const char* str_id) {
    static std::map<ImGuiID, float> scrollTargets;
    ImGuiID id = ImGui::GetID(str_id);

    float currentScroll = ImGui::GetScrollY();
    float maxScroll = ImGui::GetScrollMaxY();

    if (scrollTargets.find(id) == scrollTargets.end()) {
        scrollTargets[id] = currentScroll;
    }

    float& target = scrollTargets[id];

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            target -= wheel * 150.0f;
            if (target < 0.0f) target = 0.0f;
            if (target > maxScroll) target = maxScroll;
        }
    }

    if (target > maxScroll) target = maxScroll;

    if (std::abs(target - currentScroll) > 0.5f) {
        float nextScroll = Lerp(currentScroll, target, 0.2f);
        ImGui::SetScrollY(nextScroll);
    }
    else {
        ImGui::SetScrollY(target);
    }
}

struct EditorLine {
    int startIndex;
    int endIndex;
    float width;
    float height;
};

struct EditorState {
    int cursorIndex = 0;
    int selectStart = 0;
    int selectEnd = 0;
    float preferredX = 0.0f;
    bool isDragging = false;
};

struct UndoState {
    std::string text;
    int cursorIndex;
    int selectStart;
    int selectEnd;
    float preferredX;
};

static std::map<ImGuiID, EditorState> g_EditorStates;
static std::map<ImGuiID, std::vector<UndoState>> g_UndoStacks;
static std::map<ImGuiID, std::vector<UndoState>> g_RedoStacks;

static bool IsUTF8Continuation(char c) {
    return (c & 0xC0) == 0x80;
}

static int MoveCursor(const char* buf, int cursor, int delta) {
    int len = (int)strlen(buf);
    int next = cursor;
    if (delta > 0) {
        for (int i = 0; i < delta; i++) {
            if (next >= len) break;
            next++;
            while (next < len && IsUTF8Continuation(buf[next])) next++;
        }
    }
    else if (delta < 0) {
        for (int i = 0; i < -delta; i++) {
            if (next <= 0) break;
            next--;
            while (next > 0 && IsUTF8Continuation(buf[next])) next--;
        }
    }
    return next;
}

bool CustomMarqueeEditor(const char* label, char* buf, size_t buf_size, const ImVec2& size_arg, bool isPathEditor) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    ImGuiIO& io = ImGui::GetIO();

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImGui::CalcItemSize(size_arg, 200, 100);
    ImRect bb(pos, pos + size);

    bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    ImU32 bg_col = ImGui::GetColorU32(ImGuiCol_FrameBg);
    if (held) bg_col = ImGui::GetColorU32(ImGuiCol_FrameBgActive);
    else if (hovered) bg_col = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);

    window->DrawList->AddRectFilled(bb.Min, bb.Max, bg_col, style.FrameRounding);
    window->DrawList->AddRect(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);

    char childName[64];
    snprintf(childName, sizeof(childName), "##Child_%s", label);

    bool childVisible = ImGui::BeginChild(childName, size, false, ImGuiWindowFlags_None);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (!childVisible) {
        ImGui::EndChild();
        return false;
    }

    ImGuiWindow* childWindow = ImGui::GetCurrentWindow();
    ImDrawList* drawList = childWindow->DrawList;

    if (g_EditorStates.find(id) == g_EditorStates.end()) {
        g_EditorStates[id] = EditorState();
    }
    EditorState& state = g_EditorStates[id];

    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();
    int bufLen = (int)strlen(buf);

    if (state.cursorIndex > bufLen) state.cursorIndex = bufLen;
    if (state.selectStart > bufLen) state.selectStart = bufLen;
    if (state.selectEnd > bufLen) state.selectEnd = bufLen;

    std::vector<EditorLine> lines;
    float currentY = 0.0f;

    auto CalculateLayout = [&]() {
        lines.clear();
        currentY = 0.0f;

        float wrapWidth = ImGui::GetContentRegionAvail().x;
        if (wrapWidth <= 1.0f) wrapWidth = 100.0f;

        int len = (int)strlen(buf);
        const char* textStart = buf;
        const char* textEnd = buf + len;
        const char* s = textStart;

        while (s < textEnd) {
            const char* nextLineStart = nullptr;
            const char* lineEnd = strchr(s, '\n');

            const char* wrapLimit = lineEnd ? lineEnd : textEnd;
            const char* wrapEnd = font->CalcWordWrapPositionA(1.0f, s, wrapLimit, wrapWidth);

            EditorLine line;
            line.startIndex = (int)(s - buf);

            if (lineEnd && (lineEnd < wrapEnd || wrapEnd == wrapLimit)) {

                line.endIndex = (int)(lineEnd - buf) + 1;
                line.width = font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, s, lineEnd).x;
                nextLineStart = lineEnd + 1;
            }
            else {

                if (wrapEnd == s) wrapEnd++;
                line.endIndex = (int)(wrapEnd - buf);
                line.width = font->CalcTextSizeA(fontSize, FLT_MAX, wrapWidth, s, wrapEnd).x;
                nextLineStart = wrapEnd;
            }

            line.height = fontSize;
            lines.push_back(line);
            currentY += fontSize;
            s = nextLineStart;
        }

        if (lines.empty() || (len > 0 && buf[len - 1] == '\n')) {
            EditorLine line;
            line.startIndex = len;
            line.endIndex = len;
            line.width = 0;
            line.height = fontSize;
            lines.push_back(line);
            currentY += fontSize;
        }
        };

    CalculateLayout();

    ImGui::SetCursorPos(ImVec2(0, 0));
    float contentWidth = ImGui::GetContentRegionAvail().x;

    float buttonHeight = (std::max)(currentY + fontSize, size.y);
    ImGui::InvisibleButton("##EditorHitBox", ImVec2(contentWidth, buttonHeight));

    bool isFocused = ImGui::IsItemActive() || ImGui::IsWindowFocused();
    bool isItemHovered = ImGui::IsItemHovered();
    bool bufferChanged = false;
    bool scrollToCaret = false;

    auto GetLineScreenPos = [&](int lineIdx) -> ImVec2 {
        ImVec2 start = ImGui::GetWindowPos();
        start.x += style.FramePadding.x;
        start.y += style.FramePadding.y - ImGui::GetScrollY() + (lineIdx * fontSize);
        return start;
        };

    auto GetIndexFromPos = [&](const ImVec2& mousePos) -> int {
        if (lines.empty()) return 0;

        float relY = mousePos.y - (ImGui::GetWindowPos().y + style.FramePadding.y - ImGui::GetScrollY());

        int lineIdx = (int)(relY / fontSize);
        if (lineIdx < 0) lineIdx = 0;
        if (lineIdx >= (int)lines.size()) lineIdx = (int)lines.size() - 1;

        const auto& line = lines[lineIdx];
        const char* linePtr = buf + line.startIndex;
        const char* endPtr = buf + line.endIndex;
        if (endPtr > linePtr && *(endPtr - 1) == '\n') endPtr--;

        float startX = ImGui::GetWindowPos().x + style.FramePadding.x - ImGui::GetScrollX();

        int bestIdx = line.endIndex;
        float bestDist = FLT_MAX;
        float curX = startX;

        float distStart = std::abs(mousePos.x - curX);
        if (distStart < bestDist) { bestDist = distStart; bestIdx = line.startIndex; }

        const char* ptr = linePtr;
        while (ptr < endPtr) {
            const char* next = ptr + 1;
            while (next < endPtr && IsUTF8Continuation(*next)) next++;

            float w = font->CalcTextSizeA(fontSize, FLT_MAX, 0, ptr, next).x;
            curX += w;

            float dist = std::abs(mousePos.x - curX);
            if (dist < bestDist) { bestDist = dist; bestIdx = (int)(next - buf); }
            ptr = next;
        }
        return bestIdx;
        };

    if (isFocused) {

        auto RecordUndo = [&]() {
            g_RedoStacks[id].clear();
            auto& stack = g_UndoStacks[id];
            stack.push_back({ std::string(buf), state.cursorIndex, state.selectStart, state.selectEnd, state.preferredX });
            if (stack.size() > 100) stack.erase(stack.begin());
            };

        if (isItemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            state.cursorIndex = GetIndexFromPos(io.MousePos);
            state.preferredX = io.MousePos.x - (ImGui::GetWindowPos().x + style.FramePadding.x - ImGui::GetScrollX());
            if (!io.KeyShift) state.selectStart = state.selectEnd = state.cursorIndex;
            else state.selectEnd = state.cursorIndex;
            state.isDragging = true;
            scrollToCaret = true;
        }
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && state.isDragging) {
            state.cursorIndex = GetIndexFromPos(io.MousePos);
            state.selectEnd = state.cursorIndex;
            state.preferredX = io.MousePos.x - (ImGui::GetWindowPos().x + style.FramePadding.x - ImGui::GetScrollX());
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) state.isDragging = false;

        bool ctrl = io.KeyCtrl;
        bool shift = io.KeyShift;

        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z) && !shift) {
            auto& stack = g_UndoStacks[id];
            if (!stack.empty()) {

                g_RedoStacks[id].push_back({ std::string(buf), state.cursorIndex, state.selectStart, state.selectEnd, state.preferredX });

                UndoState u = stack.back();
                stack.pop_back();

                if (u.text.length() < buf_size) {
                    strcpy_s(buf, buf_size, u.text.c_str());
                    state.cursorIndex = u.cursorIndex;
                    state.selectStart = u.selectStart;
                    state.selectEnd = u.selectEnd;
                    state.preferredX = u.preferredX;
                    bufferChanged = true;
                    scrollToCaret = true;
                }
            }
        }

        if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y) || (shift && ImGui::IsKeyPressed(ImGuiKey_Z)))) {
            auto& stack = g_RedoStacks[id];
            if (!stack.empty()) {

                g_UndoStacks[id].push_back({ std::string(buf), state.cursorIndex, state.selectStart, state.selectEnd, state.preferredX });

                UndoState r = stack.back();
                stack.pop_back();

                if (r.text.length() < buf_size) {
                    strcpy_s(buf, buf_size, r.text.c_str());
                    state.cursorIndex = r.cursorIndex;
                    state.selectStart = r.selectStart;
                    state.selectEnd = r.selectEnd;
                    state.preferredX = r.preferredX;
                    bufferChanged = true;
                    scrollToCaret = true;
                }
            }
        }

        if (!io.InputQueueCharacters.empty()) {
            bool has_printable = false;
            for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
                if ((unsigned char)io.InputQueueCharacters[n] >= 32) {
                    has_printable = true;
                    break;
                }
            }

            if (has_printable) {
                RecordUndo();
                if (state.selectStart != state.selectEnd) {
                    int minS = (std::min)(state.selectStart, state.selectEnd);
                    int maxS = (std::max)(state.selectStart, state.selectEnd);
                    memmove(buf + minS, buf + maxS, bufLen - maxS + 1);
                    state.cursorIndex = minS; state.selectStart = state.selectEnd = minS;
                    bufLen -= (maxS - minS);
                    bufferChanged = true;
                }
                for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
                    if (bufLen < buf_size - 2) {
                        char c = (char)io.InputQueueCharacters[n];
                        if ((unsigned char)c >= 32) {
                            memmove(buf + state.cursorIndex + 1, buf + state.cursorIndex, bufLen - state.cursorIndex + 1);
                            buf[state.cursorIndex] = c; state.cursorIndex++; bufLen++;
                            bufferChanged = true;
                        }
                    }
                }
                state.selectStart = state.selectEnd = state.cursorIndex;
                scrollToCaret = true;
            }
            io.InputQueueCharacters.resize(0);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            RecordUndo();
            if (state.selectStart != state.selectEnd) {
                int minS = (std::min)(state.selectStart, state.selectEnd);
                int maxS = (std::max)(state.selectStart, state.selectEnd);
                memmove(buf + minS, buf + maxS, bufLen - maxS + 1);
                state.cursorIndex = minS; state.selectStart = state.selectEnd = minS;
                bufferChanged = true;
            }
            else if (state.cursorIndex > 0) {
                int prev = MoveCursor(buf, state.cursorIndex, -1);
                memmove(buf + prev, buf + state.cursorIndex, bufLen - state.cursorIndex + 1);
                state.cursorIndex = prev; state.selectStart = state.selectEnd = prev;
                bufferChanged = true;
            }
            scrollToCaret = true;
        }

        if (!isPathEditor && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            RecordUndo();
            if (state.selectStart != state.selectEnd) {
                int minS = (std::min)(state.selectStart, state.selectEnd);
                int maxS = (std::max)(state.selectStart, state.selectEnd);
                memmove(buf + minS, buf + maxS, bufLen - maxS + 1);
                state.cursorIndex = minS; state.selectStart = state.selectEnd = minS; bufLen -= (maxS - minS);
                bufferChanged = true;
            }
            if (bufLen < buf_size - 1) {
                memmove(buf + state.cursorIndex + 1, buf + state.cursorIndex, bufLen - state.cursorIndex + 1);
                buf[state.cursorIndex] = '\n'; state.cursorIndex++; bufLen++;
                state.selectStart = state.selectEnd = state.cursorIndex;
                bufferChanged = true;
            }
            scrollToCaret = true;
        }

        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            if (state.selectStart != state.selectEnd) {
                int minS = (std::min)(state.selectStart, state.selectEnd);
                int maxS = (std::max)(state.selectStart, state.selectEnd);
                std::string s(buf + minS, maxS - minS);
                ImGui::SetClipboardText(s.c_str());
            }
        }

        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X)) {
            if (state.selectStart != state.selectEnd) {
                RecordUndo();

                int minS = (std::min)(state.selectStart, state.selectEnd);
                int maxS = (std::max)(state.selectStart, state.selectEnd);
                std::string s(buf + minS, maxS - minS);
                ImGui::SetClipboardText(s.c_str());

                memmove(buf + minS, buf + maxS, bufLen - maxS + 1);
                state.cursorIndex = minS;
                state.selectStart = state.selectEnd = minS;
                bufLen -= (maxS - minS);
                bufferChanged = true;
                scrollToCaret = true;
            }
        }

        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            const char* clip = ImGui::GetClipboardText();
            if (clip) {
                RecordUndo();
                if (state.selectStart != state.selectEnd) {
                    int minS = (std::min)(state.selectStart, state.selectEnd);
                    int maxS = (std::max)(state.selectStart, state.selectEnd);
                    memmove(buf + minS, buf + maxS, bufLen - maxS + 1);
                    state.cursorIndex = minS; bufLen -= (maxS - minS);
                }
                int clipLen = (int)strlen(clip);
                if (bufLen + clipLen < buf_size) {
                    memmove(buf + state.cursorIndex + clipLen, buf + state.cursorIndex, bufLen - state.cursorIndex + 1);
                    memcpy(buf + state.cursorIndex, clip, clipLen);
                    state.cursorIndex += clipLen;
                    state.selectStart = state.selectEnd = state.cursorIndex;
                    bufferChanged = true;
                }
            }
            scrollToCaret = true;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            state.cursorIndex = MoveCursor(buf, state.cursorIndex, -1);
            if (!shift) state.selectStart = state.selectEnd = state.cursorIndex; else state.selectEnd = state.cursorIndex;
            state.preferredX = -1.0f;
            scrollToCaret = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            state.cursorIndex = MoveCursor(buf, state.cursorIndex, 1);
            if (!shift) state.selectStart = state.selectEnd = state.cursorIndex; else state.selectEnd = state.cursorIndex;
            state.preferredX = -1.0f;
            scrollToCaret = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            bool up = ImGui::IsKeyPressed(ImGuiKey_UpArrow);
            int lineIdx = -1;

            for (int i = 0; i < lines.size(); i++) {
                if (state.cursorIndex >= lines[i].startIndex && state.cursorIndex < lines[i].endIndex) { lineIdx = i; break; }
                if (i == lines.size() - 1 && state.cursorIndex == lines[i].endIndex) { lineIdx = i; break; }
            }
            if (lineIdx != -1) {
                if (state.preferredX < 0) {
                    const auto& l = lines[lineIdx];
                    const char* le = buf + (std::min)((int)strlen(buf), l.endIndex);
                    if (le > buf + l.startIndex && *(le - 1) == '\n' && state.cursorIndex == l.endIndex) le--;
                    state.preferredX = font->CalcTextSizeA(fontSize, FLT_MAX, 0, buf + l.startIndex, buf + state.cursorIndex).x;
                }
                int tLine = up ? lineIdx - 1 : lineIdx + 1;
                if (tLine >= 0 && tLine < lines.size()) {
                    const auto& l = lines[tLine];
                    const char* lStart = buf + l.startIndex;
                    const char* lEnd = buf + l.endIndex;
                    if (lEnd > lStart && *(lEnd - 1) == '\n') lEnd--;

                    float bestDist = FLT_MAX;
                    int bestIdx = l.startIndex;
                    float curX = 0;
                    float distS = std::abs(curX - state.preferredX);
                    if (distS < bestDist) { bestDist = distS; bestIdx = l.startIndex; }

                    const char* s = lStart;
                    while (s < lEnd) {
                        const char* n = s + 1;
                        while (n < lEnd && IsUTF8Continuation(*n)) n++;
                        curX += font->CalcTextSizeA(fontSize, FLT_MAX, 0, s, n).x;
                        if (std::abs(curX - state.preferredX) < bestDist) { bestDist = std::abs(curX - state.preferredX); bestIdx = (int)(n - buf); }
                        s = n;
                    }
                    state.cursorIndex = bestIdx;
                }
            }
            if (!shift) state.selectStart = state.selectEnd = state.cursorIndex; else state.selectEnd = state.cursorIndex;
            scrollToCaret = true;
        }

        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) { state.selectStart = 0; state.selectEnd = bufLen; state.cursorIndex = bufLen; }
    }

    if (bufferChanged) {
        CalculateLayout();

        bufLen = (int)strlen(buf);
    }

    if (isFocused && scrollToCaret) {

        for (int i = 0; i < lines.size(); i++) {
            bool here = (state.cursorIndex >= lines[i].startIndex && state.cursorIndex < lines[i].endIndex);

            if (i == lines.size() - 1 && state.cursorIndex == lines[i].endIndex) here = true;

            if (here) {
                float lTop = i * fontSize;
                float lBot = lTop + fontSize;

                if (lTop < ImGui::GetScrollY()) ImGui::SetScrollY(lTop);

                float visibleHeight = size.y - style.FramePadding.y * 2;
                if (lBot > ImGui::GetScrollY() + visibleHeight - fontSize) {
                    ImGui::SetScrollY(lBot - visibleHeight + fontSize);
                }
                break;
            }
        }
    }

    size_t validPrefixLen = 0;
    bool isValidIsFile = false;

    ImU32 colYellow = IM_COL32(255, 255, 120, 255);
    ImU32 colGreen = IM_COL32(50, 205, 130, 255);
    ImU32 colRed = IM_COL32(255, 100, 100, 255);

    ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

    if (isPathEditor && bufLen > 0) {
        std::string s(buf);
        std::error_code ec;

        bool fullPathExists = std::filesystem::exists(s, ec);

        if (fullPathExists && !s.empty() && s.back() == '.') {
            if (std::filesystem::is_directory(s, ec)) {

                size_t lastSlash = s.find_last_of("/\\");
                std::string lastComp = (lastSlash == std::string::npos) ? s : s.substr(lastSlash + 1);

                if (lastComp != "." && lastComp != "..") {
                    fullPathExists = false;
                }
            }
        }

        if (fullPathExists) {
            validPrefixLen = bufLen;
            isValidIsFile = !std::filesystem::is_directory(s, ec);
        }
        else {

            size_t scanPos = s.length();
            while (scanPos > 0) {
                size_t lastSep = s.find_last_of("/\\", scanPos - 1);

                if (lastSep == std::string::npos) {

                    std::string sub = s.substr(0, scanPos);

                    if (sub.length() == 2 && sub[1] == ':') sub += "\\";

                    if (std::filesystem::exists(sub, ec)) {
                        validPrefixLen = scanPos;
                        isValidIsFile = !std::filesystem::is_directory(sub, ec);
                    }
                    break;
                }

                std::string sub = s.substr(0, lastSep);
                if (sub.empty()) sub = "\\";
                else if (sub.length() == 2 && sub[1] == ':') sub += "\\";

                if (std::filesystem::exists(sub, ec)) {

                    validPrefixLen = lastSep + 1;
                    isValidIsFile = false;
                    break;
                }

                scanPos = lastSep;
            }
        }
    }

    for (int i = 0; i < lines.size(); i++) {
        ImVec2 linePos = GetLineScreenPos(i);

        if (linePos.y + fontSize < bb.Min.y) continue;
        if (linePos.y > bb.Max.y) break;

        const auto& line = lines[i];

        if (state.selectStart != state.selectEnd) {
            int minS = (std::min)(state.selectStart, state.selectEnd);
            int maxS = (std::max)(state.selectStart, state.selectEnd);
            int lMin = (std::max)(minS, line.startIndex);
            int lMax = (std::min)(maxS, line.endIndex);

            if (lMin < lMax) {
                float x1 = font->CalcTextSizeA(fontSize, FLT_MAX, 0, buf + line.startIndex, buf + lMin).x;
                float x2 = font->CalcTextSizeA(fontSize, FLT_MAX, 0, buf + line.startIndex, buf + lMax).x;
                drawList->AddRectFilled(
                    ImVec2(linePos.x + x1, linePos.y),
                    ImVec2(linePos.x + x2, linePos.y + fontSize),
                    IM_COL32(50, 100, 200, 100)
                );
            }
        }

        int lStart = line.startIndex;
        int lEnd = line.endIndex;

        auto DrawSegment = [&](int start, int end, ImU32 col) {
            float xOffset = font->CalcTextSizeA(fontSize, FLT_MAX, 0, buf + line.startIndex, buf + start).x;
            drawList->AddText(ImVec2(linePos.x + xOffset, linePos.y), col, buf + start, buf + end);
            };

        if (isPathEditor) {
            int currentPos = lStart;
            while (currentPos < lEnd) {
                ImU32 color = colRed;
                int nextChange = lEnd;

                if (currentPos < validPrefixLen) {

                    int validEnd = (std::min)(lEnd, (int)validPrefixLen);

                    if (isValidIsFile) {

                        std::string s(buf, validPrefixLen);
                        size_t lastSep = s.find_last_of("/\\");
                        int fileStartIdx = (lastSep == std::string::npos) ? 0 : (int)lastSep + 1;

                        if (currentPos < fileStartIdx) {

                            color = colYellow;
                            nextChange = (std::min)(validEnd, fileStartIdx);
                        }
                        else {

                            std::string ext = std::filesystem::path(s).extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if (ext == ".dll") color = colGreen;
                            else color = colRed;

                            nextChange = validEnd;
                        }
                    }
                    else {

                        color = colYellow;
                        nextChange = validEnd;
                    }
                }
                else {

                    color = colRed;
                    nextChange = lEnd;
                }

                DrawSegment(currentPos, nextChange, color);
                currentPos = nextChange;
            }
        }
        else {

            drawList->AddText(linePos, textCol, buf + lStart, buf + lEnd);
        }

        if (isFocused && (int)(g.Time * 2.0f) % 2 == 0) {
            bool isCursorHere = (state.cursorIndex >= line.startIndex && state.cursorIndex < line.endIndex);

            if (state.cursorIndex == line.endIndex && i == lines.size() - 1) isCursorHere = true;

            if (isCursorHere) {
                float cx = font->CalcTextSizeA(fontSize, FLT_MAX, 0, buf + line.startIndex, buf + state.cursorIndex).x;
                drawList->AddLine(
                    ImVec2(linePos.x + cx, linePos.y),
                    ImVec2(linePos.x + cx, linePos.y + fontSize),
                    ImGui::GetColorU32(ImGuiCol_Text)
                );
            }
        }
    }

    if (bufLen == 0 && !isFocused) {
        drawList->AddText(GetLineScreenPos(0), ImGui::GetColorU32(ImGuiCol_TextDisabled), isPathEditor ? "Paste path here..." : "Enter text...");
    }

    ImGui::EndChild();
    return isFocused;
}
