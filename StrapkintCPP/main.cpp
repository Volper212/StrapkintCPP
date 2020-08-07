#define NOMINMAX
#include <chrono>
#include <string>
#include <unordered_set>
#include <future>
#include <fstream>
#include <WS2tcpip.h>
#include <Windows.h>

#pragma comment (lib, "ws2_32.lib")

template<typename T, size_t N>
constexpr size_t lengthof(T(&array)[N]) {
    return N;
}

template<typename T, typename U>
bool maxAssign(T &variable, U value) {
    if (variable < value) {
        variable = value;
        return true;
    }
    return false;
}

template<typename T, typename U>
bool minAssign(T &variable, U value) {
    if (variable > value) {
        variable = value;
        return true;
    }
    return false;
}

int main() {
    constexpr int viewportWidth = 150, viewportHeight = 40, viewportArea = viewportWidth * viewportHeight, cameraX = viewportWidth / 3, cameraY = viewportHeight / 2;
    constexpr double horizontalSpeed = 20;

    // Setup
    SetConsoleTitleA("Strapkint");
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
    CHAR_INFO *buffer = new CHAR_INFO[viewportArea]{};
    for (size_t i = 0; i < viewportArea; ++i)
        buffer[i].Attributes = 0xf0;
    unsigned short &coinColor = buffer[viewportWidth * 3 - 8].Attributes;
    coinColor = 0xf6;
    const COORD bufferSize{ viewportWidth, viewportHeight };
    COORD bufferPosition{ 0, 0 };
    const SMALL_RECT fullWriteRegion{ 0, 0, viewportWidth - 1, viewportHeight - 1 };
    SMALL_RECT writeRegion = fullWriteRegion;
    const SMALL_RECT noWriteRegion{ viewportWidth, viewportHeight, -1, -1 };
    {
        const SMALL_RECT windowRect{ 0, 0, viewportWidth - 3, viewportHeight - 1 };
        SetConsoleScreenBufferSize(handle, bufferSize);
        SetConsoleWindowInfo(handle, TRUE, &windowRect);
        CONSOLE_SCREEN_BUFFER_INFOEX bufferInfo{ sizeof(CONSOLE_SCREEN_BUFFER_INFOEX) };
        GetConsoleScreenBufferInfoEx(handle, &bufferInfo);
        ++bufferInfo.srWindow.Right;
        ++bufferInfo.srWindow.Bottom;
        const COLORREF colorTable[] = {
            0x0c0c0c, 0xda3700, 0x0ea113, 0xddb43a,
            0x1f0fc5, 0x981788, 0x009cc1, 0xcccccc,
            0x767676, 0xff783b, 0x0cc616, 0xd6d661,
            0x5648e7, 0x9e00b4, 0xa5f1f9, 0xf2f2f2
        };
        memcpy(bufferInfo.ColorTable, colorTable, sizeof(colorTable));
        SetConsoleScreenBufferInfoEx(handle, &bufferInfo);
        const HMENU systemMenu = GetSystemMenu(GetConsoleWindow(), FALSE);
        DeleteMenu(systemMenu, 0xf000, 0);
        DeleteMenu(systemMenu, 0xf030, 0);
        unsigned long mode;
        GetConsoleMode(inputHandle, &mode);
        mode &= ~0x40;
        SetConsoleMode(inputHandle, mode);
    }

    // Components
    struct Text {
        short x, y;
        size_t renderLength;
        std::string text;
    };

    struct ConstText {
        short x, y, width, height;
        const wchar_t *text;
    };

    struct Collision {
        double x, y, width, height;
    };

    struct Level {
        int width, height, offsetX, offsetY;
        std::vector<ConstText> visibleTexts;
        std::vector<Collision> collisions;
    };

    const wchar_t *fpsString = L"FPS: ";

    std::vector<ConstText> visibleConstFixedTexts{ { 0, 0, 5, 1, fpsString }, { viewportWidth - 11, 0, 11U, 4U,
        L"┌─────────┐"
        L"│ LVL     │"
        L"│  ©      │"
        L"└─────────┘"
    } };
    std::vector<Text> visibleFixedTexts{ { 5, 0, 0, "" }, { viewportWidth - 5, 1, 3, "000" }, { viewportWidth - 5, 2, 3, "000" } };

    constexpr ConstText playerText{
        0, 0, 3, 3,
        LR"( O )"
        LR"(/|\)"
        LR"(/ \)"
    };

    constexpr Collision playerCollision{ 0, 0, 3, 3 };

    std::vector<Level> levels{
        {
            200, 60, 0, 0,
            {
                playerText,
                {
                    70, 20, 12, 1,
                    L"testowe sqrt"
                }
            },
            {
                playerCollision
            }
        },
        {
            150, 40, 0, 0,
            {
                playerText,
                {
                    10, 10, 3, 1,
                    L"tps"
                }
            },
            {
                playerCollision
            }
        }
    };
    unsigned levelIndex = 0;
    Level *level = &levels[levelIndex];

    const int player = 0;

    // Time
    std::chrono::nanoseconds deltaTime(0), timeCounter(0);
    auto lastFrame = std::chrono::steady_clock::now();
    int frameCounter = -1;

    // Input variables
    INPUT_RECORD inputRecords[16];
    unsigned long eventsRead;
    std::unordered_set<unsigned short> pressedKeys;
    int tpsSequence = 0;
    bool tps = false;

    // Functions
    const auto adjustWriteRegion = [&writeRegion, &bufferPosition](int x, int y, int width, int height = 1) {
        auto &[Left, Top, Right, Bottom] = writeRegion;
        minAssign(Left, x);
        maxAssign(Left, 0);
        minAssign(Top, y);
        maxAssign(Top, 0);
        maxAssign(Right, x + width - 1);
        minAssign(Right, viewportWidth - 1);
        maxAssign(Bottom, y + height - 1);
        minAssign(Bottom, viewportHeight - 1);
        bufferPosition = { Left, Top };
    };

    const auto fillWriteRegion = [&writeRegion, &bufferPosition, &fullWriteRegion] {
        writeRegion = fullWriteRegion;
        bufferPosition = { 0, 0 };
    };

    const auto updateCounter = [&](int entity, int value) {
        std::string number = std::to_string(value);
        visibleFixedTexts[entity].text = std::string(3 - number.size(), '0') + number;
    };

    const auto moveHorizontally = [&](double distance) {
        Collision &collision = level->collisions[player];
        ConstText &text = level->visibleTexts[player];

        collision.x += distance;

        const auto changeLevel = [&](int levelIndex) {
            level = &levels[levelIndex];
            fillWriteRegion();
            level->collisions[player].y = collision.y;
            short &after = level->visibleTexts[player].y;
            after = text.y;
            updateCounter(1, levelIndex);
            if (after > cameraY) {
                level->offsetY = after - cameraY;
                minAssign(level->offsetY, level->height - viewportHeight);
            }
        };

        if (maxAssign(collision.x, 0.) && levelIndex > 0)
            return changeLevel(--levelIndex);
        if (minAssign(collision.x, level->width - collision.width) && levelIndex < levels.size() - 1)
            return changeLevel(++levelIndex);

        const int after = (int)round(collision.x);
        const int difference = text.x - after;
        if (!difference) return;

        const int before = level->offsetX;
        level->offsetX = after - cameraX;
        maxAssign(level->offsetX, 0);
        minAssign(level->offsetX, level->width - viewportWidth);
        if (before == level->offsetX) {
            const int viewportX = text.x - level->offsetX;
            const int viewportY = viewportHeight - text.y + level->offsetY - text.height;

            if (distance < 0)
                adjustWriteRegion(viewportX - difference, viewportY, text.width + difference, text.height);
            else
                adjustWriteRegion(viewportX, viewportY, text.width - difference, text.height);
        } else {
            fillWriteRegion();
        }

        text.x = after;
    };

    const auto moveVertically = [&](double distance) {
        Collision &collision = level->collisions[player];
        ConstText &text = level->visibleTexts[player];

        collision.y += distance;

        maxAssign(collision.y, 0.);

        const int after = (int)round(collision.y);
        const int difference = text.y - after;
        if (!difference) return;

        const int before = level->offsetY;
        level->offsetY = after - cameraY;
        maxAssign(level->offsetY, 0);
        minAssign(level->offsetY, level->height - viewportHeight);
        if (before == level->offsetY) {
            const int viewportX = text.x - level->offsetX;
            const int viewportY = viewportHeight - text.y + level->offsetY - text.height;

            if (distance < 0)
                adjustWriteRegion(viewportX, viewportY, text.width, text.height + difference);
            else
                adjustWriteRegion(viewportX, viewportY + difference, text.width, text.height - difference);
        } else {
            fillWriteRegion();
        }

        text.y = after;
    };

    // Game loop
    while (true) {
        const double dtSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(deltaTime).count();

        // Update
        { // FPS counter
            timeCounter += deltaTime;
            ++frameCounter;
            int countedMilliseconds = std::chrono::duration_cast<std::chrono::duration<int, std::milli>>(timeCounter).count();
            if (countedMilliseconds > 500) {
                auto &[x, y, renderLength, text] = visibleFixedTexts[0];
                renderLength = text.size();
                text = std::to_string(frameCounter * 1000 / countedMilliseconds);
                maxAssign(renderLength, text.size());
                timeCounter = std::chrono::nanoseconds(0);
                frameCounter = 0;
                adjustWriteRegion(x, y, (short)renderLength);
            }
        }

        // Render
        if (writeRegion.Right >= 0) {
            for (size_t i = 0; i < viewportArea; ++i) {
                buffer[i].Char.UnicodeChar = '\0';
            }
            for (const auto &[x, y, width, height, text] : level->visibleTexts) {
                for (int currentY = 0; currentY < height; ++currentY) {
                    const int viewportY = viewportHeight - y + currentY + level->offsetY - height;
                    if (viewportY + height < 0 || viewportY >= viewportHeight) continue;
                    for (int currentX = 0; currentX < width; ++currentX) {
                        const int viewportX = x + currentX - level->offsetX;
                        if (viewportX < 0 || viewportX >= viewportWidth) continue;
                        buffer[viewportX + viewportWidth * viewportY].Char.UnicodeChar = text[currentX + width * currentY];
                    }
                }
            }
            for (const auto &[x, y, width, height, text] : visibleConstFixedTexts)
                for (int currentY = 0; currentY < height; ++currentY)
                    for (int currentX = 0; currentX < width; ++currentX)
                        buffer[x + currentX + viewportWidth * (y + currentY)].Char.UnicodeChar = text[currentX + width * currentY];
            for (const auto &[x, y, renderLength, text] : visibleFixedTexts)
                for (size_t i = 0; i < text.size(); ++i)
                    buffer[x + i + viewportWidth * y].Char.UnicodeChar = text[i];
            WriteConsoleOutputW(handle, buffer, bufferSize, bufferPosition, &writeRegion);
            writeRegion = noWriteRegion;
        }

        if (pressedKeys.size()) {
            for (const auto key : pressedKeys) {
                switch (key) {
                case VK_LEFT:
                    moveHorizontally(-dtSeconds * horizontalSpeed);
                    break;
                case VK_RIGHT:
                    moveHorizontally(dtSeconds * horizontalSpeed);
                    break;
                case VK_DOWN:
                    moveVertically(-dtSeconds * horizontalSpeed / 2);
                    break;
                case VK_UP:
                    moveVertically(dtSeconds * horizontalSpeed / 2);
                }
            }
        }

        // Input
        GetNumberOfConsoleInputEvents(inputHandle, &eventsRead);
        if (eventsRead) {
            ReadConsoleInputW(inputHandle, inputRecords, lengthof(inputRecords), &eventsRead);
            for (unsigned long i = 0; i < eventsRead; ++i) {
                if (inputRecords[i].EventType != KEY_EVENT) continue;
                KEY_EVENT_RECORD event = inputRecords[i].Event.KeyEvent;
                if (event.bKeyDown) {
                    if (pressedKeys.contains(event.wVirtualKeyCode)) continue;
                    pressedKeys.insert(event.wVirtualKeyCode);
                    switch (event.wVirtualKeyCode) {
                    case VK_ESCAPE:
                        return 0;
                    case 0x54: // T
                        for (size_t i = 0; i < viewportArea; ++i)
                            buffer[i].Attributes ^= 0xff;
                        coinColor ^= 0x07;
                        fillWriteRegion();
                        tpsSequence = 1;
                        break;
                    case 0x50: // P
                        tpsSequence = tpsSequence == 1 ? 2 : 0;
                        break;
                    case 0x53: // S
                        if (tpsSequence == 2) {
                            tps ^= true;
                            ConstText &fps = visibleConstFixedTexts[0];
                            fps.text = tps ? L"TPS: " : fpsString;
                            adjustWriteRegion(fps.x, fps.y, 1);
                        }
                    default:
                        tpsSequence = 0;
                    }

                } else {
                    pressedKeys.erase(event.wVirtualKeyCode);
                }
            }
        }

        auto currentFrame = std::chrono::steady_clock::now();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
    }
}
