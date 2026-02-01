/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "OnScreenDisplay.hpp"

#include <RMG-Core/Settings.hpp>

#include <backends/imgui_impl_opengl3.h>
#include <imgui.h>
#include <cmath>
#include <chrono>
#include <deque>

//
// Local Variables
//

static bool l_Initialized     = false;
static bool l_Enabled         = false;
static bool l_RenderingPaused = false;

static std::chrono::time_point<std::chrono::high_resolution_clock> l_MessageTime;
static std::string l_Message;
struct KailleraChatEntry
{
    std::string message;
    std::chrono::time_point<std::chrono::high_resolution_clock> time;
};

static std::deque<KailleraChatEntry> l_KailleraChatMessages;
static int         l_MessagePosition = 1;
static float       l_MessagePaddingX = 20.0f;
static float       l_MessagePaddingY = 20.0f;
static float       l_BackgroundRed   = 1.0f;
static float       l_BackgroundGreen = 1.0f;
static float       l_BackgroundBlue  = 1.0f;
static float       l_BackgroundAlpha = 1.0f;
static float       l_TextRed         = 1.0f;
static float       l_TextGreen       = 1.0f;
static float       l_TextBlue        = 1.0f;
static float       l_TextAlpha       = 1.0f;
static int         l_MessageDuration = 6;
static float       l_MessageScale    = 1.0f;
static size_t      l_KailleraChatMaxMessages = 5;
static bool        l_FontsDirty      = true;
static const float l_BaseFontSize    = 13.0f;

static void OnScreenDisplayUpdateFonts(void)
{
    if (!l_FontsDirty)
    {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig config;
    config.SizePixels = l_BaseFontSize * l_MessageScale;

    io.Fonts->Clear();
    io.FontDefault = io.Fonts->AddFontDefault(&config);

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();

    l_FontsDirty = false;
}

//
// Exported Functions
//

bool OnScreenDisplayInit(void)
{
    if (l_Initialized)
    {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    if (!ImGui_ImplOpenGL3_Init())
    {
        return false;
    }

    l_FontsDirty = true;
    l_Initialized = true;
    return true;
}

void OnScreenDisplayShutdown(void)
{
    if (!l_Initialized)
    {
        return;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    l_Message         = "";
    l_KailleraChatMessages.clear();
    l_Initialized     = false;
    l_RenderingPaused = false;
}

void OnScreenDisplayLoadSettings(void)
{
    l_Enabled         = CoreSettingsGetBoolValue(SettingsID::GUI_OnScreenDisplayEnabled);
    l_MessagePosition = CoreSettingsGetIntValue(SettingsID::GUI_OnScreenDisplayLocation);
    l_MessagePaddingX = CoreSettingsGetIntValue(SettingsID::GUI_OnScreenDisplayPaddingX);
    l_MessagePaddingY = CoreSettingsGetIntValue(SettingsID::GUI_OnScreenDisplayPaddingY);
    l_MessageDuration = CoreSettingsGetIntValue(SettingsID::GUI_OnScreenDisplayDuration);
    float newMessageScale = CoreSettingsGetFloatValue(SettingsID::GUI_OnScreenDisplayScale);
    if (newMessageScale <= 0.1f)
    {
        newMessageScale = 1.0f;
    }
    if (std::abs(newMessageScale - l_MessageScale) > 0.001f)
    {
        l_MessageScale = newMessageScale;
        l_FontsDirty = true;
    }
    int maxChatMessages = CoreSettingsGetIntValue(SettingsID::GUI_OnScreenDisplayMaxMessages);
    if (maxChatMessages < 1)
    {
        maxChatMessages = 1;
    }
    l_KailleraChatMaxMessages = static_cast<size_t>(maxChatMessages);

    std::vector<int> backgroundColor = CoreSettingsGetIntListValue(SettingsID::GUI_OnScreenDisplayBackgroundColor);
    std::vector<int> textColor       = CoreSettingsGetIntListValue(SettingsID::GUI_OnScreenDisplayTextColor);
    if (backgroundColor.size() == 4)
    {
        l_BackgroundRed   = backgroundColor.at(0) / 255.0f;
        l_BackgroundGreen = backgroundColor.at(1) / 255.0f;
        l_BackgroundBlue  = backgroundColor.at(2) / 255.0f;
        l_BackgroundAlpha = backgroundColor.at(3) / 255.0f;
    }
    if (textColor.size() == 4)
    {
        l_TextRed   = textColor.at(0) / 255.0f;
        l_TextGreen = textColor.at(1) / 255.0f;
        l_TextBlue  = textColor.at(2) / 255.0f;
        l_TextAlpha = textColor.at(3) / 255.0f;
    }

    while (l_KailleraChatMessages.size() > l_KailleraChatMaxMessages)
    {
        l_KailleraChatMessages.pop_front();
    }
}

bool OnScreenDisplaySetDisplaySize(int width, int height)
{
    if (!l_Initialized)
    {
        return false;
    }

    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);
    return true;
}

void OnScreenDisplaySetMessage(std::string message)
{
    if (!l_Initialized)
    {
        return;
    }

    l_Message     = message;
    l_MessageTime = std::chrono::high_resolution_clock::now();
}

void OnScreenDisplaySetKailleraChatMessage(std::string message)
{
    if (!l_Initialized)
    {
        return;
    }

    if (message.empty())
    {
        l_KailleraChatMessages.clear();
        return;
    }

    l_KailleraChatMessages.push_back({std::move(message), std::chrono::high_resolution_clock::now()});
    while (l_KailleraChatMessages.size() > l_KailleraChatMaxMessages)
    {
        l_KailleraChatMessages.pop_front();
    }
}

void OnScreenDisplayRender(void)
{
    if (!l_Initialized || l_RenderingPaused)
    {
        return;
    }

    const auto currentTime = std::chrono::high_resolution_clock::now();

    const bool hasSystemMessage = l_Enabled && !l_Message.empty() &&
        (std::chrono::duration_cast<std::chrono::seconds>(currentTime - l_MessageTime).count() < l_MessageDuration);

    while (!l_KailleraChatMessages.empty())
    {
        const auto ageSeconds = std::chrono::duration_cast<std::chrono::seconds>(currentTime - l_KailleraChatMessages.front().time).count();
        if (ageSeconds < l_MessageDuration)
        {
            break;
        }
        l_KailleraChatMessages.pop_front();
    }

    const bool hasKailleraChatMessage = l_Enabled && !l_KailleraChatMessages.empty();

    if (!hasSystemMessage && !hasKailleraChatMessage)
    {
        return;
    }

    OnScreenDisplayUpdateFonts();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    
    ImGuiIO& io = ImGui::GetIO();

    if (hasSystemMessage)
    {
        // right bottom = ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 20.0f, io.DisplaySize.y - 20.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        // right top    = ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 20.0f, 20.0f), ImGuiCond_Always, ImVec2(1.0f, 0));
        // left  bottom = ImGui::SetNextWindowPos(ImVec2(20.0f, io.DisplaySize.y - 20.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        // left  top    = ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
        switch (l_MessagePosition)
        {
        default:
        case 0: // left bottom
            ImGui::SetNextWindowPos(ImVec2(l_MessagePaddingX, io.DisplaySize.y - l_MessagePaddingY), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            break;
        case 1: // left top
            ImGui::SetNextWindowPos(ImVec2(l_MessagePaddingX, l_MessagePaddingY), ImGuiCond_Always, ImVec2(0.0f, 0.0f));
            break;
        case 2: // right top
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - l_MessagePaddingX, l_MessagePaddingY), ImGuiCond_Always, ImVec2(1.0f, 0));
            break;
        case 3: // right bottom
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - l_MessagePaddingX, io.DisplaySize.y - l_MessagePaddingY), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            break;
        }

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(l_BackgroundRed, l_BackgroundGreen, l_BackgroundBlue, l_BackgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Text,     ImVec4(l_TextRed, l_TextGreen, l_TextBlue, l_TextAlpha));

        ImGui::Begin("Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::Text("%s", l_Message.c_str());
        ImGui::End();

        ImGui::PopStyleColor(2);
    }

    if (hasKailleraChatMessage)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(l_BackgroundRed, l_BackgroundGreen, l_BackgroundBlue, l_BackgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Text,     ImVec4(l_TextRed, l_TextGreen, l_TextBlue, l_TextAlpha));

        float baseX = 0.0f;
        float baseY = 0.0f;
        ImVec2 pivot(0.0f, 0.0f);
        switch (l_MessagePosition)
        {
        default:
        case 0: // left bottom
            baseX = l_MessagePaddingX;
            baseY = io.DisplaySize.y - l_MessagePaddingY;
            pivot = ImVec2(0.0f, 1.0f);
            break;
        case 1: // left top
            baseX = l_MessagePaddingX;
            baseY = l_MessagePaddingY;
            pivot = ImVec2(0.0f, 0.0f);
            break;
        case 2: // right top
            baseX = io.DisplaySize.x - l_MessagePaddingX;
            baseY = l_MessagePaddingY;
            pivot = ImVec2(1.0f, 0.0f);
            break;
        case 3: // right bottom
            baseX = io.DisplaySize.x - l_MessagePaddingX;
            baseY = io.DisplaySize.y - l_MessagePaddingY;
            pivot = ImVec2(1.0f, 1.0f);
            break;
        }

        const bool anchorBottom = (l_MessagePosition == 0 || l_MessagePosition == 3);
        const float stackSpacingFactor = 1.5f;
        float offsetY = 0.0f;
        int messageIndex = 0;

        for (auto messageIter = l_KailleraChatMessages.rbegin(); messageIter != l_KailleraChatMessages.rend(); ++messageIter, ++messageIndex)
        {
            const float posY = anchorBottom ? (baseY - offsetY) : (baseY + offsetY);
            ImGui::SetNextWindowPos(ImVec2(baseX, posY), ImGuiCond_Always, pivot);

            const std::string windowName = "Kaillera Chat##" + std::to_string(messageIndex);
            ImGui::Begin(windowName.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::Text("%s", messageIter->message.c_str());
            const ImVec2 windowSize = ImGui::GetWindowSize();
            ImGui::End();

            offsetY += windowSize.y * stackSpacingFactor;
        }

        ImGui::PopStyleColor(2);
    }

    ImGui::Render();

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void OnScreenDisplayPause(void)
{
    l_RenderingPaused = true;
}

void OnScreenDisplayResume(void)
{
    l_RenderingPaused = false;
}
